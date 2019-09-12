#include "InstancingApp.h"
#include "TeapotModel.h"
#include "VulkanBookUtil.h"

#include <random>
#include <array>

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace std;
using namespace glm;

static glm::vec4 colorSet[] = {
  glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
  glm::vec4(1.0f, 0.65f, 1.0f, 1.0f),
  glm::vec4(0.1f, 0.5f, 1.0f, 1.0f),
  glm::vec4(0.6f, 1.0f, 0.8f, 1.0f),
};

InstancingApp::InstancingApp()
{
  m_instanceCount = 200;
  m_cameraOffset = 0.0f;
}

void InstancingApp::Prepare()
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
  PrepareInstanceData();
  PrepareDescriptors();

  VkPipelineLayoutCreateInfo pipelineLayoutCI{
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    nullptr, 0,
    1, &m_descriptorSetLayout, // SetLayouts
    0, nullptr, // PushConstants
  };
  result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &m_pipelineLayout);
  ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");

  CreatePipeline();

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
  info.MinImageCount = imageCount;
  info.ImageCount = imageCount;
  ImGui_ImplVulkan_Init(&info, m_renderPass);

  VkCommandBufferBeginInfo beginInfo{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    nullptr,
    VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  vkBeginCommandBuffer(m_commandBuffers[0], &beginInfo);
  ImGui_ImplVulkan_CreateFontsTexture(m_commandBuffers[0]);
  vkEndCommandBuffer(m_commandBuffers[0]);
  VkSubmitInfo submitInfo{
    VK_STRUCTURE_TYPE_SUBMIT_INFO,nullptr,
  };
  submitInfo.pCommandBuffers = m_commandBuffers.data();
  submitInfo.commandBufferCount = 1;
  vkQueueSubmit(m_deviceQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkDeviceWaitIdle(m_device);
}

void InstancingApp::Cleanup()
{
  for (auto& ubo : m_uniformBuffers)
  {
    DestroyBuffer(ubo);
  }
  for (auto& instanceUbo : m_instanceUniforms)
  {
    DestroyBuffer(instanceUbo);
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

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

void InstancingApp::Render()
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
    shaderParams.view = glm::lookAtRH(
      glm::vec3(3.0f, 5.0f, 10.0f - m_cameraOffset), 
      glm::vec3(3.0f, 2.0f, 0.0f - m_cameraOffset),
      glm::vec3(0, 1, 0)
    );
    auto extent = m_swapchain->GetSurfaceExtent();
    shaderParams.proj = glm::perspectiveRH(
      glm::radians(45.0f), float(extent.width) / float(extent.height), 0.1f, 1000.0f
    );

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

  auto extent = m_swapchain->GetSurfaceExtent();
  VkViewport viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));
  VkRect2D scissor{
    { 0, 0},
    extent
  };
  vkCmdSetScissor(command, 0, 1, &scissor);
  vkCmdSetViewport(command, 0, 1, &viewport);

  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[imageIndex], 0, nullptr);
  vkCmdBindIndexBuffer(command, m_teapot.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
  VkDeviceSize offsets[] = { 0 };
  vkCmdBindVertexBuffers(command, 0, 
    1, &m_teapot.vertexBuffer.buffer, offsets);
  vkCmdDrawIndexed(command, m_indexCount, m_instanceCount, 0, 0, 0);
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
  vkResetFences(m_device, 1, &fence);
  vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);

  m_swapchain->QueuePresent(m_deviceQueue, imageIndex, m_renderCompletedSem);
}


void InstancingApp::CreateRenderPass()
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

void InstancingApp::PrepareFramebuffers()
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

bool InstancingApp::OnSizeChanged(uint32_t width, uint32_t height)
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

void InstancingApp::RenderImGui(VkCommandBuffer command)
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  {
    ImGui::Begin("Control");
    ImGui::Text("DrawTeapot");
    auto framerate = ImGui::GetIO().Framerate;
    ImGui::Text("Framerate(avg) %.3f ms/frame", 1000.0f / framerate);
    ImGui::SliderInt("Count", &m_instanceCount, 1, InstanceDataMax);
    ImGui::SliderFloat("Camera", &m_cameraOffset, 0.0f, 50.0f);
    ImGui::End();
  }

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

void InstancingApp::PrepareTeapot()
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


  // 定数バッファの準備.
  uint32_t imageCount = m_swapchain->GetImageCount();
  VkMemoryPropertyFlags uboMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  m_uniformBuffers.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    auto bufferSize = uint32_t(sizeof(ShaderParameters));
    m_uniformBuffers[i] = CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uboMemoryProps);
  }
  m_indexCount = _countof(TeapotModel::TeapotIndices);
  //m_vertexCount = _countof(TeapotModel::TeapotVerticesPN);
}

void InstancingApp::PrepareInstanceData()
{
  VkMemoryPropertyFlags memoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

  // インスタンシング用のユニフォームバッファを準備
  auto bufferSize = uint32_t(sizeof(InstanceData)) * InstanceDataMax;
  m_instanceUniforms.resize(m_swapchain->GetImageCount());
  for (auto& ubo : m_instanceUniforms)
  {
    ubo = CreateBuffer(bufferSize, usage, memoryProps);
  }
  
  std::random_device rnd;
  std::vector<InstanceData> data(InstanceDataMax);
  for (uint32_t i = 0; i < InstanceDataMax; ++i)
  {
    const auto axisX = vec3(1.0f, 0.0f, 0.0f);
    const auto axisZ = vec3(0.0f, 0.0f, 1.0f);
    float k = float(rnd() % 360);
    float x = (i % 6) * 3.0f;
    float z = (i / 6) * -3.0f;

    mat4 mat(1.0f);
    mat = translate(mat, vec3(x, 0.0f, z));
    mat = rotate(mat, k, axisX);
    mat = rotate(mat, k, axisZ);
    
    data[i].world = mat;
    data[i].color = colorSet[i % _countof(colorSet)];
  }

  for (auto& ubo : m_instanceUniforms)
  {
    void* p;
    vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
    memcpy(p, data.data(), bufferSize);
    vkUnmapMemory(m_device, ubo.memory);
  }
}

void InstancingApp::PrepareDescriptors()
{
  // ディスクリプタセットレイアウト
  VkDescriptorSetLayoutBinding descSetLayoutBindings[] = {
    { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },
    { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT },
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

  // 確保したディスクリプタに書き込む.
  // [0] View,Projの定数バッファ.
  // [1] インスタンシング用のバッファ.
  for (uint32_t i = 0; i < imageCount; ++i)
  {
    VkDescriptorBufferInfo uniformBufferInfo{
      m_uniformBuffers[i].buffer,
      0, VK_WHOLE_SIZE
    };
    VkDescriptorBufferInfo instanceBufferInfo{
      m_instanceUniforms[i].buffer,
      0, VK_WHOLE_SIZE
    };

    auto descSetSceneUB = book_util::PrepareWriteDescriptorSet(
      m_descriptorSets[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    );
    descSetSceneUB.pBufferInfo = &uniformBufferInfo;

    auto descSetInstUB = book_util::PrepareWriteDescriptorSet(
      m_descriptorSets[i], 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
    );
    descSetInstUB.pBufferInfo = &instanceBufferInfo;

    std::array<VkWriteDescriptorSet, 2> writeDescriptorSets{
      descSetSceneUB, descSetInstUB
    };
    auto count = uint32_t(writeDescriptorSets.size());
    vkUpdateDescriptorSets(m_device, count, writeDescriptorSets.data(), 0, nullptr);
  }
}

void InstancingApp::CreatePipeline()
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

  auto extent = m_swapchain->GetSurfaceExtent();
  VkViewport viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));
  VkRect2D scissor{
    { 0, 0},
    { 0, 0}
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

  std::vector<VkDynamicState> dynamicStates{
    VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT,
  };
  VkPipelineDynamicStateCreateInfo pipelineDynamicStateCI{
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0,
    uint32_t(dynamicStates.size()), dynamicStates.data()
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
    &pipelineDynamicStateCI, // DynamicState
    m_pipelineLayout,
    m_renderPass,
    0, // subpass
    VK_NULL_HANDLE, 0, // basePipeline
  };
  result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_pipeline);
  ThrowIfFailed(result, "vkCreateGraphicsPipeline Failed.");

  book_util::DestroyShaderModules(m_device, shaderStages);
}