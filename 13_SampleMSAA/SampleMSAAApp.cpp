#include "SampleMSAAApp.h"
#include "TeapotModel.h"
#include "VulkanBookUtil.h"

#include <random>
#include <array>

#include <glm/gtc/matrix_transform.hpp>

using namespace std;
using namespace glm;

SampleMSAAApp::SampleMSAAApp()
{
  m_frameCount = 0;
}

void SampleMSAAApp::Prepare()
{
  CreateRenderPass();
  CreateRenderPassRT();
  CreateRenderPassMSAA();

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
  PrepareMsaaTexture();

  PrepareFramebufferMSAA();

  PrepareTeapot();
  PreparePlane();
  CreatePipelineTeapot();
  CreatePipelinePlane();

}

void SampleMSAAApp::Cleanup()
{
  DestroyModelData(m_teapot);
  DestroyModelData(m_plane);


  for (auto& layout : { m_layoutTeapot, m_layoutPlane })
  {
    vkDestroyPipelineLayout(m_device, layout.pipeline, nullptr);
    vkDestroyDescriptorSetLayout(m_device, layout.descriptorSet, nullptr);
  }

  DestroyImage(m_colorTarget);
  DestroyImage(m_depthTarget);
  DestroyImage(m_msaaColor);
  DestroyImage(m_msaaDepth);

  DestroyImage(m_depthBuffer);
  auto count = uint32_t(m_framebuffers.size());
  DestroyFramebuffers(count, m_framebuffers.data());
  DestroyFramebuffers(1, &m_framebufferMSAA);
  DestroyFramebuffers(1, &m_framebufferRT);

  vkDestroySampler(m_device, m_sampler, nullptr);
  for (auto f : m_commandFences)
  {
    vkDestroyFence(m_device, f, nullptr);
  }
  vkFreeCommandBuffers(m_device, m_commandPool, uint32_t(m_commandBuffers.size()), m_commandBuffers.data());
  m_commandBuffers.clear();
  m_commandFences.clear();
}

void SampleMSAAApp::Render()
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

  array<VkClearValue, 2> clearValue = {
  {
    { 0.0f, 0.0f, 0.0f, 0.0f}, // for Color
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
    GetRenderPass("draw_msaa"),
    m_framebufferMSAA,
    renderArea,
    uint32_t(clearValue.size()), clearValue.data()
  };
  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

  RenderToMSAABuffer(command);

  auto swapchainImage = m_swapchain->GetImage(imageIndex);
  VkImageMemoryBarrier swapchainToDstImageBarrier{
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    nullptr,
    VK_ACCESS_MEMORY_READ_BIT, // srcAccessMask
    VK_ACCESS_TRANSFER_WRITE_BIT, // dstAccessMask
    VK_IMAGE_LAYOUT_UNDEFINED, //VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_QUEUE_FAMILY_IGNORED,
    VK_QUEUE_FAMILY_IGNORED,
    swapchainImage,
    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
  };
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_DEPENDENCY_BY_REGION_BIT,
    0, nullptr, // memoryBarrier
    0, nullptr, // bufferMemoryBarrier
    1, &swapchainToDstImageBarrier
  );

  auto surfaceExtent = m_swapchain->GetSurfaceExtent();
  VkImageResolve regionMsaa{
    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
    { 0, 0, 0 }, // srcOffset
    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
    { 0, 0, 0 }, // dstOffset
    { surfaceExtent.width, surfaceExtent.height, 1} // extent
  };
  
  
  vkCmdResolveImage(
    command,
    m_msaaColor.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    1, &regionMsaa
  );

  VkImageMemoryBarrier swapchainToPresentSrcBarrier{
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    nullptr,
    VK_ACCESS_TRANSFER_WRITE_BIT, // srcAccessMask
    VK_ACCESS_MEMORY_READ_BIT,  // dstAccessMask
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    VK_QUEUE_FAMILY_IGNORED,
    VK_QUEUE_FAMILY_IGNORED,
    swapchainImage,
    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
  };
#if 0  // これでも動く
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    VK_DEPENDENCY_BY_REGION_BIT,
    0, nullptr, // memoryBarrier
    0, nullptr, // bufferMemoryBarrier
    1, &imageBarrier3
  );
#else
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_DEPENDENCY_BY_REGION_BIT,
    0, nullptr, // memoryBarrier
    0, nullptr, // bufferMemoryBarrier
    1, &swapchainToPresentSrcBarrier
  );
#endif

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


void SampleMSAAApp::CreateRenderPass()
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
  VkRenderPass renderPass;
  auto result = vkCreateRenderPass(m_device, &rpCI, nullptr, &renderPass);
  ThrowIfFailed(result, "vkCreateRenderPass Failed.");
  RegisterRenderPass("default", renderPass);
}

void SampleMSAAApp::CreateRenderPassRT()
{
  array<VkAttachmentDescription, 2> attachments;
  attachments[0] = VkAttachmentDescription{
    0, VK_FORMAT_R8G8B8A8_UNORM,
    VK_SAMPLE_COUNT_1_BIT,
    VK_ATTACHMENT_LOAD_OP_CLEAR,
    VK_ATTACHMENT_STORE_OP_STORE,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    VK_ATTACHMENT_STORE_OP_DONT_CARE,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  };
  attachments[1] = VkAttachmentDescription{
    0, VK_FORMAT_D32_SFLOAT,
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
    0, VK_PIPELINE_BIND_POINT_GRAPHICS,
    0, nullptr, // InputAttachments
    1, &colorRef,
    nullptr,
    &depthRef,  // DepthStencilAttachments
    0, nullptr
  };
  VkRenderPassCreateInfo rpCI{
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    nullptr, 0,
    uint32_t(attachments.size()), attachments.data(),
    1, &subpassDesc,
    0, nullptr,
  };
  VkRenderPass renderPass;
  auto result = vkCreateRenderPass(m_device, &rpCI, nullptr, &renderPass);
  ThrowIfFailed(result, "vkCreateRenderPass Failed.");
  RegisterRenderPass("render_target", renderPass);
}

void SampleMSAAApp::CreateRenderPassMSAA()
{
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_4_BIT;
  auto format = m_swapchain->GetSurfaceFormat().format;
  VkRenderPass renderPass;

  array<VkAttachmentDescription, 2> attachments;
  auto& colorTarget = attachments[0];
  colorTarget = VkAttachmentDescription{
    0,  // Flags
    format,
    samples,
    VK_ATTACHMENT_LOAD_OP_CLEAR,
    VK_ATTACHMENT_STORE_OP_STORE,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    VK_ATTACHMENT_STORE_OP_DONT_CARE,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
  };
  auto& depthTarget = attachments[1];
  depthTarget = VkAttachmentDescription{
    0,
    VK_FORMAT_D32_SFLOAT,
    samples,
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

  auto result = vkCreateRenderPass(m_device, &rpCI, nullptr, &renderPass);
  ThrowIfFailed(result, "vkCreateRenderPass Failed.");
  RegisterRenderPass("draw_msaa", renderPass);
}


void SampleMSAAApp::PrepareFramebuffers()
{
  auto imageCount = m_swapchain->GetImageCount();
  auto extent = m_swapchain->GetSurfaceExtent();
  m_framebuffers.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    vector<VkImageView> views;
    views.push_back(m_swapchain->GetImageView(i));
    views.push_back(m_depthBuffer.view);

    auto renderPass = GetRenderPass("default");
    m_framebuffers[i] = CreateFramebuffer(
      renderPass,
      extent.width, extent.height,
      uint32_t(views.size()), views.data()
    );
  }
}
void SampleMSAAApp::PrepareFramebufferMSAA()
{
  auto extent = m_swapchain->GetSurfaceExtent();
  vector<VkImageView> views;
  views.push_back(m_msaaColor.view);
  views.push_back(m_msaaDepth.view);
  auto renderPass = GetRenderPass("draw_msaa");
  m_framebufferMSAA = CreateFramebuffer(
    renderPass,
    extent.width, extent.height,
    uint32_t(views.size()), views.data()
  );
}

bool SampleMSAAApp::OnSizeChanged(uint32_t width, uint32_t height)
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

void SampleMSAAApp::PrepareTeapot()
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

  WriteToHostVisibleMemory(stageVB.memory, bufferSizeVB, TeapotModel::TeapotVerticesPN);
  WriteToHostVisibleMemory(stageIB.memory, bufferSizeIB, TeapotModel::TeapotIndices);

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
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },
  };
  VkDescriptorSetLayoutCreateInfo descSetLayoutCI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    nullptr, 0,
    _countof(descSetLayoutBindings), descSetLayoutBindings,
  };
  auto result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &layout.descriptorSet);
  ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");

  VkDescriptorSetAllocateInfo descriptorSetAI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    nullptr, m_descriptorPool,
    1, &layout.descriptorSet
  };

  m_teapot.descriptorSet.reserve(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkDescriptorSet descriptorSet;
    result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &descriptorSet);
    ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");
    m_teapot.descriptorSet.push_back(descriptorSet);
  }

  // ディスクリプタを書き込む.
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkDescriptorBufferInfo uboInfo{
      m_teapot.sceneUB[i].buffer,
      0, VK_WHOLE_SIZE
    };

    auto descSetSceneUB = book_util::PrepareWriteDescriptorSet(
      m_teapot.descriptorSet[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    );
    descSetSceneUB.pBufferInfo = &uboInfo;
    vkUpdateDescriptorSets(m_device, 1, &descSetSceneUB, 0, nullptr);
  }

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

void SampleMSAAApp::PreparePlane()
{
  VertexPT vertices[] = {
    { vec3(-1.0f, 1.0f, 0.0f), vec2(0.0f, 1.0f) },
    { vec3(-1.0f,-1.0f, 0.0f), vec2(0.0f, 0.0f) },
    { vec3( 1.0f, 1.0f, 0.0f), vec2(1.0f, 1.0f) },
    { vec3( 1.0f,-1.0f, 0.0f), vec2(1.0f, 0.0f) },
  };
  uint32_t indices[] = { 0, 1, 2, 3 };
  VkMemoryPropertyFlags memoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  const auto usageVB = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  const auto usageIB = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  auto bufferSizeVB = uint32_t(sizeof(vertices));
  auto bufferSizeIB = uint32_t(sizeof(indices));
  m_plane.vertexBuffer = CreateBuffer(bufferSizeVB, usageVB, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  m_plane.indexBuffer = CreateBuffer(bufferSizeIB, usageIB, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  m_plane.vertexCount = _countof(vertices);
  m_plane.indexCount = _countof(indices);

  WriteToHostVisibleMemory(m_plane.vertexBuffer.memory, bufferSizeVB, vertices);
  WriteToHostVisibleMemory(m_plane.indexBuffer.memory, bufferSizeIB, indices);

  // 定数バッファの準備.
  uint32_t imageCount = m_swapchain->GetImageCount();
  auto bufferSize = uint32_t(sizeof(ShaderParameters));
  m_plane.sceneUB = CreateUniformBuffers(bufferSize, imageCount);

  // テクスチャを貼る板用のディスクリプタセット/レイアウトを準備.
  LayoutInfo layout{};
  VkDescriptorSetLayoutBinding descSetLayoutBindings[] = {
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },
    { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT }
  };
  VkDescriptorSetLayoutCreateInfo descSetLayoutCI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    nullptr, 0,
    _countof(descSetLayoutBindings), descSetLayoutBindings,
  };
  auto result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &layout.descriptorSet);
  ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");

  VkDescriptorSetAllocateInfo descriptorSetAI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    nullptr, m_descriptorPool,
    1, &layout.descriptorSet
  };

  m_plane.descriptorSet.reserve(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkDescriptorSet descriptorSet;
    result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &descriptorSet);
    ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");
    m_plane.descriptorSet.push_back(descriptorSet);
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

  // ディスクリプタを書き込む.
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkDescriptorBufferInfo uboInfo{
      m_plane.sceneUB[i].buffer,
      0, VK_WHOLE_SIZE
    };

    auto descSetSceneUB = book_util::PrepareWriteDescriptorSet(
      m_plane.descriptorSet[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    );
    descSetSceneUB.pBufferInfo = &uboInfo;
    vkUpdateDescriptorSets(m_device, 1, &descSetSceneUB, 0, nullptr);

    VkDescriptorImageInfo texInfo{
      m_sampler,
      m_colorTarget.view,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    auto descSetTexture = book_util::PrepareWriteDescriptorSet(
      m_plane.descriptorSet[i], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    );
    descSetTexture.pImageInfo = &texInfo;
    vkUpdateDescriptorSets(m_device, 1, &descSetTexture, 0, nullptr);
  }

  // パイプラインレイアウトを準備.
  VkPipelineLayoutCreateInfo pipelineLayoutCI{
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    nullptr, 0,
    1, &layout.descriptorSet,
    0, nullptr
  };
  result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &layout.pipeline);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");

  m_layoutPlane = layout;
}

void SampleMSAAApp::CreatePipelineTeapot()
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

  VkViewport viewport{
    0, 0, TextureWidth, TextureHeight, 0.0f, 1.0f
  };
  VkRect2D scissor{
    { 0, 0},
    { TextureWidth, TextureHeight }
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
  auto renderPass = GetRenderPass("render_target");
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
    renderPass,//m_renderPass,
    0, // subpass
    VK_NULL_HANDLE, 0, // basePipeline
  };
  result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_teapot.pipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipeline Failed.");

  book_util::DestroyShaderModules(m_device, shaderStages);
}

void SampleMSAAApp::CreatePipelinePlane()
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
    VK_SAMPLE_COUNT_4_BIT,
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
    book_util::LoadShader(m_device, "planeVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
    book_util::LoadShader(m_device, "planeFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
  };
  auto rasterizerState = book_util::GetDefaultRasterizerState();
  auto dsState = book_util::GetDefaultDepthStencilState();
  auto renderPass = GetRenderPass("draw_msaa");
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
    m_layoutPlane.pipeline,
    renderPass,
    0, // subpass
    VK_NULL_HANDLE, 0, // basePipeline
  };
  result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_plane.pipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipeline Failed.");

  book_util::DestroyShaderModules(m_device, shaderStages);
}

void SampleMSAAApp::PrepareRenderTexture()
{
  // 描画先テクスチャの準備.
  ImageObject colorTarget;
  auto colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  auto depthFormat = VK_FORMAT_D32_SFLOAT;
  {
    VkImageCreateInfo imageCI{
      VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      nullptr, 0,
      VK_IMAGE_TYPE_2D,
      colorFormat,
      { TextureWidth, TextureHeight, 1 },
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

  // 描画先デプスバッファの準備.
  ImageObject depthTarget;
  {
    VkImageCreateInfo imageCI{
      VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      nullptr, 0,
      VK_IMAGE_TYPE_2D,
      depthFormat,
      { TextureWidth, TextureHeight, 1 },
      1, 1, VK_SAMPLE_COUNT_1_BIT,
      VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT| VK_IMAGE_USAGE_SAMPLED_BIT,
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

  vector<VkImageView> views;
  views.push_back(m_colorTarget.view);
  views.push_back(m_depthTarget.view);
  auto renderPass = GetRenderPass("render_target");
  m_framebufferRT = CreateFramebuffer(renderPass, TextureWidth, TextureHeight, uint32_t(views.size()), views.data());
}

void SampleMSAAApp::PrepareMsaaTexture()
{
  VkFormat colorFormat = m_swapchain->GetSurfaceFormat().format;
  auto surfaceExtent = m_swapchain->GetSurfaceExtent();
  VkExtent3D extent = { surfaceExtent.width, surfaceExtent.height, 1 };
  VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_4_BIT;
  auto imageCI = book_util::CreateEasyImageCreateInfo(colorFormat, extent, usage, samples);
  auto result = vkCreateImage(m_device, &imageCI, nullptr, &m_msaaColor.image);
  ThrowIfFailed(result, "vkCreateImage Failed.");

  VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  m_msaaColor.memory = AllocateMemory(m_msaaColor.image, memProps);
  vkBindImageMemory(m_device, m_msaaColor.image, m_msaaColor.memory, 0);

  VkImageViewCreateInfo viewCI{
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      nullptr, 0,
      m_msaaColor.image,
      VK_IMAGE_VIEW_TYPE_2D,
      imageCI.format,
      book_util::DefaultComponentMapping(),
      { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
  };
  result = vkCreateImageView(m_device, &viewCI, nullptr, &m_msaaColor.view);
  ThrowIfFailed(result, "vkCreateImageView Failed.");

  VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
  usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  imageCI = book_util::CreateEasyImageCreateInfo(depthFormat, extent, usage, samples);
  result = vkCreateImage(m_device, &imageCI, nullptr, &m_msaaDepth.image);
  ThrowIfFailed(result, "vkCreateImage Failed.");

  m_msaaDepth.memory = AllocateMemory(m_msaaDepth.image, memProps);
  vkBindImageMemory(m_device, m_msaaDepth.image, m_msaaDepth.memory, 0);
  viewCI.format = depthFormat;
  viewCI.image = m_msaaDepth.image;
  viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  result = vkCreateImageView(m_device, &viewCI, nullptr, &m_msaaDepth.view);
  ThrowIfFailed(result, "vkCreateImageView Failed.");

  VkPhysicalDeviceProperties physProps;
  vkGetPhysicalDeviceProperties(m_physicalDevice, &physProps);
}

void SampleMSAAApp::RenderToTexture(VkCommandBuffer command)
{
  array<VkClearValue, 2> clearValue = {
    {
      { 1.0f, 0.0f, 0.0f, 0.0f}, // for Color
      { 1.0f, 0 }, // for Depth
    }
  };

  auto renderArea = VkRect2D{
    VkOffset2D{0,0},
    { TextureWidth, TextureHeight },
  };

  VkRenderPassBeginInfo rpBI{
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    nullptr,
    GetRenderPass("render_target"),
    m_framebufferRT,
    renderArea,
    uint32_t(clearValue.size()), clearValue.data()
  };

  {
    ShaderParameters shaderParams{};
    shaderParams.world = glm::mat4(1.0f);
    shaderParams.view = glm::lookAtRH(
      glm::vec3(0.0f, 2.0f, 5.0f),
      glm::vec3(0.0f, 0.0f, 0.0f),
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

  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_teapot.pipeline);
  vkCmdBindDescriptorSets(
    command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layoutTeapot.pipeline, 
    0, 1, &m_teapot.descriptorSet[m_frameIndex], 0, nullptr);
  vkCmdBindIndexBuffer(command, m_teapot.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  VkDeviceSize offsets[] = { 0 };
  vkCmdBindVertexBuffers(command, 0,
    1, &m_teapot.vertexBuffer.buffer, offsets);
  vkCmdDrawIndexed(command, m_teapot.indexCount, 1, 0, 0, 0);

  vkCmdEndRenderPass(command);
}

void SampleMSAAApp::RenderToMSAABuffer( VkCommandBuffer command )
{
  {
    ShaderParameters shaderParams{};
    shaderParams.world = glm::rotate(glm::mat4(1.0f), glm::radians(float(m_frameCount)), glm::vec3(0.0f, 1.0f, 0.0f));
    shaderParams.view = glm::lookAtRH(
      glm::vec3(0.0f, 0.0f, 5.0f),
      glm::vec3(0.0f, 0.0f, 0.0f),
      glm::vec3(0, 1, 0)
    );
    auto extent = m_swapchain->GetSurfaceExtent();
    shaderParams.proj = glm::perspectiveRH(
      glm::radians(45.0f), float(extent.width) / float(extent.height), 0.1f, 1000.0f
    );

    auto ubo = m_plane.sceneUB[m_frameIndex];
    void* p;
    vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
    memcpy(p, &shaderParams, sizeof(ShaderParameters));
    vkUnmapMemory(m_device, ubo.memory);
  }

  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_plane.pipeline);
  vkCmdBindDescriptorSets(
    command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layoutPlane.pipeline,
    0, 1, &m_plane.descriptorSet[m_frameIndex], 0, nullptr);
  vkCmdBindIndexBuffer(command, m_plane.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  VkDeviceSize offsets[] = { 0 };
  vkCmdBindVertexBuffers(command, 0,
    1, &m_plane.vertexBuffer.buffer, offsets);
  vkCmdDrawIndexed(command, m_plane.indexCount, 1, 0, 0, 0);

  vkCmdEndRenderPass(command);
}

void SampleMSAAApp::DestroyModelData(ModelData& model)
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
