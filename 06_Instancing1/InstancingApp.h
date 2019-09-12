#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>

class InstancingApp : public VulkanAppBase
{
public:
  InstancingApp();

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
  struct InstanceData
  {
    glm::vec3 offsetPosition;
    glm::vec4 color;
  };
  const uint32_t InstanceDataMax = 200;

private:
  void CreateRenderPass();
  void PrepareFramebuffers();
  void PrepareTeapot();
  void PrepareInstanceData();
  void CreatePipeline();

  void RenderImGui(VkCommandBuffer command);
private:
  VkRenderPass m_renderPass;
  ImageObject m_depthBuffer;

  std::vector<VkFramebuffer> m_framebuffers;
  std::vector<VkFence> m_commandFences;
  std::vector<VkCommandBuffer> m_commandBuffers;

  VkDescriptorSetLayout m_descriptorSetLayout;
  std::vector<VkDescriptorSet> m_descriptorSets;
  VkPipelineLayout m_pipelineLayout;
  VkPipeline    m_pipeline;
  struct ModelData
  {
    BufferObject vertexBuffer;
    BufferObject indexBuffer;
    uint32_t vertexCount;
    uint32_t indexCount;
  };
  ModelData m_teapot;
  BufferObject m_instanceData;
  uint32_t m_indexCount;
  int m_instanceCount;
  float m_cameraOffset;
  std::vector<BufferObject> m_uniformBuffers;
};