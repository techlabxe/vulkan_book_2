#include "AnimationApp.h"
#include "VulkanBookUtil.h"

#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

using namespace std;
using namespace glm;

inline std::array<VkAttachmentDescription, 2> GetDefaultRenderPassAttachments(
  VkFormat color, VkFormat depth
)
{
  array<VkAttachmentDescription, 2> attachments;
  attachments[0] = book_util::GetAttachmentDescription(color, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  attachments[1] = book_util::GetAttachmentDescription(depth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  return attachments;
}
inline std::array<VkAttachmentDescription, 2> GetShadowRenderPassAttachments(
  VkFormat color, VkFormat depth )
{
  array<VkAttachmentDescription, 2> attachments;
  attachments[0] = book_util::GetAttachmentDescription(color, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  attachments[1] = book_util::GetAttachmentDescription(depth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  return attachments;
}

RenderPMDApp::RenderPMDApp()
{
  m_camera.SetLookAt(vec3(-7.0f, 14.0f, 13.0f), vec3(-2.0f, 15.0f, 0.0f));
  m_drawOutline = true;
  m_frameCount = 0;
  m_isAnimeStart = false;
}

void RenderPMDApp::Prepare()
{
  CreateRenderPass();
  PrepareDepthbuffer();

  // モデル用のディスクリプタセットレイアウトを構築.
  // モデル用のパイプラインレイアウトを構築.
  PrepareLayout();

  PrepareFramebuffers();
  PrepareShadowTargets();

  PrepareCommandBuffersPrimary();

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

  const char filePath[] = "初音ミク.pmd"; // このデータは用意してください。
  m_model.Load(filePath, this);
  m_model.SetShadowMap(m_shadowColor);
  m_model.Prepare(this);

  auto command = CreateCommandBuffer();
  ImGui_ImplVulkan_CreateFontsTexture(command);
  FinishCommandBuffer(command);
  vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);

  m_faceWeights.resize(m_model.GetFaceMorphCount());

  m_animator.Prepare("animation.vmd"); // このデータは用意してください。
  m_animator.Attach(&m_model);
}

void RenderPMDApp::Cleanup()
{
  m_model.Cleanup(this);

  DestroyImage(m_shadowColor);
  DestroyImage(m_shadowDepth);
  DestroyFramebuffers(1, &m_shadowFramebuffer);

  for (auto& cmd : m_mainCommands)
  {
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd.command);
    vkDestroyFence(m_device, cmd.fence, nullptr);
  }

  DestroyImage(m_depthBuffer);
  DestroyFramebuffers(uint32_t(m_framebuffers.size()), m_framebuffers.data());

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void RenderPMDApp::Render()
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

  array<VkClearValue, 2> clearValue = {
  {
    { 0.85f, 0.5f, 0.5f, 0.0f}, // for Color
    { 1.0f, 0 }, // for Depth
  }
  };

  auto extent = m_swapchain->GetSurfaceExtent();
  auto eyePosition = vec3(-15.0f, 15.0f, 20.0f);
  auto target = vec3(0, 10, 0);
  m_sceneParameters.eyePosition = vec4(m_camera.GetPosition(), 1.0f);
  m_sceneParameters.view = m_camera.GetViewMatrix();
  m_sceneParameters.proj = perspective(
    radians(45.f), float(extent.width) / float(extent.height), 0.1f, 500.0f);
  
  auto lightView = glm::lookAt(vec3(0.0f, 30.0f, 40.0f), vec3(0, 0, 0), vec3(0, 1, 0));
  auto lightProj = glm::ortho(-40.f, 40.0f, -40.0f, 40.0f, 0.1f, 100.0f);

  m_sceneParameters.lightViewProj = lightProj * lightView;
  m_sceneParameters.lightDirection = vec4(0.0f, 20.0f, 20.0f, 0.0f);

  auto matBias = glm::translate(mat4(1.0f), vec3(0.5f,0.5f,0.5f)) * glm::scale(mat4(1.0f), vec3(0.5f, 0.5f, 0.5f));
  m_sceneParameters.lightViewProjBias = matBias * m_sceneParameters.lightViewProj;

  // アニメーションを適用する.
  if (!m_isAnimeStart)
  {
    if (ImGui::GetIO().KeysDown[ImGui::GetKeyIndex(ImGuiKey_LeftArrow)])
    {
      m_frameCount--;
    }
    if (ImGui::GetIO().KeysDown[ImGui::GetKeyIndex(ImGuiKey_RightArrow)])
    {
      m_frameCount++;
    }
  }
  if (m_frameCount < 0)
  {
    m_frameCount = 0;
  }
  m_animator.UpdateAnimation(m_frameCount);

  m_model.SetSceneParameter(m_sceneParameters);
  m_model.Update(imageIndex, this);

  auto command = m_mainCommands[imageIndex].command;
  auto fence = m_mainCommands[imageIndex].fence;
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

  RenderShadowPass(command, imageIndex);

  // パイプラインバリア設定.
  {
    VkImageMemoryBarrier imageBarrier{
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      nullptr,
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,
      m_shadowColor.image,
      {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    vkCmdPipelineBarrier(
      command,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_DEPENDENCY_BY_REGION_BIT,
      0, nullptr,
      0, nullptr,
      1, &imageBarrier
    );
  }

  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  auto subcommand = m_model.GetCommandBuffers(imageIndex);
  // モデル通常描画
  vkCmdExecuteCommands(command, uint32_t(subcommand.size()), subcommand.data());
  // 輪郭線描画
  if (m_drawOutline)
  {
    auto commandOutline = m_model.GetCommandBuffersOutline(imageIndex);
    vkCmdExecuteCommands(command, uint32_t(commandOutline.size()), commandOutline.data());
  }
  vkCmdEndRenderPass(command);

  rpBI.renderPass = GetRenderPass("imgui");
  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);
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

  if (m_isAnimeStart)
  {
    m_frameCount++;
  }
}

bool RenderPMDApp::OnSizeChanged(uint32_t width, uint32_t height)
{
  bool ret = VulkanAppBase::OnSizeChanged(width, height);
  if (ret)
  {
  }
  return ret;
}

void RenderPMDApp::OnMouseButtonDown(int button)
{
  auto io = ImGui::GetIO();
  if (io.WantCaptureMouse)
  {
    return;
  }
  m_camera.OnMouseButtonDown(button);
}
void RenderPMDApp::OnMouseButtonUp(int button)
{
  auto io = ImGui::GetIO();
  if (io.WantCaptureMouse)
  {
    return;
  }
  m_camera.OnMouseButtonUp();
}

void RenderPMDApp::OnMouseMove(int dx, int dy)
{
  auto io = ImGui::GetIO();
  if (io.WantCaptureMouse)
  {
    return;
  }
  m_camera.OnMouseMove(dx, dy);
}


void RenderPMDApp::CreateRenderPass()
{
  auto attachments = GetDefaultRenderPassAttachments(
    m_swapchain->GetSurfaceFormat().format,
    VK_FORMAT_D32_SFLOAT
  );
  array<VkAttachmentReference, 2> references{{
    { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
  }};
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

  attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkRenderPass renderPass;
  auto result = vkCreateRenderPass(m_device, &rpCI, nullptr, &renderPass);
  ThrowIfFailed(result, "vkCreateRenderPass Failed.");
  RegisterRenderPass("default", renderPass);

  auto attachmentsShadow = GetShadowRenderPassAttachments(
    VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_D32_SFLOAT
  );
  rpCI.pAttachments = attachmentsShadow.data();
  result = vkCreateRenderPass(m_device, &rpCI, nullptr, &renderPass);
  ThrowIfFailed(result, "vkCreateRenderPass Failed.");
  RegisterRenderPass("shadow", renderPass);

  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  rpCI.pAttachments = attachments.data();
  result = vkCreateRenderPass(m_device, &rpCI, nullptr, &renderPass);
  ThrowIfFailed(result, "vkCreateRenderPass Failed.");
  RegisterRenderPass("imgui", renderPass);
}

void RenderPMDApp::PrepareDepthbuffer()
{
  auto extent = m_swapchain->GetSurfaceExtent();
  auto format = VK_FORMAT_D32_SFLOAT;
  m_depthBuffer = CreateTexture(extent.width, extent.height, format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}
void RenderPMDApp::PrepareFramebuffers()
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

void RenderPMDApp::PrepareShadowTargets()
{
  VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  m_shadowColor = CreateTexture(ShadowSize, ShadowSize, VK_FORMAT_R32G32B32A32_SFLOAT, usage);
  usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  m_shadowDepth = CreateTexture(ShadowSize, ShadowSize, VK_FORMAT_D32_SFLOAT, usage);

  auto renderPass = GetRenderPass("shadow");

  std::vector<VkImageView> views;
  views.push_back(m_shadowColor.view);
  views.push_back(m_shadowDepth.view);
  m_shadowFramebuffer = CreateFramebuffer(
    renderPass, ShadowSize, ShadowSize,
    uint32_t(views.size()), views.data());
}

void RenderPMDApp::PrepareLayout()
{
  VkResult result;

  array<VkDescriptorSetLayoutBinding, 5> descriptorSetLayoutBindings{
    {
      { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr}, // SceneParam
      { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}, //Bone
      { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // MaterialParam
      { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
      { 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    }
  };

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    nullptr, 0,
    uint32_t(descriptorSetLayoutBindings.size()),
    descriptorSetLayoutBindings.data()
  };
  VkDescriptorSetLayout descriptorSetLayout;
  result = vkCreateDescriptorSetLayout(m_device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout);
  ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
  RegisterLayout("model", descriptorSetLayout);
  
  VkPipelineLayoutCreateInfo pipelineLayoutCI{
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    nullptr, 0,
    1, &descriptorSetLayout,
    0, nullptr
  };
  VkPipelineLayout pipelineLayout;
  result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &pipelineLayout);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
  RegisterLayout("model", pipelineLayout);


}

void RenderPMDApp::PrepareCommandBuffersPrimary()
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
  m_mainCommands.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    result = vkAllocateCommandBuffers(m_device, &commandAI, &m_mainCommands[i].command);
    ThrowIfFailed(result, "vkAllocateCommandBuffers Failed.");
    result = vkCreateFence(m_device, &fenceCI, nullptr, &m_mainCommands[i].fence);
    ThrowIfFailed(result, "vkCreateFence Failed.");
  }
}

void RenderPMDApp::RenderShadowPass(VkCommandBuffer command, uint32_t imageIndex)
{
  auto renderPass = GetRenderPass("shadow");
  array<VkClearValue, 2> clearValue = {{
    { 1.0f, 1.0f, 1.0f, 1.0f}, // for Color
    { 1.0f, 0 }, // for Depth
  }};
  VkRenderPassBeginInfo rpBI{
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
   nullptr,
   renderPass, m_shadowFramebuffer,
   { { 0 }, { ShadowSize, ShadowSize }},
   uint32_t(clearValue.size()), clearValue.data()
  };

  vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
  auto modelCommands = m_model.GetCommandBuffersShadow(imageIndex);
  vkCmdExecuteCommands(command, uint32_t(modelCommands.size()), modelCommands.data());
  vkCmdEndRenderPass(command);
}

void RenderPMDApp::RenderImGui(VkCommandBuffer command)
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  {
    ImGui::Begin("Control");
    auto framerate = ImGui::GetIO().Framerate;
    ImGui::Text("Framerate(avg) %.3f ms/frame", 1000.0f / framerate);

    auto cameraPos = m_camera.GetPosition();
    ImGui::Text("CameraPos: (%.2f, %.2f, %.2f)", cameraPos.x, cameraPos.y, cameraPos.z);
    ImGui::Checkbox("Outline", &m_drawOutline);
    ImGui::ColorEdit3("Outline", (float*)&m_sceneParameters.outlineColor);
    ImGui::Spacing();
    ImGui::InputInt("Frame: ", &m_frameCount);
    if (ImGui::Checkbox("EnableAnimation", &m_isAnimeStart))
    {
      m_frameCount = m_isAnimeStart ? 0 : m_frameCount;
    }
    ImGui::End();
  }

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}
