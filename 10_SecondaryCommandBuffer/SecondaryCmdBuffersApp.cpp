#include "SecondaryCmdBuffersApp.h"
#include "TeapotModel.h"
#include "VulkanBookUtil.h"

#include <random>
#include <array>

#include <glm/gtc/matrix_transform.hpp>

using namespace std;
using namespace glm;

static glm::vec4 colorSet[] = {
  glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
  glm::vec4(1.0f, 0.65f, 1.0f, 1.0f),
  glm::vec4(0.1f, 0.5f, 1.0f, 1.0f),
  glm::vec4(0.6f, 1.0f, 0.8f, 1.0f),
};


SecondaryCmdBuffersApp::SecondaryCmdBuffersApp()
{
}

void SecondaryCmdBuffersApp::Prepare()
{
  // レンダーパスを生成.
  m_renderPass = book_util::CreateRenderPass(
    m_device,
    m_swapchain->GetSurfaceFormat().format,
    VK_FORMAT_D32_SFLOAT);
 
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

  PrepareTeapot();
  PrepareInstanceData();
  CreatePipelineTeapot();
  
  PrepareDescriptors();
  PrepareSecondaryCommands();
}

void SecondaryCmdBuffersApp::Cleanup()
{
  for (auto& data : m_instanceUniforms)
  {
    DestroyBuffer(data);
  }
  DestroyModelData(m_teapot); 

  for (auto& layout : { m_layoutTeapot })
  {
    vkDestroyPipelineLayout(m_device, layout.pipeline, nullptr);
    vkDestroyDescriptorSetLayout(m_device, layout.descriptorSet, nullptr);
  }
  vkDestroyRenderPass(m_device, m_renderPass, nullptr);

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
}

void SecondaryCmdBuffersApp::Render()
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

  // ユニフォームバッファ更新.
  {
    ShaderParameters shaderParams{};
    shaderParams.view = glm::lookAtRH(
      glm::vec3(0.0f, 5.0f, 10.0f),
      glm::vec3(0.0f, 2.0f, 0.0f),
      glm::vec3(0, 1, 0)
    );
    auto extent = m_swapchain->GetSurfaceExtent();
    shaderParams.proj = glm::perspectiveRH(
      glm::radians(45.0f), float(extent.width) / float(extent.height), 0.1f, 1000.0f
    );

    void* p;
    auto ubo = m_teapot.sceneUB[imageIndex];
    vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
    memcpy(p, &shaderParams, sizeof(shaderParams));
    vkUnmapMemory(m_device, ubo.memory);
  }
  
  auto command = m_commandBuffers[imageIndex];
  auto fence = m_commandFences[imageIndex];
  vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
  vkResetFences(m_device, 1, &fence);

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

  VkCommandBufferBeginInfo commandBI{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    nullptr, 0, nullptr
  };
  vkBeginCommandBuffer(command, &commandBI);

  VkRenderPassBeginInfo rpBI{
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    nullptr,
    m_renderPass,
    m_framebuffers[imageIndex],
    renderArea,
    uint32_t(clearValue.size()), clearValue.data()
  };

  // セカンダリコマンドバッファを呼び出す.
  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  vkCmdExecuteCommands(command, 1, &m_secondaryCommands[imageIndex]);
  vkCmdEndRenderPass(command);

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

  m_swapchain->QueuePresent(m_deviceQueue, imageIndex, m_renderCompletedSem);
}

void SecondaryCmdBuffersApp::PrepareFramebuffers()
{
  auto imageCount = m_swapchain->GetImageCount();
  auto extent = m_swapchain->GetSurfaceExtent();
  m_framebuffers.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    vector<VkImageView> views;
    views.push_back(m_swapchain->GetImageView(i));
    views.push_back(m_depthBuffer.view);

    m_framebuffers[i] = CreateFramebuffer(
      m_renderPass,
      extent.width, extent.height,
      uint32_t(views.size()), views.data()
    );
  }
}

bool SecondaryCmdBuffersApp::OnSizeChanged(uint32_t width, uint32_t height)
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
  }
  return result;
}

void SecondaryCmdBuffersApp::PrepareTeapot()
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

void SecondaryCmdBuffersApp::PrepareInstanceData()
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
    float x = (i % 10) * 3.0f - 10.0f;
    float z = (i / 10) * -3.0f+ 5.0f;

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

void SecondaryCmdBuffersApp::PrepareDescriptors()
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

void SecondaryCmdBuffersApp::CreatePipelineTeapot()
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

  // シェーダーのロード.
  std::vector<VkPipelineShaderStageCreateInfo> shaderStages
  {
    book_util::LoadShader(m_device, "modelVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "modelFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
  };

  auto rasterizerState = book_util::GetDefaultRasterizerState();
  auto dsState = book_util::GetDefaultDepthStencilState();

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
    nullptr, // DynamicState
    m_layoutTeapot.pipeline,
    m_renderPass,
    0, // subpass
    VK_NULL_HANDLE, 0, // basePipeline
  };
  result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_teapot.pipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipeline Failed.");

  book_util::DestroyShaderModules(m_device, shaderStages);
}

void SecondaryCmdBuffersApp::PrepareSecondaryCommands()
{
  VkResult result;
  auto imageCount = m_swapchain->GetImageCount();
  m_secondaryCommands.resize(imageCount);

  VkCommandBufferAllocateInfo commandAI{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    nullptr,
    m_commandPool,
    VK_COMMAND_BUFFER_LEVEL_SECONDARY,
    imageCount
  };
  result = vkAllocateCommandBuffers(m_device, &commandAI, m_secondaryCommands.data());
  ThrowIfFailed(result, "vkAllocateCommandBuffers Failed.");

  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkCommandBufferInheritanceInfo inheritanceInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
      nullptr,
      m_renderPass,
      0,
      m_framebuffers[i],
      VK_FALSE, 0, 0
    };
    VkCommandBufferBeginInfo beginInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      nullptr,
      VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
      &inheritanceInfo
    };
    auto command = m_secondaryCommands[i];
    result = vkBeginCommandBuffer(command, &beginInfo);
    ThrowIfFailed(result, "vkBeginCommandBuffer Failed.");

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_teapot.pipeline);
    vkCmdBindDescriptorSets(
      command, VK_PIPELINE_BIND_POINT_GRAPHICS,
      m_layoutTeapot.pipeline,
      0, 1, &m_teapot.descriptorSet[i], 0, nullptr);
    vkCmdBindIndexBuffer(command, m_teapot.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(command, 0,
      1, &m_teapot.vertexBuffer.buffer, offsets);
    vkCmdDrawIndexed(command, m_teapot.indexCount, InstanceCount, 0, 0, 0);

    vkEndCommandBuffer(command);
  }
}

void SecondaryCmdBuffersApp::DestroyModelData(ModelData& model)
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
