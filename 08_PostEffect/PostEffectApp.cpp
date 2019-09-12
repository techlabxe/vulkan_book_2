#include "PostEffectApp.h"
#include "TeapotModel.h"
#include "VulkanBookUtil.h"

#include <random>
#include <array>

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace std;
using namespace glm;

static glm::vec4 colorSet[] = {
  glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
  glm::vec4(1.0f, 0.65f, 1.0f, 1.0f),
  glm::vec4(0.1f, 0.5f, 1.0f, 1.0f),
  glm::vec4(0.6f, 1.0f, 0.8f, 1.0f),
};


PostEffectApp::PostEffectApp()
  : m_frameCount(0), m_effectType(EFFECT_TYPE_MOSAIC)
{
  m_effectParameter.mosaicBlockSize = 10;
  m_effectParameter.frameCount = m_frameCount;
  m_effectParameter.ripple = 0.75f;
  m_effectParameter.speed = 1.5f;
  m_effectParameter.distortion = 0.03f;
  m_effectParameter.brightness = 0.25f;
}

void PostEffectApp::Prepare()
{
  // レンダーパスを生成.
  auto renderPassMain = book_util::CreateRenderPass(
    m_device,
    m_swapchain->GetSurfaceFormat().format,
    VK_FORMAT_D32_SFLOAT);
  auto renderPassRenderTarget = book_util::CreateRenderPassToRenderTarget(
    m_device,
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_D32_SFLOAT);
  RegisterRenderPass("main", renderPassMain);
  RegisterRenderPass("render_target", renderPassRenderTarget);
 
  // デプスバッファを準備する.
  auto extent = m_swapchain->GetSurfaceExtent();
  m_depthBuffer = CreateTexture(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

  // フレームバッファの準備.
  PrepareFramebuffers();
  auto imageCount = m_swapchain->GetImageCount();

  VkResult result;
  m_commandFences.resize(imageCount);
  VkFenceCreateInfo fenceCI{
    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    nullptr,
    VK_FENCE_CREATE_SIGNALED_BIT
  };
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    result = vkCreateFence(m_device, &fenceCI, nullptr, &m_commandFences[i]);
    ThrowIfFailed(result, "vkCreateFence Failed.");
  }

  m_commandBuffers.resize(imageCount);
  VkCommandBufferAllocateInfo allocInfo{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    nullptr,
    m_commandPool,
    VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    imageCount
  };
  result = vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data());
  ThrowIfFailed(result, "vkCreateFence Failed.");

  PrepareRenderTexture();

  PrepareTeapot();
  PreparePlane();
  PrepareInstanceData();

  CreatePipelineTeapot();
  CreatePipelinePlane();
  
  PrepareDescriptors();
  PreparePostEffectDescriptors();

  // ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui_ImplGlfw_InitForVulkan(m_window, true);

  ImGui_ImplVulkan_InitInfo info{};
  info.Instance = m_vkInstance;
  info.PhysicalDevice = m_physicalDevice;
  info.Device = m_device;
  info.QueueFamily = m_gfxQueueIndex;
  info.Queue = m_deviceQueue;
  info.DescriptorPool = m_descriptorPool;
  info.MinImageCount = imageCount;
  info.ImageCount = imageCount;
  ImGui_ImplVulkan_Init(&info, renderPassMain);

  auto command = CreateCommandBuffer();
  ImGui_ImplVulkan_CreateFontsTexture(command);
  FinishCommandBuffer(command);
  vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
}

void PostEffectApp::Cleanup()
{
  for (auto& data : m_instanceUniforms)
  {
    DestroyBuffer(data);
  }
  for (auto& data : m_effectUB)
  {
    DestroyBuffer(data);
  }

  DestroyModelData(m_teapot);
  
  DestroyImage(m_colorTarget);
  DestroyImage(m_depthTarget);
  
  vkDestroyFramebuffer(m_device, m_renderTextureFB, nullptr);
  vkDestroySampler(m_device, m_sampler, nullptr);

  for (auto& pipeline : { m_mosaicPipeline, m_waterPipeline })
  {
    vkDestroyPipeline(m_device, pipeline, nullptr);
  }

  for (auto& layout : { m_layoutTeapot, m_layoutEffect })
  {
    vkDestroyPipelineLayout(m_device, layout.pipeline, nullptr);
    vkDestroyDescriptorSetLayout(m_device, layout.descriptorSet, nullptr);
  }

  DestroyImage(m_depthBuffer);
  auto count = uint32_t(m_framebuffers.size());
  DestroyFramebuffers(count, m_framebuffers.data());

  for (auto f : m_commandFences)
  {
    vkDestroyFence(m_device, f, nullptr);
  }
  vkFreeCommandBuffers(m_device, m_commandPool, uint32_t(m_commandBuffers.size()), m_commandBuffers.data());
  m_commandBuffers.clear();
  m_commandFences.clear();

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void PostEffectApp::Render()
{
  if (m_isMinimizedWindow)
  {
    MsgLoopMinimizedWindow();
  }
  uint32_t imageIndex = 0;
  auto result = m_swapchain->AcquireNextImage(&imageIndex, m_presentCompletedSem);
  if (result == VK_ERROR_OUT_OF_DATE_KHR)
  {
    return;
  }
  m_frameIndex = imageIndex;
  auto command = m_commandBuffers[m_frameIndex];
  auto fence = m_commandFences[m_frameIndex];
  vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
  vkResetFences(m_device, 1, &fence);

  VkCommandBufferBeginInfo commandBI{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    nullptr, 0, nullptr
  };
  vkBeginCommandBuffer(command, &commandBI);

  RenderToTexture(command);

  VkImageMemoryBarrier imageBarrier{
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    nullptr,
    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // srcAccessMask
    VK_ACCESS_SHADER_READ_BIT, // dstAccessMask
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    VK_QUEUE_FAMILY_IGNORED,
    VK_QUEUE_FAMILY_IGNORED,
    m_colorTarget.image,
    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
  };
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    VK_DEPENDENCY_BY_REGION_BIT,
    0, nullptr, // memoryBarrier
    0, nullptr, // bufferMemoryBarrier
    1, &imageBarrier
  );

  RenderToMain(command);

  VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submitInfo{
    VK_STRUCTURE_TYPE_SUBMIT_INFO,
    nullptr,
    1, &m_presentCompletedSem, // WaitSemaphore
    &waitStageMask, // DstStageMask
    1, &command, // CommandBuffer
    1, &m_renderCompletedSem, // SignalSemaphore
  };
  vkEndCommandBuffer(command);
  vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);

  m_swapchain->QueuePresent(m_deviceQueue, m_frameIndex, m_renderCompletedSem);
  m_frameCount++;
}

void PostEffectApp::PrepareFramebuffers()
{
  auto imageCount = m_swapchain->GetImageCount();
  auto extent = m_swapchain->GetSurfaceExtent();
  m_framebuffers.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    vector<VkImageView> views;
    views.push_back(m_swapchain->GetImageView(i));
    views.push_back(m_depthBuffer.view);

    auto renderPass = GetRenderPass("main");
    m_framebuffers[i] = CreateFramebuffer(
      renderPass,
      extent.width, extent.height,
      uint32_t(views.size()), views.data()
    );
  }
}

bool PostEffectApp::OnSizeChanged(uint32_t width, uint32_t height)
{
  auto result = VulkanAppBase::OnSizeChanged(width, height);
  if (result)
  {

    DestroyImage(m_depthBuffer);
    DestroyFramebuffers(uint32_t(m_framebuffers.size()), m_framebuffers.data());

    // デプスバッファを再生成.
    auto extent = m_swapchain->GetSurfaceExtent();
    m_depthBuffer = CreateTexture(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

    // フレームバッファを準備.
    PrepareFramebuffers();

    // ポストエフェクト用リソースの削除＆再生成.
    DestroyImage(m_colorTarget);
    DestroyImage(m_depthTarget);
    DestroyFramebuffers(1, &m_renderTextureFB);

    PrepareRenderTexture();

    // ディスクリプタを更新.
    PreparePostEffectDescriptors();
  }
  return result;
}

void PostEffectApp::PrepareTeapot()
{
  VkMemoryPropertyFlags srcMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  VkMemoryPropertyFlags dstMemoryProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  auto bufferSizeVB = uint32_t(sizeof(TeapotModel::TeapotVerticesPN));
  auto bufferSizeIB = uint32_t(sizeof(TeapotModel::TeapotIndices));
  VkBufferUsageFlags usageVB = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  VkBufferUsageFlags usageIB = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  auto stageVB = CreateBuffer(bufferSizeVB, usageVB | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, srcMemoryProps);
  auto targetVB = CreateBuffer(bufferSizeVB, usageVB | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dstMemoryProps);
  auto stageIB = CreateBuffer(bufferSizeIB, usageIB | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, srcMemoryProps);
  auto targetIB = CreateBuffer(bufferSizeIB, usageIB | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dstMemoryProps);

  void* p;
  vkMapMemory(m_device, stageVB.memory, 0, VK_WHOLE_SIZE, 0, &p);
  memcpy(p, TeapotModel::TeapotVerticesPN, bufferSizeVB);
  vkUnmapMemory(m_device, stageVB.memory);
  vkMapMemory(m_device, stageIB.memory, 0, VK_WHOLE_SIZE, 0, &p);
  memcpy(p, TeapotModel::TeapotIndices, bufferSizeIB);
  vkUnmapMemory(m_device, stageIB.memory);

  VkCommandBuffer command = CreateCommandBuffer();
  VkBufferCopy copyRegionVB{}, copyRegionIB{};
  copyRegionVB.size = bufferSizeVB;
  copyRegionIB.size = bufferSizeIB;
  vkCmdCopyBuffer(command, stageVB.buffer, targetVB.buffer, 1, &copyRegionVB);
  vkCmdCopyBuffer(command, stageIB.buffer, targetIB.buffer, 1, &copyRegionIB);
  FinishCommandBuffer(command);
  
  vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
  m_teapot.vertexBuffer= targetVB;
  m_teapot.indexBuffer = targetIB;
  m_teapot.indexCount = _countof(TeapotModel::TeapotIndices);
  m_teapot.vertexCount = _countof(TeapotModel::TeapotVerticesPN);
  DestroyBuffer(stageVB);
  DestroyBuffer(stageIB);

  // 定数バッファの準備.
  uint32_t imageCount = m_swapchain->GetImageCount();
  auto bufferSize = uint32_t(sizeof(ShaderParameters));
  m_teapot.sceneUB = CreateUniformBuffers(bufferSize, imageCount);
  m_teapot.indexCount = _countof(TeapotModel::TeapotIndices);
  m_teapot.vertexCount = _countof(TeapotModel::TeapotVerticesPN);

  // teapot 用のディスクリプタセット/レイアウトを準備.
  LayoutInfo layout{};
  VkDescriptorSetLayoutBinding descSetLayoutBindings[] = {
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },  // SceneParameters
    { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },  // InstanceParameters
  };
  VkDescriptorSetLayoutCreateInfo descSetLayoutCI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    nullptr, 0,
    _countof(descSetLayoutBindings), descSetLayoutBindings,
  };
  auto result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &layout.descriptorSet);
  ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");

  // パイプラインレイアウトを準備.
  VkPipelineLayoutCreateInfo pipelineLayoutCI{
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    nullptr, 0,
    1, &layout.descriptorSet,
    0, nullptr
  };
  result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &layout.pipeline);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");

  m_layoutTeapot = layout;
}

void PostEffectApp::PreparePlane()
{
  // テクスチャを貼る板用のディスクリプタセット/レイアウトを準備.
  VkDescriptorSetLayoutBinding descSetLayoutBindings[] = {
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
    { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }
  };
  VkDescriptorSetLayoutCreateInfo descSetLayoutCI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    nullptr, 0,
    _countof(descSetLayoutBindings), descSetLayoutBindings,
  };
  auto result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &m_layoutEffect.descriptorSet);
  ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");

  VkDescriptorSetAllocateInfo descriptorSetAI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    nullptr, m_descriptorPool,
    1, &m_layoutEffect.descriptorSet
  };

  auto imageCount = m_swapchain->GetImageCount();
  m_effectDescriptorSet.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    result = vkAllocateDescriptorSets(
      m_device, &descriptorSetAI, &m_effectDescriptorSet[i]);
    ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");
  }

  VkSamplerCreateInfo samplerCI{
    VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    nullptr, 0,
    VK_FILTER_LINEAR,
    VK_FILTER_LINEAR,
    VK_SAMPLER_MIPMAP_MODE_LINEAR,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    0.0f, 
    VK_FALSE,
    1.0f,
    VK_FALSE,
    VK_COMPARE_OP_NEVER,
    0.0f,
    0.0f,
    VK_BORDER_COLOR_INT_OPAQUE_WHITE,
    VK_FALSE,
  };
  result = vkCreateSampler(m_device, &samplerCI, nullptr, &m_sampler);
  ThrowIfFailed(result, "vkCreateSampler Failed.");

  // エフェクト用定数バッファ確保.
  auto bufferSize = uint32_t(sizeof(EffectParameters));
  m_effectUB = CreateUniformBuffers(bufferSize, imageCount);

  // パイプラインレイアウトを準備.
  VkPipelineLayoutCreateInfo pipelineLayoutCI{
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    nullptr, 0,
    1, &m_layoutEffect.descriptorSet,
    0, nullptr
  };
  result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &m_layoutEffect.pipeline);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
}

void PostEffectApp::PrepareInstanceData()
{
  VkMemoryPropertyFlags memoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

  // インスタンシング用のユニフォームバッファを準備
  auto bufferSize = uint32_t(sizeof(InstanceData)) * InstanceCount;
  m_instanceUniforms.resize(m_swapchain->GetImageCount());
  for (auto& ubo : m_instanceUniforms)
  {
    ubo = CreateBuffer(bufferSize, usage, memoryProps);
  }

  std::random_device rnd;
  std::vector<InstanceData> data(InstanceCount);
  for (uint32_t i = 0; i < InstanceCount; ++i)
  {
    const auto axisX = vec3(1.0f, 0.0f, 0.0f);
    const auto axisZ = vec3(0.0f, 0.0f, 1.0f);
    float k = float(rnd() % 360);
    float x = (i % 6) * 3.0f;
    float z = (i / 6) * -3.0f;

    mat4 mat(1.0f);
    mat = translate(mat, vec3(x, 0.0f, z));
    mat = rotate(mat, k, axisX);
    mat = rotate(mat, k, axisZ);

    data[i].world = mat;
    data[i].color = colorSet[i % _countof(colorSet)];
  }

  for (auto& ubo : m_instanceUniforms)
  {
    void* p;
    vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
    memcpy(p, data.data(), bufferSize);
    vkUnmapMemory(m_device, ubo.memory);
  }
}

void PostEffectApp::PrepareDescriptors()
{
  auto imageCount = m_swapchain->GetImageCount();
  VkResult result;

  // teapot 用ディスクリプタ準備.
  VkDescriptorSetAllocateInfo descriptorSetAI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    nullptr, m_descriptorPool,
    1, &m_layoutTeapot.descriptorSet
  };

  m_teapot.descriptorSet.reserve(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkDescriptorSet descriptorSet;
    result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &descriptorSet);
    ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");
    m_teapot.descriptorSet.push_back(descriptorSet);
  }

  // (teapot用) ディスクリプタを書き込む.
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkDescriptorBufferInfo uboInfo{
      m_teapot.sceneUB[i].buffer,
      0, VK_WHOLE_SIZE
    };
    VkDescriptorBufferInfo instanceInfo{
      m_instanceUniforms[i].buffer,
      0, VK_WHOLE_SIZE
    };

    VkWriteDescriptorSet writes[] = {
      book_util::PrepareWriteDescriptorSet(
        m_teapot.descriptorSet[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
      book_util::PrepareWriteDescriptorSet(
        m_teapot.descriptorSet[i], 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
    };
    writes[0].pBufferInfo = &uboInfo;
    writes[1].pBufferInfo = &instanceInfo;
    vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
  }
}

void PostEffectApp::PreparePostEffectDescriptors()
{
  // (effect用) ディスクリプタを書き込む.
  auto imageCount = m_swapchain->GetImageCount();

  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkDescriptorBufferInfo effectUbo{
      m_effectUB[i].buffer,
      0, VK_WHOLE_SIZE
    };

    auto descSetSceneUB = book_util::PrepareWriteDescriptorSet(
      m_effectDescriptorSet[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    );
    descSetSceneUB.pBufferInfo = &effectUbo;
    vkUpdateDescriptorSets(m_device, 1, &descSetSceneUB, 0, nullptr);

    VkDescriptorImageInfo texInfo{
      m_sampler,
      m_colorTarget.view,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    auto descSetTexture = book_util::PrepareWriteDescriptorSet(
      m_effectDescriptorSet[i], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    );
    descSetTexture.pImageInfo = &texInfo;
    vkUpdateDescriptorSets(m_device, 1, &descSetTexture, 0, nullptr);
  }
}

void PostEffectApp::CreatePipelineTeapot()
{
  // Teapot 用 Pipeline
  auto stride = uint32_t(sizeof(TeapotModel::Vertex));
  VkVertexInputBindingDescription vibDesc{
      0, // binding
      stride,
      VK_VERTEX_INPUT_RATE_VERTEX
  };

  array<VkVertexInputAttributeDescription, 2> inputAttribs{
    {
      { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TeapotModel::Vertex, Position) },
      { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TeapotModel::Vertex, Normal) },
    }
  };
  VkPipelineVertexInputStateCreateInfo pipelineVisCI{
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    nullptr, 0,
    1, &vibDesc,
    uint32_t(inputAttribs.size()), inputAttribs.data(),
  };

  auto colorBlendAttachmentState = book_util::GetOpaqueColorBlendAttachmentState();
  VkPipelineColorBlendStateCreateInfo colorBlendStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    nullptr, 0,
    VK_FALSE, VK_LOGIC_OP_CLEAR, // logicOpEnable
    1, &colorBlendAttachmentState,
    { 0.0f, 0.0f, 0.0f,0.0f }
  };
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI{
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_FALSE,
  };
  VkPipelineMultisampleStateCreateInfo multisampleCI{
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    nullptr, 0,
    VK_SAMPLE_COUNT_1_BIT,
    VK_FALSE, // sampleShadingEnable
    0.0f, nullptr,
    VK_FALSE, VK_FALSE,
  };

  auto extent = m_swapchain->GetSurfaceExtent();
  VkViewport viewport{
    0, 0, float(extent.width), float(extent.height), 0.0f, 1.0f
  };
  VkRect2D scissor{
    { 0, 0}, { 0, 0 }
  };
  VkPipelineViewportStateCreateInfo viewportCI{
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    nullptr, 0,
    1, &viewport,
    1, &scissor,
  };

  // シェーダーのロード.
  std::vector<VkPipelineShaderStageCreateInfo> shaderStages
  {
    book_util::LoadShader(m_device, "modelVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "modelFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
  };

  std::vector<VkDynamicState> dynamicStates{
    VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT
  };
  VkPipelineDynamicStateCreateInfo pipelineDynamicStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0,
    uint32_t(dynamicStates.size()), dynamicStates.data()
  };

  auto dsState = book_util::GetDefaultDepthStencilState();
  auto rasterizerState = book_util::GetDefaultRasterizerState();

  auto renderPass = GetRenderPass("render_target");
  VkResult result;
  // パイプライン構築.
  VkGraphicsPipelineCreateInfo pipelineCI{
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    nullptr, 0,
    uint32_t(shaderStages.size()), shaderStages.data(),
    &pipelineVisCI, &inputAssemblyCI,
    nullptr, // Tessellation
    &viewportCI, // ViewportState
    &rasterizerState,
    &multisampleCI,
    &dsState,
    &colorBlendStateCI,
    &pipelineDynamicStateCI,
    m_layoutTeapot.pipeline,
    renderPass,
    0, // subpass
    VK_NULL_HANDLE, 0, // basePipeline
  };
  result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_teapot.pipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipeline Failed.");

  book_util::DestroyShaderModules(m_device, shaderStages);
}

void PostEffectApp::CreatePipelinePlane()
{
  // Plane 用 Pipeline
  auto stride = uint32_t(sizeof(VertexPT));
  VkVertexInputBindingDescription vibDesc{
      0, // binding
      stride,
      VK_VERTEX_INPUT_RATE_VERTEX
  };

  array<VkVertexInputAttributeDescription, 2> inputAttribs{
    {
      { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexPT, position) },
      { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexPT, uv) },
    }
  };
  VkPipelineVertexInputStateCreateInfo pipelineVisCI{
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    nullptr, 0,
    1, &vibDesc,
    uint32_t(inputAttribs.size()), inputAttribs.data(),
  };

  auto colorBlendAttachmentState = book_util::GetOpaqueColorBlendAttachmentState();
  VkPipelineColorBlendStateCreateInfo colorBlendStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    nullptr, 0,
    VK_FALSE, VK_LOGIC_OP_CLEAR, // logicOpEnable
    1, &colorBlendAttachmentState,
    { 0.0f, 0.0f, 0.0f,0.0f }
  };
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI{
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    VK_FALSE,
  };
  VkPipelineMultisampleStateCreateInfo multisampleCI{
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    nullptr, 0,
    VK_SAMPLE_COUNT_1_BIT,
    VK_FALSE, // sampleShadingEnable
    0.0f, nullptr,
    VK_FALSE, VK_FALSE,
  };

  auto extent = m_swapchain->GetSurfaceExtent();
  VkViewport viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));
  VkRect2D scissor{
    { 0, 0}, {0,0}
  };
  VkPipelineViewportStateCreateInfo viewportCI{
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    nullptr, 0,
    1, &viewport,
    1, &scissor,
  };

  // シェーダーのロード.
  std::vector<VkPipelineShaderStageCreateInfo> shaderStagesForMosaic
  {
    book_util::LoadShader(m_device, "quadVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "mosaicFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
  };
  std::vector<VkPipelineShaderStageCreateInfo> shaderStagesForWater
  {
    book_util::LoadShader(m_device, "quadVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "waterFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
  };

  std::vector<VkDynamicState> dynamicStates{
    VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT
  };
  VkPipelineDynamicStateCreateInfo pipelineDynamicStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0,
    uint32_t(dynamicStates.size()), dynamicStates.data()
  };

  auto rasterizerState = book_util::GetDefaultRasterizerState();
  auto dsState = book_util::GetDefaultDepthStencilState();

  auto renderPass = GetRenderPass("main");
  VkResult result;
  // パイプライン構築.
  VkGraphicsPipelineCreateInfo pipelineCI{
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    nullptr, 0,
    uint32_t(shaderStagesForMosaic.size()), shaderStagesForMosaic.data(),
    &pipelineVisCI, &inputAssemblyCI,
    nullptr, // Tessellation
    &viewportCI, // ViewportState
    &rasterizerState,
    &multisampleCI,
    &dsState,
    &colorBlendStateCI,
    &pipelineDynamicStateCI,
    m_layoutEffect.pipeline,
    renderPass,
    0, // subpass
    VK_NULL_HANDLE, 0, // basePipeline
  };
  result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_mosaicPipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipeline Failed.");

  pipelineCI.pStages = shaderStagesForWater.data();
  result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_waterPipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipeline Failed.");

  book_util::DestroyShaderModules(m_device, shaderStagesForMosaic);
  book_util::DestroyShaderModules(m_device, shaderStagesForWater);
}

void PostEffectApp::PrepareRenderTexture()
{
  // 描画先テクスチャの準備.
  ImageObject colorTarget, depthTarget;
  auto colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  auto depthFormat = VK_FORMAT_D32_SFLOAT;
  auto surfaceExtent = m_swapchain->GetSurfaceExtent();
  auto width = surfaceExtent.width;
  auto height = surfaceExtent.height;
  {
    VkImageCreateInfo imageCI{
      VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      nullptr, 0,
      VK_IMAGE_TYPE_2D,
      colorFormat,
      { width, height, 1 },
      1, 1, VK_SAMPLE_COUNT_1_BIT,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_SHARING_MODE_EXCLUSIVE,
      0, nullptr,
      VK_IMAGE_LAYOUT_UNDEFINED
    };
    auto result = vkCreateImage(m_device, &imageCI, nullptr, &colorTarget.image);
    ThrowIfFailed(result, "vkCreateImage Failed.");

    // メモリ量の算出.
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(m_device, colorTarget.image, &reqs);
    VkMemoryAllocateInfo info{
      VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      nullptr,
      reqs.size,
      GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    result = vkAllocateMemory(m_device, &info, nullptr, &colorTarget.memory);
    ThrowIfFailed(result, "vkAllocateMemory Failed.");
    vkBindImageMemory(m_device, colorTarget.image, colorTarget.memory, 0);

    VkImageViewCreateInfo viewCI{
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      nullptr, 0,
      colorTarget.image,
      VK_IMAGE_VIEW_TYPE_2D,
      imageCI.format,
      book_util::DefaultComponentMapping(),
      { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    result = vkCreateImageView(m_device, &viewCI, nullptr, &colorTarget.view);
    ThrowIfFailed(result, "vkCreateImageView Failed.");
  }
  {
    VkImageCreateInfo imageCI{
      VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      nullptr, 0,
      VK_IMAGE_TYPE_2D,
      depthFormat,
      { width, height, 1 },
      1, 1, VK_SAMPLE_COUNT_1_BIT,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_SHARING_MODE_EXCLUSIVE,
      0, nullptr,
      VK_IMAGE_LAYOUT_UNDEFINED
    };
    auto result = vkCreateImage(m_device, &imageCI, nullptr, &depthTarget.image);
    ThrowIfFailed(result, "vkCreateImage Failed.");

    // メモリ量の算出.
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(m_device, depthTarget.image, &reqs);
    VkMemoryAllocateInfo info{
      VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      nullptr,
      reqs.size,
      GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };
    result = vkAllocateMemory(m_device, &info, nullptr, &depthTarget.memory);
    ThrowIfFailed(result, "vkAllocateMemory Failed.");
    vkBindImageMemory(m_device, depthTarget.image, depthTarget.memory, 0);

    VkImageViewCreateInfo viewCI{
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      nullptr, 0,
      depthTarget.image,
      VK_IMAGE_VIEW_TYPE_2D,
      imageCI.format,
      book_util::DefaultComponentMapping(),
      { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}
    };
    result = vkCreateImageView(m_device, &viewCI, nullptr, &depthTarget.view);
    ThrowIfFailed(result, "vkCreateImageView Failed.");
  }

  m_colorTarget = colorTarget;
  m_depthTarget = depthTarget;

  // フレームバッファを準備.
  vector<VkImageView> views;
  views.push_back(m_colorTarget.view);
  views.push_back(m_depthTarget.view);
  auto renderPass = GetRenderPass("render_target");
  m_renderTextureFB = CreateFramebuffer(renderPass, width, height, uint32_t(views.size()), views.data());
}

void PostEffectApp::RenderToTexture(VkCommandBuffer command)
{
  array<VkClearValue, 2> clearValue = {
    {
      { 0.2f, 0.65f, 0.0f, 0.0f}, // for Color
      { 1.0f, 0 }, // for Depth
    }
  };
  auto renderArea = VkRect2D{
    VkOffset2D{0,0},
    m_swapchain->GetSurfaceExtent(),
  };
  auto renderPass = GetRenderPass("render_target");
  VkRenderPassBeginInfo rpBI{
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    nullptr,
    renderPass,
    m_renderTextureFB,
    renderArea,
    uint32_t(clearValue.size()), clearValue.data()
  };

  {
    ShaderParameters shaderParams{};
    shaderParams.view = glm::lookAtRH(
      glm::vec3(3.0f, 5.0f, 5.0f),
      glm::vec3(3.0f, 2.0f, 0.0f),
      glm::vec3(0, 1, 0)
    );
    auto extent = m_swapchain->GetSurfaceExtent();
    shaderParams.proj = glm::perspectiveRH(
      glm::radians(45.0f), float(extent.width) / float(extent.height), 0.1f, 1000.0f
    );

    auto ubo = m_teapot.sceneUB[m_frameIndex];
    void* p;
    vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
    memcpy(p, &shaderParams, sizeof(ShaderParameters));
    vkUnmapMemory(m_device, ubo.memory);
  }

  auto extent = m_swapchain->GetSurfaceExtent();
  VkViewport viewport{
    0, 0, float(extent.width), float(extent.height), 0.0f, 1.0f
  };
  VkRect2D scissor{
    { 0, 0},
    extent
  };
  VkPipelineViewportStateCreateInfo viewportCI{
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    nullptr, 0,
    1, &viewport,
    1, &scissor,
  };

  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_teapot.pipeline);
  vkCmdSetScissor(command, 0, 1, &scissor);
  vkCmdSetViewport(command, 0, 1, &viewport);

  vkCmdBindDescriptorSets(
    command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layoutTeapot.pipeline, 
    0, 1, &m_teapot.descriptorSet[m_frameIndex], 0, nullptr);
  vkCmdBindIndexBuffer(command, m_teapot.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  VkDeviceSize offsets[] = { 0 };
  vkCmdBindVertexBuffers(command, 0,
    1, &m_teapot.vertexBuffer.buffer, offsets);
  vkCmdDrawIndexed(command, m_teapot.indexCount, InstanceCount, 0, 0, 0);

  vkCmdEndRenderPass(command);
}

void PostEffectApp::RenderToMain( VkCommandBuffer command )
{
  array<VkClearValue, 2> clearValue = {
    {
      { 0.85f, 0.5f, 0.5f, 0.0f}, // for Color
      { 1.0f, 0 }, // for Depth
    }
  };
  auto surfaceExtenet = m_swapchain->GetSurfaceExtent();
  auto renderArea = VkRect2D{
    VkOffset2D{0,0},
    surfaceExtenet
  };

  auto renderPass = GetRenderPass("main");
  VkRenderPassBeginInfo rpBI{
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    nullptr,
    renderPass,
    m_framebuffers[m_frameIndex],
    renderArea,
    uint32_t(clearValue.size()), clearValue.data()
  };

  {
    m_effectParameter.frameCount = m_frameCount;
    auto screenSize = vec2(
      float(surfaceExtenet.width),
      float(surfaceExtenet.height));
    m_effectParameter.screenSize = screenSize;

    auto ubo = m_effectUB[m_frameIndex];
    void* p;
    vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
    memcpy(p, &m_effectParameter, sizeof(ShaderParameters));
    vkUnmapMemory(m_device, ubo.memory);
  }

  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindDescriptorSets(
    command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layoutEffect.pipeline,
    0, 1, &m_effectDescriptorSet[m_frameIndex], 0, nullptr);
  if (m_effectType == EFFECT_TYPE_MOSAIC)
  {
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_mosaicPipeline);
  }
  if (m_effectType == EFFECT_TYPE_WATER)
  {
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_waterPipeline);
  }
  
  auto extent = m_swapchain->GetSurfaceExtent();
  VkViewport viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));
  VkRect2D scissor{
    { 0, 0},
    extent
  };
  VkPipelineViewportStateCreateInfo viewportCI{
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    nullptr, 0,
    1, &viewport,
    1, &scissor,
  };
  vkCmdSetScissor(command, 0, 1, &scissor);
  vkCmdSetViewport(command, 0, 1, &viewport);

  vkCmdDraw(command, 4, 1, 0, 0);

  RenderImGui(command);

  vkCmdEndRenderPass(command);
}

void PostEffectApp::RenderImGui(VkCommandBuffer command)
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  {
    ImGui::Begin("Control");
    ImGui::Text("PostEffect");
    auto framerate = ImGui::GetIO().Framerate;
    ImGui::Text("Framerate(avg) %.3f ms/frame", 1000.0f / framerate);

    ImGui::Combo("Effect", (int*)&m_effectType, "Mosaic effect\0Water effect\0\0");
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Mosaic effect", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::Indent();
      ImGui::SliderFloat("Size", &m_effectParameter.mosaicBlockSize, 10, 50);
      ImGui::Unindent();
      ImGui::Spacing();
    }
    if (ImGui::CollapsingHeader("WaterEffect", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::Indent();
      ImGui::SliderFloat("Ripple", &m_effectParameter.ripple, 0.1f, 1.5f);
      ImGui::SliderFloat("Speed", &m_effectParameter.speed, 1.0f, 5.0f);
      ImGui::SliderFloat("Distortion", &m_effectParameter.distortion, 0.01f, 0.5f);
      ImGui::SliderFloat("Brightness", &m_effectParameter.brightness, 0.0f, 1.0f);
      ImGui::Unindent();
      ImGui::Spacing();
    }
    ImGui::End();
  }

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

void PostEffectApp::DestroyModelData(ModelData& model)
{
  for (auto& bufObj : { model.vertexBuffer, model.indexBuffer })
  {
    vkDestroyBuffer(m_device, bufObj.buffer, nullptr);
    vkFreeMemory(m_device, bufObj.memory, nullptr);
  }
  for (auto& bufCB : model.sceneUB)
  {
    vkDestroyBuffer(m_device, bufCB.buffer, nullptr);
    vkFreeMemory(m_device, bufCB.memory, nullptr);
  }
  vkDestroyPipeline(m_device, model.pipeline, nullptr);
  vkFreeDescriptorSets(m_device, m_descriptorPool, uint32_t(model.descriptorSet.size()), model.descriptorSet.data());
}
