#include "UseImGuiApp.h"
#include "VulkanBookUtil.h"

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

using namespace std;
using namespace glm;

UseImGuiApp::UseImGuiApp()
{
}

void UseImGuiApp::Prepare()
{
  PrepareRenderPass();
  PrepareDepthbuffer();
  PrepareFramebuffers();

  PrepareCommandBuffersPrimary();

  PrepareImGui();
}

void UseImGuiApp::Cleanup()
{
  CleanupImGui();

  DestroyImage(m_depthBuffer);
  auto count = uint32_t(m_framebuffers.size());
  DestroyFramebuffers(count, m_framebuffers.data());

  for (auto c : m_commandBuffers)
  {
    vkDestroyFence(m_device, c.fence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &c.command);
  }
  m_commandBuffers.clear();
}

void UseImGuiApp::Render()
{
  if (m_isMinimizedWindow) {
    MsgLoopMinimizedWindow();
  }

  uint32_t imageIndex = 0;
  auto result = m_swapchain->AcquireNextImage(&imageIndex, m_presentCompletedSem);
  if (result == VK_ERROR_OUT_OF_DATE_KHR)
  {
    return;
  }

  array<VkClearValue, 2> clearValue = {{
    { 0.85f, 0.5f, 0.5f, 0.0f}, // for Color
    { 1.0f, 0 }, // for Depth
  }};
  for (int i = 0; i < 4; ++i) {
    clearValue[0].color.float32[i] = m_color[i];
  }

  auto extent = m_swapchain->GetSurfaceExtent();
  auto command = m_commandBuffers[imageIndex].command;
  auto fence = m_commandBuffers[imageIndex].fence;
  vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
  vkResetFences(m_device, 1, &fence);

  VkCommandBufferBeginInfo commandBI{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    nullptr, 0, nullptr
  };
  vkBeginCommandBuffer(command, &commandBI);

  auto renderPass = GetRenderPass("default");
  VkRenderPassBeginInfo rpBI{
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    nullptr,
    renderPass,
    m_framebuffers[imageIndex],
    GetSwapchainRenderArea(),
    uint32_t(clearValue.size()), clearValue.data()
  };
  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

  // ImGui 描画.
  RenderImGui(command);

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

  vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);
  m_swapchain->QueuePresent(m_deviceQueue, imageIndex, m_renderCompletedSem);

}

bool UseImGuiApp::OnSizeChanged(uint32_t width, uint32_t height)
{
  bool isResized = VulkanAppBase::OnSizeChanged(width, height);
  if (isResized)
  {
    // 古いデプスバッファを破棄
    DestroyImage(m_depthBuffer);

    // 古いフレームバッファを破棄.
    DestroyFramebuffers(uint32_t(m_framebuffers.size()), m_framebuffers.data());

    // 新解像度でのデプスバッファ作成.
    PrepareDepthbuffer();

    // 新解像度でのフレームバッファを作成.
    PrepareFramebuffers();
  }
  return isResized;
}

void UseImGuiApp::PrepareRenderPass()
{
  array<VkAttachmentDescription, 2> attachments;
  attachments[0] = book_util::GetAttachmentDescription(
    m_swapchain->GetSurfaceFormat().format,
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  attachments[1] = book_util::GetAttachmentDescription(
    VK_FORMAT_D32_SFLOAT, 
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

  array<VkAttachmentReference, 2> references{ {
    { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
  } };
  VkSubpassDescription subpassDesc{
    0, VK_PIPELINE_BIND_POINT_GRAPHICS,
    0, nullptr,
    1, &references[0], nullptr, &references[1], 0, nullptr
  };
  VkRenderPassCreateInfo rpCI{
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    nullptr, 0,
    uint32_t(attachments.size()), attachments.data(),
    1, &subpassDesc, 0, nullptr
  };

  VkRenderPass renderPass;
  auto result = vkCreateRenderPass(m_device, &rpCI, nullptr, &renderPass);
  ThrowIfFailed(result, "vkCreateRenderPass Failed.");
  RegisterRenderPass("default", renderPass);
}

void UseImGuiApp::PrepareDepthbuffer()
{
  auto extent = m_swapchain->GetSurfaceExtent();
  m_depthBuffer = CreateTexture(
    extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void UseImGuiApp::PrepareFramebuffers()
{
  auto imageCount = m_swapchain->GetImageCount();
  m_framebuffers.resize(imageCount);
  auto extent = m_swapchain->GetSurfaceExtent();
  auto renderPass = GetRenderPass("default");
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    vector<VkImageView> views;
    views.push_back(m_swapchain->GetImageView(i));
    views.push_back(m_depthBuffer.view);
    m_framebuffers[i] = CreateFramebuffer(
      renderPass, extent.width, extent.height,
      uint32_t(views.size()), views.data()
    );
  }
}

void UseImGuiApp::PrepareCommandBuffersPrimary()
{
  VkFenceCreateInfo fenceCI{
    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr,
    VK_FENCE_CREATE_SIGNALED_BIT
  };
  VkCommandBufferAllocateInfo commandAI{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr,
    m_commandPool,
    VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1
  };

  VkResult result;
  auto imageCount = m_swapchain->GetImageCount();
  m_commandBuffers.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    result = vkAllocateCommandBuffers(m_device, &commandAI, &m_commandBuffers[i].command);
    ThrowIfFailed(result, "vkAllocateCommandBuffers Failed.");
    result = vkCreateFence(m_device, &fenceCI, nullptr, &m_commandBuffers[i].fence);
    ThrowIfFailed(result, "vkCreateFence Failed.");
  }
}



void UseImGuiApp::PrepareImGui()
{
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
  info.MinImageCount = m_swapchain->GetImageCount();
  info.ImageCount = m_swapchain->GetImageCount();
  ImGui_ImplVulkan_Init(&info, GetRenderPass("default"));

  // フォントテクスチャを転送する.
  VkCommandBufferAllocateInfo commandAI{
   VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    nullptr, m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1
  };
  VkCommandBufferBeginInfo beginInfo{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };

  VkCommandBuffer command;
  vkAllocateCommandBuffers(m_device, &commandAI, &command);
  vkBeginCommandBuffer(command, &beginInfo);
  ImGui_ImplVulkan_CreateFontsTexture(command);
  vkEndCommandBuffer(command);

  VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
  submitInfo.pCommandBuffers = &command;
  submitInfo.commandBufferCount = 1;
  vkQueueSubmit(m_deviceQueue, 1, &submitInfo, VK_NULL_HANDLE);

  // フォントテクスチャ転送の完了を待つ.
  vkDeviceWaitIdle(m_device);
  vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
}

void UseImGuiApp::CleanupImGui()
{
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void UseImGuiApp::RenderImGui(VkCommandBuffer command)
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // ImGui ウィジェットを描画する.
  ImGui::Begin("Information");
  ImGui::Text("Hello,ImGui world");
  ImGui::Text("Framerate(avg) %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
  if (ImGui::Button("Button"))
  {
    // ボタンが押されたときの処理.
  }
  ImGui::SliderFloat("Factor", &m_factor, 0.0f, 100.0f);
  //ImGui::ColorEdit4("Color", m_color, ImGuiColorEditFlags_None);
  ImGui::ColorPicker4("Color", m_color);
  ImGui::End();

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(
    ImGui::GetDrawData(), command
  );
}
