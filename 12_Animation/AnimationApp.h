#pragma once
#include "VulkanAppBase.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <memory>

#include "Camera.h"
#include "Model.h"
#include "Animator.h"

class RenderPMDApp : public VulkanAppBase
{
public:
  RenderPMDApp();

  virtual void Prepare();
  virtual void Cleanup();
  virtual void Render();

  virtual bool OnSizeChanged(uint32_t width, uint32_t height);
  virtual void OnMouseButtonDown(int button);
  virtual void OnMouseButtonUp(int button);
  virtual void OnMouseMove(int dx, int dy);

private:
  void CreateRenderPass();
  void PrepareDepthbuffer();
  void PrepareFramebuffers();
  void PrepareShadowTargets();
  void PrepareLayout();
  void PrepareCommandBuffersPrimary();

  void RenderShadowPass(VkCommandBuffer command, uint32_t imageIndex);
  void RenderImGui(VkCommandBuffer command);
private:
  ImageObject m_depthBuffer;
  std::vector<VkFramebuffer> m_framebuffers;

  ImageObject m_shadowColor;
  ImageObject m_shadowDepth;
  VkFramebuffer m_shadowFramebuffer;
  
  enum {
    ShadowSize = 1024,
  };
  struct CommandBuffer
  {
    VkFence fence;
    VkCommandBuffer command;
  };
  std::vector<CommandBuffer> m_mainCommands;
  Model m_model;
  Model::SceneParameter m_sceneParameters;
  Animator m_animator;

  Camera m_camera;
  bool m_drawOutline;
  std::vector<float> m_faceWeights;

  int m_frameCount;
  bool m_isAnimeStart;
};

