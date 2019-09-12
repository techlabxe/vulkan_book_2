#include "DisplayHDR10App.h"
#include "TeapotModel.h"
#include "VulkanBookUtil.h"

#include <array>

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace std;

DisplayHDR10App::DisplayHDR10App()
{
}

void DisplayHDR10App::Prepare()
{
  CreateRenderPass();
  
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

  VkPipelineLayoutCreateInfo pipelineLayoutCI{
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    nullptr, 0,
    1, &m_descriptorSetLayout, // SetLayouts
    0, nullptr, // PushConstants
  };
  result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &m_pipelineLayout);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");

  CreatePipeline();
}

void DisplayHDR10App::Cleanup()
{
  for (auto& ubo : m_uniformBuffers)
  {
    DestroyBuffer(ubo);
  }
  DestroyBuffer(m_teapot.vertexBuffer);
  DestroyBuffer(m_teapot.indexBuffer);

  vkFreeDescriptorSets(m_device, m_descriptorPool, uint32_t(m_descriptorSets.size()), m_descriptorSets.data());
  vkDestroyPipeline(m_device, m_pipeline, nullptr);
  vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
  vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

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

void DisplayHDR10App::Render()
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
  array<VkClearValue, 2> clearValue = {
    {
      { 0.85f, 0.5f, 0.5f, 0.0f}, // for Color
      { 1.0f, 0 }, // for Depth
    }
  };

  auto renderArea = VkRect2D{
    VkOffset2D{0,0},
    m_swapchain->GetSurfaceExtent(),
  };

  VkRenderPassBeginInfo rpBI{
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    nullptr,
    m_renderPass,
    m_framebuffers[imageIndex],
    renderArea,
    uint32_t(clearValue.size()), clearValue.data()
  };

  VkCommandBufferBeginInfo commandBI{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    nullptr, 0, nullptr
  };

  {

    ShaderParameters shaderParams{};
    shaderParams.world = glm::mat4(1.0f);

    glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 5.0f);
    shaderParams.view = glm::lookAtRH(
      cameraPos, 
      glm::vec3(0.0f, 0.0f, 0.0f),
      glm::vec3(0, 1, 0)
    );
    auto extent = m_swapchain->GetSurfaceExtent();
    shaderParams.proj = glm::perspectiveRH(
      glm::radians(45.0f), float(extent.width) / float(extent.height), 0.1f, 1000.0f
    );
    shaderParams.lightPos = glm::vec4(0.0f, 10.0f, 10.0f, 0.0f);
    shaderParams.cameraPos = glm::vec4(cameraPos, 0.0f);

    auto ubo = m_uniformBuffers[imageIndex];
    void* p;
    vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
    memcpy(p, &shaderParams, sizeof(ShaderParameters));
    vkUnmapMemory(m_device, ubo.memory);
  }

  auto command = m_commandBuffers[imageIndex];
  auto fence = m_commandFences[imageIndex];
  vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);

  vkBeginCommandBuffer(command, &commandBI);
  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[imageIndex], 0, nullptr);
  vkCmdBindIndexBuffer(command, m_teapot.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  VkDeviceSize offsets[] = { 0 };
  vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.vertexBuffer.buffer, offsets);
  vkCmdDrawIndexed(command, m_teapot.indexCount, 1, 0, 0, 0);


  vkCmdEndRenderPass(command);
  vkEndCommandBuffer(command);

  VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submitInfo{
    VK_STRUCTURE_TYPE_SUBMIT_INFO,
    nullptr,
    1, &m_presentCompletedSem, // WaitSemaphore
    &waitStageMask, // DstStageMask
    1, &command, // CommandBuffer
    1, &m_renderCompletedSem, // SignalSemaphore
  };
  vkResetFences(m_device, 1, &fence);
  vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);

  m_swapchain->QueuePresent(m_deviceQueue, imageIndex, m_renderCompletedSem);
}


void DisplayHDR10App::CreateRenderPass()
{
  array<VkAttachmentDescription, 2> attachments;
  auto& colorTarget = attachments[0];
  colorTarget = VkAttachmentDescription{
    0,  // Flags
    m_swapchain->GetSurfaceFormat().format,
    VK_SAMPLE_COUNT_1_BIT,
    VK_ATTACHMENT_LOAD_OP_CLEAR,
    VK_ATTACHMENT_STORE_OP_STORE,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    VK_ATTACHMENT_STORE_OP_DONT_CARE,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  };
  auto& depthTarget = attachments[1];
  depthTarget = VkAttachmentDescription{
    0,
    VK_FORMAT_D32_SFLOAT,
    VK_SAMPLE_COUNT_1_BIT,
    VK_ATTACHMENT_LOAD_OP_CLEAR,
    VK_ATTACHMENT_STORE_OP_STORE,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    VK_ATTACHMENT_STORE_OP_DONT_CARE,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
  };

  VkAttachmentReference colorRef{
    0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  };
  VkAttachmentReference depthRef{
    1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
  };

  VkSubpassDescription subpassDesc{
    0, // Flags
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    0, nullptr, // InputAttachments
    1, &colorRef, // ColorAttachments
    nullptr,    // ResolveAttachments
    &depthRef,  // DepthStencilAttachments
    0, nullptr, // PreserveAttachments
  };


  VkRenderPassCreateInfo rpCI{
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    nullptr, 0,
    uint32_t(attachments.size()), attachments.data(),
    1, &subpassDesc,
    0, nullptr, // Dependency
  };
  auto result = vkCreateRenderPass(m_device, &rpCI, nullptr, &m_renderPass);
  ThrowIfFailed(result, "vkCreateRenderPass Failed.");
}

void DisplayHDR10App::PrepareFramebuffers()
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

bool DisplayHDR10App::OnSizeChanged(uint32_t width, uint32_t height)
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

void DisplayHDR10App::PrepareTeapot()
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

  // ディスクリプタセットレイアウト
  VkDescriptorSetLayoutBinding descSetLayoutBindings[] = {
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, },
  };
  VkDescriptorSetLayoutCreateInfo descSetLayoutCI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    nullptr, 0,
    _countof(descSetLayoutBindings), descSetLayoutBindings,
  };
  auto result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &m_descriptorSetLayout);
  ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");

  // ディスクリプタセット.
  auto imageCount = m_swapchain->GetImageCount();
  VkDescriptorSetAllocateInfo descriptorSetAI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    nullptr, m_descriptorPool,
    1, &m_descriptorSetLayout
  };
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkDescriptorSet descriptorSet;
    result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &descriptorSet);
    ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");
    m_descriptorSets.push_back(descriptorSet);
  }

  // 定数バッファの準備.
  VkMemoryPropertyFlags uboMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  m_uniformBuffers.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    auto bufferSize = uint32_t(sizeof(ShaderParameters));
    m_uniformBuffers[i] = CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uboMemoryProps);
  }
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkDescriptorBufferInfo bufferInfo{
      m_uniformBuffers[i].buffer,
      0, VK_WHOLE_SIZE
    };

    VkWriteDescriptorSet writeDescSet{
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      nullptr,
      m_descriptorSets[i],  // dstSet
      0,
      0, // dstArrayElement
      1, // descriptorCount
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      nullptr,
      &bufferInfo,
      nullptr,
    };
    vkUpdateDescriptorSets(m_device, 1, &writeDescSet, 0, nullptr);
  }

  m_teapot.indexCount = _countof(TeapotModel::TeapotIndices);
  m_teapot.vertexCount = _countof(TeapotModel::TeapotVerticesPN);
}

void DisplayHDR10App::CreatePipeline()
{
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
    uint32_t(inputAttribs.size()), inputAttribs.data()
  };

  auto blendAttachmentState = book_util::GetOpaqueColorBlendAttachmentState();
  VkPipelineColorBlendStateCreateInfo colorBlendStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    nullptr, 0,
    VK_FALSE, VK_LOGIC_OP_CLEAR, // logicOpEnable
    1, &blendAttachmentState,
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
    book_util::LoadShader(m_device, "shaderVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "shaderFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
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
    m_pipelineLayout,
    m_renderPass,
    0, // subpass
    VK_NULL_HANDLE, 0, // basePipeline
  };
  result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_pipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipeline Failed.");

  book_util::DestroyShaderModules(m_device, shaderStages);
}