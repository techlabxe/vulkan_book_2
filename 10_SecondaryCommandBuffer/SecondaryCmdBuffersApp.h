#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>

class SecondaryCmdBuffersApp : public VulkanAppBase
{
public:
  SecondaryCmdBuffersApp();

  virtual void Prepare();
  virtual void Cleanup();
  virtual void Render();

  virtual bool OnSizeChanged(uint32_t width, uint32_t height);

  enum {
    InstanceCount = 200,
  };

  struct ShaderParameters
  {
    glm::mat4 view;
    glm::mat4 proj;
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
  void PrepareInstanceData();
  
  void CreatePipelineTeapot();
  void PrepareDescriptors();
  void PrepareSecondaryCommands();

  void RenderToMain(VkCommandBuffer command);

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
  VkRenderPass m_renderPass;
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

  std::vector<VkCommandBuffer> m_secondaryCommands;
};