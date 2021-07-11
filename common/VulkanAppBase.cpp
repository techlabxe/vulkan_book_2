#include "VulkanAppBase.h"
#include "VulkanBookUtil.h"

#include <vector>
#include <sstream>

static VkBool32 VKAPI_CALL DebugReportCallback(
  VkDebugReportFlagsEXT flags,
  VkDebugReportObjectTypeEXT objactTypes,
  uint64_t object,
  size_t	location,
  int32_t messageCode,
  const char* pLayerPrefix,
  const char* pMessage,
  void* pUserData)
{
  VkBool32 ret = VK_FALSE;
  if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT ||
    flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
  {
    ret = VK_TRUE;
  }
  std::stringstream ss;
  if (pLayerPrefix)
  {
    ss << "[" << pLayerPrefix << "] ";
  }
  ss << pMessage << std::endl;

  OutputDebugStringA(ss.str().c_str());

  return ret;
}


bool VulkanAppBase::OnSizeChanged(uint32_t width, uint32_t height)
{
  m_isMinimizedWindow = (width == 0 || height == 0);
  if (m_isMinimizedWindow)
  {
    return false;
  }
  vkDeviceWaitIdle(m_device);

  auto format = m_swapchain->GetSurfaceFormat().format;
  // スワップチェインを作り直す.
  m_swapchain->Prepare(m_physicalDevice, m_gfxQueueIndex, width, height, format);
  return true;
}

uint32_t VulkanAppBase::GetMemoryTypeIndex(uint32_t requestBits, VkMemoryPropertyFlags requestProps) const
{
  uint32_t result = ~0u;
  for (uint32_t i = 0; i < m_physicalMemProps.memoryTypeCount; ++i)
  {
    if (requestBits & 1)
    {
      const auto& types = m_physicalMemProps.memoryTypes[i];
      if ((types.propertyFlags & requestProps) == requestProps)
      {
        result = i;
        break;
      }
    }
    requestBits >>= 1;
  }
  return result;
}

void VulkanAppBase::SwitchFullscreen(GLFWwindow* window)
{
  static int lastWindowPosX, lastWindowPosY;
  static int lastWindowSizeW, lastWindowSizeH;

  auto monitor = glfwGetPrimaryMonitor();
  const auto mode = glfwGetVideoMode(monitor);

#if 1 // 現在のモニターに合わせたサイズへ変更.
  if (!m_isFullscreen)
  {
    // to fullscreen
    glfwGetWindowPos(window, &lastWindowPosX, &lastWindowPosY);
    glfwGetWindowSize(window, &lastWindowSizeW, &lastWindowSizeH);
    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
  }
  else
  {
    // to windowmode
    glfwSetWindowMonitor(window, nullptr, lastWindowPosX, lastWindowPosY, lastWindowSizeW, lastWindowSizeH, mode->refreshRate);
  }
#else
  // 指定された解像度へモニターを変更.
  if (!m_isFullscreen)
  {
    // to fullscreen
    glfwGetWindowPos(window, &lastWindowPosX, &lastWindowPosY);
    glfwGetWindowSize(window, &lastWindowSizeW, &lastWindowSizeH);
    glfwSetWindowMonitor(window, monitor, 0, 0, lastWindowSizeW, lastWindowSizeH, mode->refreshRate);
  }
  else
  {
    // to windowmode
    glfwSetWindowMonitor(window, nullptr, lastWindowPosX, lastWindowPosY, lastWindowSizeW, lastWindowSizeH, mode->refreshRate);
  }
#endif
  m_isFullscreen = !m_isFullscreen;
}

void VulkanAppBase::Initialize(GLFWwindow* window, VkFormat format, bool isFullscreen)
{
  m_window = window;
  CreateInstance();

  // 物理デバイスの選択.
  uint32_t count;
  vkEnumeratePhysicalDevices(m_vkInstance, &count, nullptr);
  std::vector<VkPhysicalDevice> physicalDevices(count);
  vkEnumeratePhysicalDevices(m_vkInstance, &count, physicalDevices.data());
  // 最初のデバイスを使用する.
  m_physicalDevice = physicalDevices[0];
  vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_physicalMemProps);

  // グラフィックスのキューインデックス取得.
  SelectGraphicsQueue();

#ifdef _DEBUG
  EnableDebugReport();
#endif
  // 論理デバイスの生成.
  CreateDevice();

  // コマンドプールの生成.
  CreateCommandPool();

  VkSurfaceKHR surface;
  auto result = glfwCreateWindowSurface(m_vkInstance, window, nullptr, &surface);
  ThrowIfFailed(result, "glfwCreateWindowSurface Failed.");

  // スワップチェインの生成.
  m_swapchain = std::make_unique<Swapchain>(m_vkInstance, m_device, surface);

  int width, height;
  glfwGetWindowSize(window, &width, &height);
  m_swapchain->Prepare(
    m_physicalDevice, m_gfxQueueIndex,
    uint32_t(width), uint32_t(height),
    format
  );
  auto imageCount = m_swapchain->GetImageCount();
  auto extent = m_swapchain->GetSurfaceExtent();

  VkSemaphoreCreateInfo semCI{
    VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    nullptr, 0,
  };

  vkCreateSemaphore(m_device, &semCI, nullptr, &m_renderCompletedSem);
  vkCreateSemaphore(m_device, &semCI, nullptr, &m_presentCompletedSem);

  // ディスクリプタプールの生成.
  CreateDescriptorPool();

  m_renderPassStore = std::make_unique<RenderPassRegistry>([&](VkRenderPass renderPass) { vkDestroyRenderPass(m_device, renderPass, nullptr); });
  m_descriptorSetLayoutStore = std::make_unique<DescriptorSetLayoutManager>([&](VkDescriptorSetLayout layout) { vkDestroyDescriptorSetLayout(m_device, layout, nullptr); });
  m_pipelineLayoutStore = std::make_unique<PipelineLayoutManager>([&](VkPipelineLayout layout) { vkDestroyPipelineLayout(m_device, layout, nullptr); });

  Prepare();
}

void VulkanAppBase::Terminate()
{
  if (m_device != VK_NULL_HANDLE)
  {
    vkDeviceWaitIdle(m_device);
  }
  Cleanup();
  if (m_swapchain)
  {
    m_swapchain->Cleanup();
  }
#ifdef _DEBUG
  DisableDebugReport();
#endif

  m_renderPassStore->Cleanup();
  m_descriptorSetLayoutStore->Cleanup();
  m_pipelineLayoutStore->Cleanup();

  vkDestroySemaphore(m_device, m_renderCompletedSem, nullptr);
  vkDestroySemaphore(m_device, m_presentCompletedSem, nullptr);

  vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
  vkDestroyCommandPool(m_device, m_commandPool, nullptr);
  vkDestroyDevice(m_device, nullptr);
  vkDestroyInstance(m_vkInstance, nullptr);
  m_commandPool = VK_NULL_HANDLE;
  m_device = VK_NULL_HANDLE;
  m_vkInstance = VK_NULL_HANDLE;
}

VulkanAppBase::BufferObject VulkanAppBase::CreateBuffer(uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props)
{
  BufferObject obj;
  VkBufferCreateInfo bufferCI{
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    nullptr, 0,
    size, usage,
    VK_SHARING_MODE_EXCLUSIVE,
    0, nullptr
  };
  auto result = vkCreateBuffer(m_device, &bufferCI, nullptr, &obj.buffer);
  ThrowIfFailed(result, "vkCreateBuffer Failed.");

  // メモリ量の算出.
  VkMemoryRequirements reqs;
  vkGetBufferMemoryRequirements(m_device, obj.buffer, &reqs);
  VkMemoryAllocateInfo info{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    nullptr,
    reqs.size,
    GetMemoryTypeIndex(reqs.memoryTypeBits, props)
  };
  vkAllocateMemory(m_device, &info, nullptr, &obj.memory);
  vkBindBufferMemory(m_device, obj.buffer, obj.memory, 0);
  return obj;
}

VulkanAppBase::ImageObject VulkanAppBase::CreateTexture(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage)
{
  ImageObject obj;
  VkImageCreateInfo imageCI{
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    nullptr, 0,
    VK_IMAGE_TYPE_2D,
    format, { width, height, 1 },
    1, 1, VK_SAMPLE_COUNT_1_BIT,
    VK_IMAGE_TILING_OPTIMAL,
    usage,
    VK_SHARING_MODE_EXCLUSIVE,
    0, nullptr,
    VK_IMAGE_LAYOUT_UNDEFINED
  };
  auto result = vkCreateImage(m_device, &imageCI, nullptr, &obj.image);
  ThrowIfFailed(result, "vkCreateImage Failed.");

  // メモリ量の算出.
  VkMemoryRequirements reqs;
  vkGetImageMemoryRequirements(m_device, obj.image, &reqs);
  VkMemoryAllocateInfo info{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    nullptr,
    reqs.size,
    GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };
  result = vkAllocateMemory(m_device, &info, nullptr, &obj.memory);
  ThrowIfFailed(result, "vkAllocateMemory Failed.");
  vkBindImageMemory(m_device, obj.image, obj.memory, 0);

  VkImageAspectFlags imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;
  if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
  {
    imageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  VkImageViewCreateInfo viewCI{
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    nullptr, 0,
    obj.image,
    VK_IMAGE_VIEW_TYPE_2D,
    imageCI.format,
    book_util::DefaultComponentMapping(),
    { imageAspect, 0, 1, 0, 1}
  };
  result = vkCreateImageView(m_device, &viewCI, nullptr, &obj.view);
  ThrowIfFailed(result, "vkCreateImageView Failed.");
  return obj;
}

void VulkanAppBase::DestroyBuffer(BufferObject bufferObj)
{
  vkDestroyBuffer(m_device, bufferObj.buffer, nullptr);
  vkFreeMemory(m_device, bufferObj.memory, nullptr);
}

void VulkanAppBase::DestroyImage(ImageObject imageObj)
{
  vkDestroyImage(m_device, imageObj.image, nullptr);
  vkFreeMemory(m_device, imageObj.memory, nullptr);
  if (imageObj.view != VK_NULL_HANDLE)
  {
    vkDestroyImageView(m_device, imageObj.view, nullptr);
  }
}

VkFramebuffer VulkanAppBase::CreateFramebuffer(
  VkRenderPass renderPass, uint32_t width, uint32_t height, uint32_t viewCount, VkImageView* views)
{
  VkFramebufferCreateInfo fbCI{
    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    nullptr, 0,
    renderPass,
    viewCount, views,
    width, height,
    1,
  };
  VkFramebuffer framebuffer;
  auto result = vkCreateFramebuffer(m_device, &fbCI, nullptr, &framebuffer);
  ThrowIfFailed(result, "vkCreateFramebuffer Failed.");
  return framebuffer;
}
void VulkanAppBase::DestroyFramebuffers(uint32_t count, VkFramebuffer* framebuffers)
{
  for (uint32_t i = 0; i < count; ++i)
  {
    vkDestroyFramebuffer(m_device, framebuffers[i], nullptr);
  }
}

VkCommandBuffer VulkanAppBase::CreateCommandBuffer()
{
  VkCommandBufferAllocateInfo commandAI{
     VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    nullptr, m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    1
  };
  VkCommandBufferBeginInfo beginInfo{
  VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };

  VkCommandBuffer command;
  vkAllocateCommandBuffers(m_device, &commandAI, &command);
  vkBeginCommandBuffer(command, &beginInfo);
  return command;
}

void VulkanAppBase::FinishCommandBuffer(VkCommandBuffer command)
{
  auto result = vkEndCommandBuffer(command);
  ThrowIfFailed(result, "vkEndCommandBuffer Failed.");
  VkFence fence;
  VkFenceCreateInfo fenceCI{
    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    nullptr, 0
  };
  result = vkCreateFence(m_device, &fenceCI, nullptr, &fence);

  VkSubmitInfo submitInfo{
    VK_STRUCTURE_TYPE_SUBMIT_INFO,
    nullptr,
    0, nullptr,
    nullptr,
    1, &command,
    0, nullptr,
  };
  vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);
  vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(m_device, fence, nullptr);
}

VkRect2D VulkanAppBase::GetSwapchainRenderArea() const
{
  return VkRect2D{
    VkOffset2D{0,0},
    m_swapchain->GetSurfaceExtent()
  };
}

std::vector<VulkanAppBase::BufferObject> VulkanAppBase::CreateUniformBuffers(uint32_t bufferSize, uint32_t imageCount)
{
  std::vector<BufferObject> buffers(imageCount);
  for (auto& b : buffers)
  {
    VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    b = CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, props);
  }
  return buffers;
}

void VulkanAppBase::WriteToHostVisibleMemory(VkDeviceMemory memory, uint32_t size, const void* pData)
{
  void* p;
  vkMapMemory(m_device, memory, 0, VK_WHOLE_SIZE, 0, &p);
  memcpy(p, pData, size);
  vkUnmapMemory(m_device, memory);
}

void VulkanAppBase::AllocateCommandBufferSecondary(uint32_t count, VkCommandBuffer* pCommands)
{
  VkCommandBufferAllocateInfo commandAI{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    nullptr, m_commandPool,
    VK_COMMAND_BUFFER_LEVEL_SECONDARY, count
  };
  auto result = vkAllocateCommandBuffers(m_device, &commandAI, pCommands);
  ThrowIfFailed(result, "vkAllocateCommandBuffers Faield.");
}

void VulkanAppBase::FreeCommandBufferSecondary(uint32_t count, VkCommandBuffer* pCommands)
{
  vkFreeCommandBuffers(m_device, m_commandPool, count, pCommands);
}

void VulkanAppBase::TransferStageBufferToImage(
  const BufferObject& srcBuffer, const ImageObject& dstImage, const VkBufferImageCopy* region)
{ 
  VkImageMemoryBarrier imb{
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
    0, VK_ACCESS_TRANSFER_WRITE_BIT,
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
    dstImage.image,
    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
  };

  // Staging から転送.
  auto command = CreateCommandBuffer();
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 0, nullptr,
    0, nullptr, 1, &imb);

  vkCmdCopyBufferToImage(
    command, 
    srcBuffer.buffer, dstImage.image,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, region);
  imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  imb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    0, 0, nullptr,
    0, nullptr,
    1, &imb);
  FinishCommandBuffer(command);
}

void VulkanAppBase::CreateInstance()
{
  VkApplicationInfo appinfo{};
  appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appinfo.pApplicationName = "VulkanBook2";
  appinfo.pEngineName = "VulkanBook2";
  appinfo.apiVersion = VK_API_VERSION_1_1;
  appinfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);

  // 拡張情報の取得.
  uint32_t count;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> props(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data());
  std::vector<const char*> extensions;
  extensions.reserve(count);
  for (const auto& v : props)
  {
    extensions.push_back(v.extensionName);
  }

  VkInstanceCreateInfo instanceCI{};
  instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceCI.enabledExtensionCount = count;
  instanceCI.ppEnabledExtensionNames = extensions.data();
  instanceCI.pApplicationInfo = &appinfo;
#ifdef _DEBUG
  // デバッグビルド時には検証レイヤーを有効化
  // デバッグビルド時には検証レイヤーを有効化
  const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
  if (VK_HEADER_VERSION_COMPLETE < VK_MAKE_VERSION(1, 1, 106)) {
	  // "VK_LAYER_LUNARG_standard_validation" は廃止になっているが昔の Vulkan SDK では動くので対処しておく.
	  layers[0] = "VK_LAYER_LUNARG_standard_validation";
  }
  instanceCI.enabledLayerCount = 1;
  instanceCI.ppEnabledLayerNames = layers;
#endif
  auto result = vkCreateInstance(&instanceCI, nullptr, &m_vkInstance);
  ThrowIfFailed(result, "vkCreateInstance Failed.");
}

void VulkanAppBase::SelectGraphicsQueue()
{
  // グラフィックスキューのインデックス値を取得.
  uint32_t queuePropCount;
  vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queuePropCount, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilyProps(queuePropCount);
  vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queuePropCount, queueFamilyProps.data());
  uint32_t graphicsQueue = ~0u;
  for (uint32_t i = 0; i < queuePropCount; ++i)
  {
    if (queueFamilyProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
    {
      graphicsQueue = i; break;
    }
  }
  m_gfxQueueIndex = graphicsQueue;
}

void VulkanAppBase::CreateDevice()
{
  const float defaultQueuePriority(1.0f);
  VkDeviceQueueCreateInfo devQueueCI{
    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    nullptr, 0,
    m_gfxQueueIndex,
    1, &defaultQueuePriority
  };
  uint32_t count;
  vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> deviceExtensions(count);
  vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &count, deviceExtensions.data());

  std::vector<const char*> extensions;
  extensions.reserve(count);
  for (const auto& v : deviceExtensions)
  {
    extensions.push_back(v.extensionName);
  }
  VkDeviceCreateInfo deviceCI{
    VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    nullptr, 0,
    1, &devQueueCI,
    0, nullptr,
    count, extensions.data(),
    nullptr
  };
  auto result = vkCreateDevice(m_physicalDevice, &deviceCI, nullptr, &m_device);
  ThrowIfFailed(result, "vkCreateDevice Failed.");

  vkGetDeviceQueue(m_device, m_gfxQueueIndex, 0, &m_deviceQueue);
}

void VulkanAppBase::CreateCommandPool()
{
  VkCommandPoolCreateInfo cmdPoolCI{
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    nullptr,
    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    m_gfxQueueIndex
  };
  auto result = vkCreateCommandPool(m_device, &cmdPoolCI, nullptr, &m_commandPool);
  ThrowIfFailed(result, "vkCreateCommandPool Failed.");
}

void VulkanAppBase::CreateDescriptorPool()
{
  VkResult result;
  VkDescriptorPoolSize poolSize[] = {
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
  };
  VkDescriptorPoolCreateInfo descPoolCI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    nullptr,  VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    1000 * _countof(poolSize), // maxSets
    _countof(poolSize), poolSize,
  };
  result = vkCreateDescriptorPool(m_device, &descPoolCI, nullptr, &m_descriptorPool);
  ThrowIfFailed(result, "vkCreateDescriptorPool Failed.");
}

VkDeviceMemory VulkanAppBase::AllocateMemory(VkBuffer buffer, VkMemoryPropertyFlags memProps)
{
  VkDeviceMemory memory;
  VkMemoryRequirements reqs;
  vkGetBufferMemoryRequirements(m_device, buffer, &reqs);
  VkMemoryAllocateInfo info{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    nullptr,
    reqs.size,
    GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };
  auto result = vkAllocateMemory(m_device, &info, nullptr, &memory);
  ThrowIfFailed(result, "vkAllocateMemory Failed.");
  return memory;
}

VkDeviceMemory VulkanAppBase::AllocateMemory(VkImage image, VkMemoryPropertyFlags memProps)
{
  VkDeviceMemory memory;
  VkMemoryRequirements reqs;
  vkGetImageMemoryRequirements(m_device, image, &reqs);
  VkMemoryAllocateInfo info{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    nullptr,
    reqs.size,
    GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };
  auto result = vkAllocateMemory(m_device, &info, nullptr, &memory);
  ThrowIfFailed(result, "vkAllocateMemory Failed.");
  return memory;
}


#define GetInstanceProcAddr(FuncName) \
  m_##FuncName = reinterpret_cast<PFN_##FuncName>(vkGetInstanceProcAddr(m_vkInstance, #FuncName))

void VulkanAppBase::EnableDebugReport()
{
  GetInstanceProcAddr(vkCreateDebugReportCallbackEXT);
  GetInstanceProcAddr(vkDebugReportMessageEXT);
  GetInstanceProcAddr(vkDestroyDebugReportCallbackEXT);

  VkDebugReportFlagsEXT flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;

  VkDebugReportCallbackCreateInfoEXT drcCI{};
  drcCI.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
  drcCI.flags = flags;
  drcCI.pfnCallback = &DebugReportCallback;
  auto result = m_vkCreateDebugReportCallbackEXT(m_vkInstance, &drcCI, nullptr, &m_debugReport);
  ThrowIfFailed(result, "vkCreateDebugReportCallback Failed.");
}

void VulkanAppBase::DisableDebugReport()
{
  if (m_vkDestroyDebugReportCallbackEXT)
  {
    m_vkDestroyDebugReportCallbackEXT(m_vkInstance, m_debugReport, nullptr);
  }
}

void VulkanAppBase::MsgLoopMinimizedWindow()
{
  int width, height;
  do
  {
    glfwGetWindowSize(m_window, &width, &height);
    glfwWaitEvents();
  } while (width == 0 || height == 0);
}
