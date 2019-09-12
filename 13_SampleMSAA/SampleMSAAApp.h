#pragma once
#include "VulkanAppBase.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

class SampleMSAAApp : public VulkanAppBase
{
public:
  SampleMSAAApp();

  virtual void Prepare();
  virtual void Cleanup();
  virtual void Render();

  virtual bool OnSizeChanged(uint32_t width, uint32_t height);

  struct ShaderParameters
  {
    glm::mat4 world;
    glm::mat4 view;
    glm::mat4 proj;
  };

  enum {
    TextureWidth = 512,
    TextureHeight = 512,
  };
private:
  void CreateRenderPass();
  void CreateRenderPassRT();
  void CreateRenderPassMSAA();
  void PrepareFramebuffers();
  void PrepareFramebufferMSAA();
  void PrepareTeapot();
  void PreparePlane();
  
  void CreatePipelineTeapot();
  void CreatePipelinePlane();

  void PrepareRenderTexture();
  void PrepareMsaaTexture();

  void RenderToTexture(VkCommandBuffer command);
  void RenderToMSAABuffer(VkCommandBuffer command);

  struct VertexPT
  {
    glm::vec3 position;
    glm::vec2 uv;
  };
  struct ModelData
  {
    BufferObject vertexBuffer;
    BufferObject indexBuffer;
    uint32_t vertexCount;
    uint32_t indexCount;

    std::vector<BufferObject> sceneUB;
    std::vector<VkDescriptorSet> descriptorSet;

    VkPipeline pipeline;
  };

  void DestroyModelData(ModelData& data);
private:
  ImageObject m_depthBuffer;

  std::vector<VkFramebuffer> m_framebuffers;

  std::vector<VkFence> m_commandFences;
  std::vector<VkCommandBuffer> m_commandBuffers;

  ModelData m_teapot;
  ModelData m_plane;

  struct LayoutInfo
  {
    VkDescriptorSetLayout descriptorSet;
    VkPipelineLayout pipeline;
  };
  LayoutInfo m_layoutTeapot;
  LayoutInfo m_layoutPlane;

  uint32_t m_frameIndex;

  ImageObject m_colorTarget, m_depthTarget;
  VkFramebuffer m_framebufferRT;
  VkSampler m_sampler;

  ImageObject m_msaaColor, m_msaaDepth;
  VkFramebuffer m_framebufferMSAA;

  uint32_t m_frameCount;
};