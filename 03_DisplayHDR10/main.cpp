#include "DisplayHDR10App.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "VulkanBookUtil.h"

const int WindowWidth = 800, WindowHeight = 600;
const char* AppTitle = "DisplayHDR10";

static void KeyboardInputCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
  auto pApp = book_util::GetApplication<VulkanAppBase>(window);
  if (pApp == nullptr)
  {
    return;
  }
  switch (action)
  {
  case GLFW_PRESS:
    if (key == GLFW_KEY_ESCAPE)
    {
      glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    if (key == GLFW_KEY_ENTER && mods == GLFW_MOD_ALT)
    {
      pApp->SwitchFullscreen(window);
    }
    break;

  default:
    break;
  }
}
static void MouseMoveCallback(GLFWwindow* window, double x, double y)
{
  static int lastPosX, lastPosY;
  auto pApp = book_util::GetApplication< VulkanAppBase>(window);
  if (pApp == nullptr)
  {
    return;
  }
  int dx = int(x) - lastPosX;
  int dy = int(y) - lastPosY;
  pApp->OnMouseMove(dx, dy);
  lastPosX = int(x);
  lastPosY = int(y);
}
static void MouseInputCallback(GLFWwindow* window, int button, int action, int mods)
{
  auto pApp = book_util::GetApplication< VulkanAppBase>(window);
  if (pApp == nullptr)
  {
    return;
  }
  if (action == GLFW_PRESS)
  {
    pApp->OnMouseButtonDown(button);
  }
  if (action == GLFW_RELEASE)
  {
    pApp->OnMouseButtonUp(button);
  }
}
static void MouseWheelCallback(GLFWwindow* window, double xoffset, double yoffset)
{
  auto pApp = book_util::GetApplication< VulkanAppBase>(window);
  if (pApp == nullptr)
  {
    return;
  }
}
static void WindowResizeCallback(GLFWwindow* window, int width, int height)
{
  auto pApp = book_util::GetApplication< VulkanAppBase>(window);
  if (pApp == nullptr)
  {
    return;
  }
  pApp->OnSizeChanged(width, height);
}

int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  auto window = glfwCreateWindow(WindowWidth, WindowHeight, AppTitle, nullptr, nullptr);

  // 各種コールバック登録.
  glfwSetKeyCallback(window, KeyboardInputCallback);
  glfwSetMouseButtonCallback(window, MouseInputCallback);
  glfwSetCursorPosCallback(window, MouseMoveCallback);
  glfwSetScrollCallback(window, MouseWheelCallback);
  glfwSetWindowSizeCallback(window, WindowResizeCallback);

  DisplayHDR10App theApp;
  glfwSetWindowUserPointer(window, &theApp);

  try
  {
    VkFormat surfaceFormat = VK_FORMAT_B8G8R8A8_UNORM;
    surfaceFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    theApp.Initialize(window, surfaceFormat, false);
    while (glfwWindowShouldClose(window) == GLFW_FALSE)
    {
      glfwPollEvents();
      theApp.Render();
    }
    theApp.Terminate();
  }
  catch (std::runtime_error e)
  {
    OutputDebugStringA(e.what());
    OutputDebugStringA("\n");
  }
  glfwTerminate();
  return 0;
}

