#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>


class UseImGuiApp : public VulkanAppBase
{
public:
  UseImGuiApp();

  virtual void Prepare();
  virtual void Cleanup();
  virtual void Render();

  virtual bool OnSizeChanged(uint32_t width, uint32_t height);

private:
  void PrepareRenderPass();
  void PrepareDepthbuffer();
  void PrepareFramebuffers();
  void PrepareCommandBuffersPrimary();

  void PrepareImGui();
  void CleanupImGui();
  void RenderImGui(VkCommandBuffer command);

private:
  ImageObject m_depthBuffer;
  std::vector<VkFramebuffer> m_framebuffers;
  struct CommandBuffer
  {
    VkFence fence;
    VkCommandBuffer command;
  };
  std::vector<CommandBuffer> m_commandBuffers;

  float m_factor;
  float m_color[4];
};