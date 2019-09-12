#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <fstream>
#include <stdexcept>
#include <functional>
#include <vector>
#include <array>

#define STRINGFY(s)  #s
#define TO_STRING(x) STRINGFY(x)
#define FILE_PREFIX __FILE__ "(" TO_STRING(__LINE__) "): " 
#define ThrowIfFailed(code, msg) book_util::CheckResultCodeVk(code, FILE_PREFIX msg)

namespace book_util
{
  class VulkanException : public std::runtime_error
  {
  public:
    VulkanException(const std::string& msg) : std::runtime_error(msg.c_str())
    {
    }
  };

  inline void CheckResultCodeVk(VkResult code, const std::string& errorMsg)
  {
    if (code != VK_SUCCESS)
    {
      throw VulkanException(errorMsg);
    }
  }

  template<class T, class U>
  void SafeDestroy(T& handle, U func)
  {
    if (handle != VK_NULL_HANDLE)
    {
      func(handle);
    }
    handle = VK_NULL_HANDLE;
  }
  
  inline VkAttachmentDescription GetAttachmentDescription(VkFormat format, VkImageLayout before, VkImageLayout after, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT)
  {
    return VkAttachmentDescription{
      0, format, samples,
      VK_ATTACHMENT_LOAD_OP_CLEAR,
      VK_ATTACHMENT_STORE_OP_STORE,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      VK_ATTACHMENT_STORE_OP_DONT_CARE,
      before,
      after };
  }

  inline VkComponentMapping DefaultComponentMapping()
  {
    return VkComponentMapping{
      VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A
    };
  }

  inline VkPipelineColorBlendAttachmentState GetOpaqueColorBlendAttachmentState()
  {
    const auto colorWriteAll = \
      VK_COLOR_COMPONENT_R_BIT | \
      VK_COLOR_COMPONENT_G_BIT | \
      VK_COLOR_COMPONENT_B_BIT | \
      VK_COLOR_COMPONENT_A_BIT;
    return VkPipelineColorBlendAttachmentState{
      VK_TRUE, // blendEnable
      VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, // color[Src/Dst] BlendFactor
      VK_BLEND_OP_ADD,
      VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, // alpha[Src/Dst] BlendFactor
      VK_BLEND_OP_ADD,
      colorWriteAll
    };
  }
  inline VkPipelineColorBlendAttachmentState GetTransparentColorBlendAttachmentState()
  {
    const auto colorWriteAll = \
      VK_COLOR_COMPONENT_R_BIT | \
      VK_COLOR_COMPONENT_G_BIT | \
      VK_COLOR_COMPONENT_B_BIT | \
      VK_COLOR_COMPONENT_A_BIT;
    return VkPipelineColorBlendAttachmentState{
      VK_TRUE, // blendEnable
      VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // color[Src/Dst] BlendFactor
      VK_BLEND_OP_ADD,
      VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // alpha[Src/Dst] BlendFactor
      VK_BLEND_OP_ADD,
      colorWriteAll
    };
  }

  inline VkPipelineRasterizationStateCreateInfo GetDefaultRasterizerState( VkCullModeFlags cullmode = VK_CULL_MODE_NONE)
  {
    return VkPipelineRasterizationStateCreateInfo{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      nullptr, 0,
      VK_FALSE, // depthClampEnable
      VK_FALSE, // rasterizerDiscardEnable
      VK_POLYGON_MODE_FILL,
      cullmode,
      VK_FRONT_FACE_COUNTER_CLOCKWISE,
      VK_FALSE, // depthBiasEnable
      0.0f, 0.0f, 0.0f,
      1.0f // lineWidth
    };
  }
  inline VkPipelineDepthStencilStateCreateInfo GetDefaultDepthStencilState()
  {
    VkPipelineDepthStencilStateCreateInfo depthStencilCI{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      nullptr, 0,
      VK_TRUE, // DepthTestEnable
      VK_TRUE, // DepthWriteEnable
      VK_COMPARE_OP_LESS_OR_EQUAL,
      VK_FALSE,
      VK_FALSE,
      { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NEVER, 0, 0, 0 }, // front
      { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NEVER, 0, 0, 0 }, // back
      0.0f, 0.0f  // dpethBounds
    };
    return depthStencilCI;
  }
  inline VkPipelineInputAssemblyStateCreateInfo GetInputAssembly(VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
  {
    return VkPipelineInputAssemblyStateCreateInfo{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      nullptr, 0, topology,
      VK_FALSE,
    };
  }

  inline VkPipelineMultisampleStateCreateInfo GetNoMultisampleState()
  {
    return VkPipelineMultisampleStateCreateInfo{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      nullptr, 0,
      VK_SAMPLE_COUNT_1_BIT,
      VK_FALSE, // sampleShadingEnable
      0.0f, nullptr,
      VK_FALSE, VK_FALSE,
    };
  }

  inline VkPipelineShaderStageCreateInfo LoadShader(VkDevice device, const char* fileName, VkShaderStageFlagBits stage)
  {
    std::ifstream infile(fileName, std::ios::binary);
    std::vector<char> code;
    code.resize(uint32_t(infile.seekg(0, std::ifstream::end).tellg()));
    infile.seekg(0, std::ifstream::beg).read(code.data(), code.size());

    VkShaderModule module;
    VkShaderModuleCreateInfo ci{
      VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      nullptr, 0,
      code.size(),
      reinterpret_cast<uint32_t*>(code.data()),
    };
    auto result = vkCreateShaderModule(device, &ci, nullptr, &module);
    ThrowIfFailed(result, "vkCreateShaderModule Failed.");

    VkPipelineShaderStageCreateInfo shaderStageCI{
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      nullptr, 0,
      stage,
      module,
      "main",
      nullptr
    };
    return shaderStageCI;
  }
  
  inline void DestroyShaderModules(VkDevice device, std::vector<VkPipelineShaderStageCreateInfo>& modules)
  {
    for (auto& shader : modules)
    {
      vkDestroyShaderModule(device, shader.module, nullptr);
    }
    modules.clear();
  }

  inline VkViewport GetViewportFlipped(float width, float height)
  {
    return VkViewport{
      0.0f, height,
      width, height * -1.0f,
      0.0f, 1.0f
    };
  }

  inline VkWriteDescriptorSet PrepareWriteDescriptorSet(
    VkDescriptorSet descriptorSet, uint32_t dstBinding, VkDescriptorType type)
  {
    return VkWriteDescriptorSet{
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      nullptr,
      descriptorSet,  // dstSet
      // dstBinding, dstArrayElement, descriptorCount
      dstBinding, 0, 1,
      type,
      nullptr, nullptr, nullptr,
    };
  }


  inline VkRenderPass CreateRenderPass(VkDevice device, VkFormat colorFormat, VkFormat depthFormat)
  {
    std::array<VkAttachmentDescription, 2> attachments;
    attachments[0] = VkAttachmentDescription{
      0, colorFormat,
      VK_SAMPLE_COUNT_1_BIT,
      VK_ATTACHMENT_LOAD_OP_CLEAR,
      VK_ATTACHMENT_STORE_OP_STORE,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    attachments[1] = VkAttachmentDescription{
      0, depthFormat,
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
      &depthRef, 0, nullptr
    };
    VkRenderPassCreateInfo rpCI{
      VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      nullptr, 0,
      uint32_t(attachments.size()), attachments.data(),
      1, &subpassDesc,
      0, nullptr,
    };

    VkRenderPass renderPass;
    auto result = vkCreateRenderPass(device, &rpCI, nullptr, &renderPass);
    ThrowIfFailed(result, "vkCreateRenderPass Failed.");
    return renderPass;
  }

  inline VkRenderPass CreateRenderPassToRenderTarget(VkDevice device, VkFormat colorFormat, VkFormat depthFormat)
  {
    std::array<VkAttachmentDescription, 2> attachments;
    attachments[0] = VkAttachmentDescription{
      0, colorFormat,
      VK_SAMPLE_COUNT_1_BIT,
      VK_ATTACHMENT_LOAD_OP_CLEAR,
      VK_ATTACHMENT_STORE_OP_STORE,
      VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    attachments[1] = VkAttachmentDescription{
      0, depthFormat,
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
      &depthRef, 0, nullptr
    };
    VkRenderPassCreateInfo rpCI{
      VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      nullptr, 0,
      uint32_t(attachments.size()), attachments.data(),
      1, &subpassDesc,
      0, nullptr,
    };

    VkRenderPass renderPass;
    auto result = vkCreateRenderPass(device, &rpCI, nullptr, &renderPass);
    ThrowIfFailed(result, "vkCreateRenderPass Failed.");
    return renderPass;
  }

  inline VkDescriptorSetAllocateInfo CreateDescriptorSetAllocateInfo(
    VkDescriptorPool descriptorPool, VkDescriptorSetLayout layout)
  {
    return VkDescriptorSetAllocateInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      nullptr, descriptorPool, 1, &layout
    };
  }

  inline VkImageCreateInfo CreateEasyImageCreateInfo(
    VkFormat format, VkExtent3D extent, VkImageUsageFlags usageFlags,
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT )
  {
    return VkImageCreateInfo{
      VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      nullptr, 0,
      VK_IMAGE_TYPE_2D,
      format,
      extent,
      1, 1, samples,
      VK_IMAGE_TILING_OPTIMAL,
      usageFlags,
      VK_SHARING_MODE_EXCLUSIVE,
      0, nullptr,
      VK_IMAGE_LAYOUT_UNDEFINED
    };
  }
  inline VkWriteDescriptorSet CreateWriteDescriptorSet(
    VkDescriptorSet descriptorSet, uint32_t dstBinding, const VkDescriptorBufferInfo* pUboInfo)
  {
    return VkWriteDescriptorSet{
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      nullptr,
      descriptorSet,
      dstBinding, 0,
      1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      nullptr, pUboInfo, nullptr
    };
  }
  inline VkWriteDescriptorSet CreateWriteDescriptorSet(
    VkDescriptorSet descriptorSet, uint32_t dstBinding, const VkDescriptorImageInfo* pImageInfo)
  {
    return VkWriteDescriptorSet{
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      nullptr,
      descriptorSet,
      dstBinding, 0,
      1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      pImageInfo, nullptr, nullptr
    };
  }

  template<class T>
  T* GetApplication(GLFWwindow* window)
  {
    return reinterpret_cast<T*>(glfwGetWindowUserPointer(window));
  }
}
