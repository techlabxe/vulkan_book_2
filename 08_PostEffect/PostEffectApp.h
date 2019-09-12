#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>

class PostEffectApp : public VulkanAppBase
{
public:
  PostEffectApp();

  virtual void Prepare();
  virtual void Cleanup();
  virtual void Render();

  virtual bool OnSizeChanged(uint32_t width, uint32_t height);

  enum {
    InstanceCount = 200,
  };
  enum EffectType
  {
    EFFECT_TYPE_MOSAIC,
    EFFECT_TYPE_WATER,
  };

  struct ShaderParameters
  {
    glm::mat4 view;
    glm::mat4 proj;
  };
  struct EffectParameters
  {
    glm::vec2 screenSize;
    float mosaicBlockSize;
    UINT frameCount;
    float ripple;
    float speed;  // ó¨ÇÍë¨ìx
    float distortion; // òcÇ›ã≠ìx
    float brightness; // ñæÇÈÇ≥åvêî
  };

  struct InstanceData
  {
    glm::mat4 world;
    glm::vec4 color;
  };
  struct InstanceParameters
  {
    InstanceData data[InstanceCount];
  };

private:
  void PrepareFramebuffers();
  void PrepareTeapot();
  void PreparePlane();
  void PrepareInstanceData();
  
  void CreatePipelineTeapot();
  void CreatePipelinePlane();

  void PrepareDescriptors();
  void PreparePostEffectDescriptors();
  void PrepareRenderTexture();

  void RenderToTexture(VkCommandBuffer command);
  void RenderToMain(VkCommandBuffer command);
  void RenderImGui(VkCommandBuffer command);

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
  std::vector<BufferObject> m_instanceUniforms;

  struct LayoutInfo
  {
    VkDescriptorSetLayout descriptorSet;
    VkPipelineLayout pipeline;
  };
  LayoutInfo m_layoutTeapot;
  LayoutInfo m_layoutEffect;

  uint32_t m_frameIndex;

  ImageObject m_colorTarget, m_depthTarget;
  VkFramebuffer m_renderTextureFB;
  VkSampler m_sampler;

  EffectParameters  m_effectParameter;
  std::vector<BufferObject> m_effectUB;
  std::vector<VkDescriptorSet> m_effectDescriptorSet;

  EffectType m_effectType;
  VkPipeline m_mosaicPipeline, m_waterPipeline;
  uint32_t m_frameCount;
};