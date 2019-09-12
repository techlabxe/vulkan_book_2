#include "Model.h"
#include "loader/PMDloader.h"

#include "VulkanAppBase.h"
#include "VulkanBookUtil.h"

#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace std;
using namespace glm;




inline Model::PMDVertex convertTo(const loader::PMDVertex& v)
{
  return Model::PMDVertex{
    v.getPosition(), v.getNormal(), v.getUV(),
    uvec2(v.getBoneIndex(0), v.getBoneIndex(1)),
    vec2(v.getBoneWeight(0), v.getBoneWeight(1)),
    v.getEdgeFlag(),
  };
}

void Material::Update(VulkanAppBase* app)
{
  auto bufferSize = uint32_t(sizeof(m_parameters));
  app->WriteToHostVisibleMemory(m_uniformBuffer.memory, bufferSize, &m_parameters );
}

void Bone::UpdateLocalMatrix()
{
  m_mtxLocal = glm::translate(m_translation) * glm::toMat4(m_rotation);
}

void Bone::UpdateWorldMatrix()
{
  UpdateLocalMatrix();
  auto mtxParent = mat4(1.0f);
  if (m_parent)
  {
    mtxParent = m_parent->GetWorldMatrix();
  }
  m_mtxWorld = mtxParent * m_mtxLocal;
}


void Bone::UpdateMatrices()
{
  UpdateWorldMatrix();
  for (auto c : m_children)
  {
    c->UpdateMatrices();
  }
}

void Model::Load(const char* filename, VulkanAppBase* app)
{
  ifstream infile(filename, std::ios::binary);
  loader::PMDFile loader(infile);
  auto device = app->GetDevice();

  auto vertexCount = loader.getVertexCount();
  auto indexCount = loader.getIndexCount();
  m_hostMemVertices.resize(vertexCount);
  for (uint32_t i = 0; i < vertexCount; ++i)
  {
    m_hostMemVertices[i] = convertTo(loader.getVertex(i));
  }
  std::vector<uint32_t> modelIndices(indexCount);
  for (uint32_t i = 0; i < indexCount; ++i)
  {
    auto& v = modelIndices[i];
    v = loader.getIndices()[i];
  }

  uint32_t bufferSizeIB = indexCount * sizeof(uint32_t);
  VkMemoryPropertyFlags stageMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  const auto deviceLocal = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  VkBufferUsageFlagBits stage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  auto stagingIB = app->CreateBuffer(bufferSizeIB, stage, stageMemProps);

  m_indexBuffer = app->CreateBuffer(bufferSizeIB,
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT  | VK_BUFFER_USAGE_TRANSFER_DST_BIT, deviceLocal );

  app->WriteToHostVisibleMemory(stagingIB.memory, bufferSizeIB, modelIndices.data());

  // Stageing => DeviceLocal へ転送.
  auto command = app->CreateCommandBuffer();
  VkBufferCopy copyRegion{};
  copyRegion.size = bufferSizeIB;
  vkCmdCopyBuffer(command, stagingIB.buffer, m_indexBuffer.buffer, 1, &copyRegion);
  app->FinishCommandBuffer(command);
  app->DestroyBuffer(stagingIB);

  const uint32_t imageCount = app->GetSwapchain()->GetImageCount();
  m_vertexBuffers.resize(imageCount);
  uint32_t bufferSizeVB = vertexCount * sizeof(PMDVertex);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    m_vertexBuffers[i] = app->CreateBuffer(bufferSizeVB, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, stageMemProps);
  }

  // マテリアル読み込み
  const uint32_t materialCount = loader.getMaterialCount();
  for (uint32_t i = 0; i < materialCount; ++i)
  {
    const auto& src = loader.getMaterial(i);
    Material::MaterialParameters materialParams{};
    materialParams.diffuse = vec4(src.getDiffuse(), src.getAlpha());
    materialParams.ambient = vec4(src.getAmbient(), 0.0f);
    materialParams.specular = vec4(src.getSpecular(), src.getShininess());
    materialParams.useTexture.x = 0;
    materialParams.edgeFlag.x = src.getEdgeFlag();

    auto textureFileName = src.getTexture();
    auto hasSphereMap = textureFileName.find('*');
    if (hasSphereMap != std::string::npos)
    {
      textureFileName = textureFileName.substr(0, hasSphereMap);
    }
    if (!textureFileName.empty())
    {
      materialParams.useTexture.x = 1;
    }
    Material material(materialParams);
    
    uint32_t bufferSize = uint32_t(sizeof(materialParams));
    auto uniformBuffers = app->CreateUniformBuffers(bufferSize, 1);
    material.SetUniformBuffer(uniformBuffers[0]);

    if (materialParams.useTexture.x)
    {
      int width, height;
      auto pImage = stbi_load(textureFileName.c_str(), &width, &height, nullptr, 4);
      auto texture = app->CreateTexture(width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
      uint32_t bufferSize = width * height * sizeof(uint32_t);
      auto bufferSrc = app->CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      app->WriteToHostVisibleMemory(bufferSrc.memory, bufferSize, pImage);

      VkBufferImageCopy region{};
      region.imageExtent = { uint32_t(width), uint32_t(height), 1 };
      region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      app->TransferStageBufferToImage(bufferSrc, texture, &region);
      stbi_image_free(pImage);
      app->DestroyBuffer(bufferSrc);

      material.SetTexture(texture);
    }

    material.Update(app);
    m_materials.emplace_back(material);
  }

  // 描画用メッシュ情報構築.
  uint32_t startIndexOffset = 0;
  for (uint32_t i = 0; i < materialCount; ++i) {
    const auto& src = loader.getMaterial(i);
    uint32_t indexCount = src.getNumberOfPolygons();

    m_meshes.emplace_back(Mesh{
      startIndexOffset, indexCount
      });
    startIndexOffset += indexCount;
  }
 
  // ボーン情報構築.
  uint32_t boneCount = loader.getBoneCount();
  m_bones.reserve(boneCount);
  for (uint32_t i = 0; i < boneCount; ++i)
  {
    const auto& boneSrc = loader.getBone(i);
    auto index = boneSrc.getParent();

    auto bone = new Bone(boneSrc.getName());
    auto translation = boneSrc.getPosition();
    if (index != 0xFFFFu)
    {
      const auto& parent = loader.getBone(boneSrc.getParent());
      translation = translation - parent.getPosition();
    }
    bone->SetTranslation(translation);
    bone->SetInitialTranslation(translation);

    // バインド逆行列をグローバル位置より求める.
    auto invBind = glm::inverse(translate(boneSrc.getPosition()));
    bone->SetInvBindMatrix(invBind);

    m_bones.push_back(bone);
  }
  for (uint32_t i = 0; i < boneCount; ++i)
  {
    const auto& boneSrc = loader.getBone(i);
    auto bone = m_bones[i];
    uint32_t index = boneSrc.getParent();
    if (index != 0xFFFFu)
    {
      bone->SetParent(m_bones[index]);
    }
  }
  UpdateMatrices();


  // 表情モーフ情報読み込み.
  {
    // 表情ベース.
    auto baseFace = loader.getFaceBase();
    auto vertexCount = baseFace.getVertexCount();
    auto indexCount = baseFace.getIndexCount();
    m_faceBaseInfo.verticesPos.resize(vertexCount);
    m_faceBaseInfo.indices.resize(indexCount);
    auto sizeVB = vertexCount * sizeof(glm::vec3);
    auto sizeIB = indexCount * sizeof(uint32_t);
    memcpy(m_faceBaseInfo.verticesPos.data(), baseFace.getFaceVertices(), sizeVB);
    memcpy(m_faceBaseInfo.indices.data(), baseFace.getFaceIndices(), sizeIB);

    // オフセット表情モーフ.
    auto faceCount = loader.getFaceCount()-1;
    m_faceOffsetInfo.resize(faceCount);
    for (uint32_t i = 0; i < faceCount; ++i) // 1から始めるのは 0 が base のため.
    {
      auto faceSrc = loader.getFace(i+1);
      auto& face = m_faceOffsetInfo[i];
      face.name = faceSrc.getName();

      indexCount = faceSrc.getIndexCount();
      vertexCount = faceSrc.getVertexCount();
      face.indices.resize(indexCount);
      face.verticesOffset.resize(vertexCount);
      sizeVB = vertexCount * sizeof(glm::vec3);
      sizeIB = indexCount * sizeof(uint32_t);
      memcpy(face.verticesOffset.data(), faceSrc.getFaceVertices(), sizeVB);
      memcpy(face.indices.data(), faceSrc.getFaceIndices(), sizeIB);
    }

    m_faceMorphWeights.resize(faceCount);
  }

  // IKボーン情報を読み込む.
  auto ikBoneCount = loader.getIkCount();
  m_boneIkList.resize(ikBoneCount);
  for (uint32_t i = 0; i < ikBoneCount; ++i)
  {
    const auto& ik = loader.getIk(i);
    auto& boneIk = m_boneIkList[i];
    auto targetBone = m_bones[ik.getTargetBoneId()];
    auto effectorBone = m_bones[ik.getBoneEff()];

    boneIk = PMDBoneIK(targetBone, effectorBone);
    boneIk.SetAngleLimit(ik.getAngleLimit());
    boneIk.SetIterationCount(ik.getIterations());

    auto chains = ik.getChains();
    std::vector<Bone*> ikChains;
    ikChains.reserve(chains.size());
    for (auto& id : chains)
    {
      ikChains.push_back(m_bones[id]);
    }
    boneIk.SetIkChains(ikChains);
  }
  

  uint32_t sizeVB = sizeof(PMDVertex) * vertexCount;
  app->WriteToHostVisibleMemory(m_vertexBuffers[0].memory, sizeVB, m_hostMemVertices.data());
  app->WriteToHostVisibleMemory(m_vertexBuffers[1].memory, sizeVB, m_hostMemVertices.data());
}

void Model::Prepare(VulkanAppBase* app)
{
  auto imageCount = app->GetSwapchain()->GetImageCount();

  PrepareDummyTexture(app);
  PreparePipelines(app);
  PrepareModelUniformBuffers(imageCount, app);
  PrepareDescriptorSets(app);
  PrepareCommandBuffers(imageCount, app);
}

void Model::Cleanup(VulkanAppBase* app)
{
  auto device = app->GetDevice();
  for (auto& command : m_commandBuffers)
  {
    app->FreeCommandBufferSecondary(uint32_t(command.size()), command.data());
  }
  for (auto& command : m_commandBuffersOutline)
  {
    app->FreeCommandBufferSecondary(uint32_t(command.size()), command.data());
  }
  for (auto& command : m_commandBuffersShadow)
  {
    app->FreeCommandBufferSecondary(uint32_t(command.size()), command.data());
  }

  for (auto& pipeline : m_pipelines)
  {
    vkDestroyPipeline(device, pipeline.second, nullptr);
  }
  for (auto& m : m_materials)
  {
    app->DestroyBuffer(m.GetUniformBuffer());
    if (m.HasTexture())
    {
      app->DestroyImage(m.GetTexture());
    }
  }
  for (auto& v : m_sceneParamUBO)
  {
    app->DestroyBuffer(v);
  }
  for (auto& v : m_boneUBO)
  {
    app->DestroyBuffer(v);
  }
  for (auto& v : m_vertexBuffers)
  {
    app->DestroyBuffer(v);
  }
  app->DestroyBuffer(m_indexBuffer);
  app->DestroyImage(m_dummyTexture);
  vkDestroySampler(device, m_sampler, nullptr);

  for (auto& b : m_bones)
  {
    delete b;
  }
  m_bones.clear();
}

int Model::GetFaceMorphIndex(const std::string& name) const
{
  int ret = -1;
  for (uint32_t i = 0; i < m_faceOffsetInfo.size(); ++i)
  {
    const auto& face = m_faceOffsetInfo[i];
    if (face.name == name)
    {
      ret = i;
      break;
    }
  }
  return ret;
}

void Model::SetFaceMorphWeight(int index, float weight)
{
  if (index < 0)
    return;
  m_faceMorphWeights[index] = weight;
}

void Model::PrepareModelUniformBuffers(uint32_t count, VulkanAppBase* app)
{
  auto sceneParamSize = uint32_t(sizeof(SceneParameter));
  auto boneParamSize = uint32_t(sizeof(BoneParameter));
  m_sceneParamUBO = app->CreateUniformBuffers(sceneParamSize, count);
  m_boneUBO = app->CreateUniformBuffers(boneParamSize, count);
}

Model::SecondaryCommandBuffers Model::GetCommandBuffers(uint32_t index)
{
  return m_commandBuffers[index];
}
Model::SecondaryCommandBuffers Model::GetCommandBuffersOutline(uint32_t index)
{
  return m_commandBuffersOutline[index];
}
Model::SecondaryCommandBuffers Model::GetCommandBuffersShadow(uint32_t index)
{
  return m_commandBuffersShadow[index];
}

void Model::PreparePipelines(VulkanAppBase* app)
{
  auto device = app->GetDevice();
  array<VkVertexInputAttributeDescription, 6> inputAttribs{ {
    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PMDVertex, position)},
    { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PMDVertex, normal)},
    { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PMDVertex, uv)},
    { 3, 0, VK_FORMAT_R32G32_UINT, offsetof(PMDVertex, boneIndices)},
    { 4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PMDVertex, boneWeights)},
    { 5, 0, VK_FORMAT_R32_UINT, offsetof(PMDVertex, edgeFlag)},
  } };
  VkVertexInputBindingDescription vibDesc{
    0, sizeof(PMDVertex), VK_VERTEX_INPUT_RATE_VERTEX
  };
  VkPipelineVertexInputStateCreateInfo pipelineVIS{
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    nullptr, 0,
    1, &vibDesc,
    uint32_t(inputAttribs.size()), inputAttribs.data()
  };

  auto pipelineLayout = app->GetPipelineLayout("model");
  auto defaultRS = book_util::GetDefaultRasterizerState();
  auto outlineRS = book_util::GetDefaultRasterizerState(VK_CULL_MODE_FRONT_BIT);

  auto renderPass = app->GetRenderPass("default");
  using ShaderStageInfo = std::vector<VkPipelineShaderStageCreateInfo>;

  ShaderStageInfo shaderStages{
    book_util::LoadShader(device, "modelVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(device, "modelFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
  };
  ShaderStageInfo shaderStagesOutline{
    book_util::LoadShader(device, "modelOutlineVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(device, "modelOutlineFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
  };
  ShaderStageInfo shaderStagesShadow{
    book_util::LoadShader(device, "modelShadowVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(device, "modelShadowFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
  };

  auto extent = app->GetSwapchain()->GetSurfaceExtent();
  VkViewport viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));
  VkRect2D scissor = app->GetSwapchainRenderArea();
  VkPipelineViewportStateCreateInfo viewportCI{
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    nullptr, 0,
    1, &viewport,
    1, &scissor,
  };

  auto opaqueState = book_util::GetOpaqueColorBlendAttachmentState();

  VkPipelineColorBlendStateCreateInfo colorBlendStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    nullptr, 0,
    VK_FALSE, VK_LOGIC_OP_CLEAR, // logicOpEnable
    1, &opaqueState,
    { 0.0f, 0.0f, 0.0f,0.0f }
  };

  auto ia = book_util::GetInputAssembly();
  auto nomultisample = book_util::GetNoMultisampleState();
  auto dss = book_util::GetDefaultDepthStencilState();

  VkGraphicsPipelineCreateInfo pipelineCI{
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    nullptr, 0,
    uint32_t(shaderStages.size()), shaderStages.data(),
    &pipelineVIS, &ia, nullptr,
    &viewportCI,  &defaultRS, &nomultisample,
    &dss, &colorBlendStateCI,
    nullptr,
    pipelineLayout,
    renderPass,
    0,
    VK_NULL_HANDLE, 0
  };

  VkResult result;
  VkPipeline pipeline;
  result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");
  m_pipelines["normalDraw"] = pipeline;

  pipelineCI.pStages = shaderStagesOutline.data();
  pipelineCI.pRasterizationState = &outlineRS;
  result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");
  m_pipelines["outlineDraw"] = pipeline;


  renderPass = app->GetRenderPass("shadow");
  viewport = { 0, 0, 1024, 1024, 0, 1.0f };
  scissor = { { 0 }, { 1024, 1024 } };
  pipelineCI.renderPass = renderPass;
  pipelineCI.pStages = shaderStagesShadow.data();
  pipelineCI.pRasterizationState = &defaultRS;
  result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");
  m_pipelines["shadow"] = pipeline;
 
  book_util::DestroyShaderModules(device, shaderStages);
  book_util::DestroyShaderModules(device, shaderStagesOutline);
  book_util::DestroyShaderModules(device, shaderStagesShadow);
}

void Model::PrepareDescriptorSets(VulkanAppBase* app)
{
  auto device = app->GetDevice();
  auto imageCount = app->GetSwapchain()->GetImageCount();
  for (auto& material : m_materials)
  {
    auto layout = app->GetDescriptorSetLayout("model");
    std::vector<VkDescriptorSet> descriptorSets(imageCount);

    std::vector<VkDescriptorSetLayout> layouts;
    for (uint32_t i = 0; i < imageCount; ++i) { layouts.push_back(layout); }
    VkDescriptorSetAllocateInfo descriptorSetAI{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      nullptr, app->GetDescriptorPool(),
      uint32_t(layouts.size()), layouts.data()
    };
    vkAllocateDescriptorSets(device, &descriptorSetAI, descriptorSets.data());

    VkDescriptorBufferInfo sceneParamUBO{
      m_sceneParamUBO[0].buffer, 0, VK_WHOLE_SIZE
    };
    VkDescriptorBufferInfo boneUBO{
      m_boneUBO[0].buffer, 0, VK_WHOLE_SIZE
    };
    VkDescriptorBufferInfo materialUBO{
      material.GetUniformBuffer().buffer, 0, VK_WHOLE_SIZE
    };
    VkDescriptorImageInfo diffuseTexture{
      m_sampler,
      m_dummyTexture.view,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkDescriptorImageInfo shadowTexture{
      m_sampler, 
      m_shadowMap.view,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    for (uint32_t i = 0; i < imageCount; ++i)
    {
      std::array<VkWriteDescriptorSet, 5> writeDescriptors{
        book_util::CreateWriteDescriptorSet(descriptorSets[i], 0, &sceneParamUBO),
        book_util::CreateWriteDescriptorSet(descriptorSets[i], 1, &boneUBO),
        book_util::CreateWriteDescriptorSet(descriptorSets[i], 2, &materialUBO),
        book_util::CreateWriteDescriptorSet(descriptorSets[i], 3, &diffuseTexture),
        book_util::CreateWriteDescriptorSet(descriptorSets[i], 4, &shadowTexture),
      };

      if (material.HasTexture())
      {
        diffuseTexture.imageView = material.GetTexture().view;
      }

      vkUpdateDescriptorSets(device, uint32_t(writeDescriptors.size()), writeDescriptors.data(), 0, nullptr);
    }
    material.SetDescriptorSet(descriptorSets);
  }
}

void Model::UpdateMatrices()
{
  // ボーンの行列を更新する.
  for (auto& v : m_bones)
  {
    if (v->GetParent())
    {
      continue;
    }
    v->UpdateMatrices();
  }
}

void Model::Update(uint32_t imageIndex, VulkanAppBase* app)
{
  app->WriteToHostVisibleMemory(m_sceneParamUBO[imageIndex].memory, sizeof(SceneParameter), &m_sceneParams);

  // ボーン行列をユニフォームバッファへ書き込む.
  for (uint32_t i = 0; i < m_bones.size(); ++i)
  {
    auto bone = m_bones[i];
    auto mtx = bone->GetWorldMatrix() * bone->GetInvBindMatrix();
    m_boneMatrices.bone[i] = mtx;
  }
  app->WriteToHostVisibleMemory(m_boneUBO[imageIndex].memory, sizeof(BoneParameter), &m_boneMatrices);


  // 頂点バッファの更新.
  {
    auto vertexCount = m_faceBaseInfo.verticesPos.size();
    for (uint32_t i = 0; i < vertexCount; ++i)
    {
      auto offsetIndex = m_faceBaseInfo.indices[i];
      m_hostMemVertices[offsetIndex].position = m_faceBaseInfo.verticesPos[i];
    }

    // ウェイトに応じて頂点を変更.
    for (uint32_t faceIndex = 0; faceIndex < m_faceOffsetInfo.size(); ++faceIndex)
    {
      const auto& face = m_faceOffsetInfo[faceIndex];
      float w = m_faceMorphWeights[faceIndex];

      for(uint32_t i=0;i<face.indices.size();++i)
      {
        auto baseVertexIndex = face.indices[i];
        auto displacement = face.verticesOffset[i];

        auto offsetIndex = m_faceBaseInfo.indices[baseVertexIndex];
        m_hostMemVertices[offsetIndex].position += displacement * w;
      }
    }

    auto bufferSize = sizeof(PMDVertex) * m_hostMemVertices.size();
    app->WriteToHostVisibleMemory(
      m_vertexBuffers[imageIndex].memory,
      uint32_t(bufferSize),
      m_hostMemVertices.data());
  }
}

void Model::PrepareDummyTexture(VulkanAppBase* app)
{
  VkResult result;
  VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  m_dummyTexture = app->CreateTexture(1, 1, VK_FORMAT_R8G8B8A8_UNORM, usage);

  VkMemoryPropertyFlags bufferMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  auto bufferSrc = app->CreateBuffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, bufferMemProps);
  uint32_t imagePixel = 0xffffffffu;
  app->WriteToHostVisibleMemory(bufferSrc.memory, 4, &imagePixel);

  VkBufferImageCopy region{};
  region.imageExtent = { 1,1,1 };
  region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };

  app->TransferStageBufferToImage(bufferSrc, m_dummyTexture, &region);

  VkSamplerCreateInfo samplerCI{
    VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    nullptr, 0, VK_FILTER_LINEAR,VK_FILTER_LINEAR,
    VK_SAMPLER_MIPMAP_MODE_LINEAR,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    0.0f, VK_FALSE, 1.0f, VK_FALSE,
    VK_COMPARE_OP_NEVER, 0.0f, 0.0f,
    VK_BORDER_COLOR_INT_OPAQUE_WHITE, VK_FALSE,
  };
  result = vkCreateSampler(app->GetDevice(), &samplerCI, nullptr, &m_sampler);
  ThrowIfFailed(result, "vkCreateSampler Failed.");

  app->DestroyBuffer(bufferSrc);
}

void Model::PrepareCommandBuffers(uint32_t count, VulkanAppBase* app)
{
  auto renderPass = app->GetRenderPass("default");
  auto materialCount = uint32_t(m_materials.size());
 
  VkCommandBufferInheritanceInfo inheritInfo{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
    nullptr, renderPass,
    0, VK_NULL_HANDLE, VK_FALSE, 0, 0
  };
  VkCommandBufferBeginInfo beginInfo{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    nullptr,
    VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
    &inheritInfo
  };

  // 通常描画のコマンド構築.
  m_commandBuffers.resize(count);
  for (uint32_t index = 0; index < count; ++index)
  {
    auto& buffers = m_commandBuffers[index];
    buffers.resize(materialCount);
    app->AllocateCommandBufferSecondary(materialCount, buffers.data());

    auto vertexBuffer = m_vertexBuffers[index];
    VkPipeline usePipeline = m_pipelines["normalDraw"];
    for (uint32_t i = 0; i < materialCount; ++i)
    {
      auto descriptorSet = m_materials[i].GetDescriptorSet(index);
      auto pipelineLayout = app->GetPipelineLayout("model");
      auto mesh = m_meshes[i];
      auto command = buffers[i];

      vkBeginCommandBuffer(command, &beginInfo);
      VkDeviceSize offsets[] = { 0 };
      vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, usePipeline);
      vkCmdBindIndexBuffer(command, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer.buffer, offsets);
      vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
      vkCmdDrawIndexed(command, mesh.indexCount, 1, mesh.startIndexOffset, 0, 0);
      vkEndCommandBuffer(command);
    }
  }

  // 輪郭線描画用のコマンド構築.
  m_commandBuffersOutline.resize(count);
  for (uint32_t index = 0; index < count; ++index)
  {
    auto& buffers = m_commandBuffersOutline[index];
    buffers.resize(materialCount);
    app->AllocateCommandBufferSecondary(materialCount, buffers.data());

    auto vertexBuffer = m_vertexBuffers[index];
    VkPipeline usePipeline = m_pipelines["outlineDraw"];
    uint32_t commandIndex = 0;
    for (uint32_t i = 0; i < materialCount; ++i)
    {
      auto descriptorSet = m_materials[i].GetDescriptorSet(index);
      auto pipelineLayout = app->GetPipelineLayout("model");
      auto mesh = m_meshes[i];
      auto material = m_materials[i];
      if (material.GetEdgeFlag() == 0)
      {
        continue;
      }
      auto command = buffers[commandIndex++];

      vkBeginCommandBuffer(command, &beginInfo);
      VkDeviceSize offsets[] = { 0 };
      vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, usePipeline);
      vkCmdBindIndexBuffer(command, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer.buffer, offsets);
      vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
      vkCmdDrawIndexed(command, mesh.indexCount, 1, mesh.startIndexOffset, 0, 0);
      vkEndCommandBuffer(command);
    }
    buffers.resize(commandIndex);
  }

  // シャドウパス用のコマンド構築.
  m_commandBuffersShadow.resize(count);
  inheritInfo.renderPass = app->GetRenderPass("shadow");
  for (uint32_t index = 0; index < count; ++index)
  {
    auto& buffers = m_commandBuffersShadow[index];
    buffers.resize(materialCount);
    app->AllocateCommandBufferSecondary(materialCount, buffers.data());

    auto vertexBuffer = m_vertexBuffers[index];
    VkPipeline usePipeline = m_pipelines["shadow"];
    for (uint32_t i = 0; i < materialCount; ++i)
    {
      auto descriptorSet = m_materials[i].GetDescriptorSet(index);
      auto pipelineLayout = app->GetPipelineLayout("model");
      auto mesh = m_meshes[i];
      auto command = buffers[i];

      vkBeginCommandBuffer(command, &beginInfo);
      VkDeviceSize offsets[] = { 0 };
      vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, usePipeline);
      vkCmdBindIndexBuffer(command, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdBindVertexBuffers(command, 0, 1, &vertexBuffer.buffer, offsets);
      vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
      vkCmdDrawIndexed(command, mesh.indexCount, 1, mesh.startIndexOffset, 0, 0);
      vkEndCommandBuffer(command);
    }
  }
}