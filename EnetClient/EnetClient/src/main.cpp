#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

// GLFW
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// ImGui
#include <imgui.h>

#include "GameClient.h"
#include "IconsLucide.h"
#include "VulkanResourceHandler.h"

// Constants
constexpr int MAX_FRAMES_IN_FLIGHT = 3;
constexpr float BASE_FONT_SIZE = 16.0f;
constexpr uint32_t WIDTH = 1024;
constexpr uint32_t HEIGHT = 768;
const char* APP_NAME = "MMO Client";

void initWindow(GLFWwindow*& window)
{
	Logger& logger = Logger::getInstance();

	// Set error callback to catch GLFW errors
	glfwSetErrorCallback([](int error, const char* description) { Logger::getInstance().error("GLFW error: " + std::to_string(error) + " - " + description); });

	// Initialize GLFW
	logger.debug("Initializing GLFW...");
	if (!glfwInit())
	{
		logger.error("Failed to initialize GLFW");
		throw std::runtime_error("Failed to initialize GLFW");
	}

	logger.debug("GLFW initialized successfully");

	// Check for Vulkan support
	if (!glfwVulkanSupported())
	{
		logger.error("GLFW: Vulkan not supported on this system");
		glfwTerminate();
		throw std::runtime_error("Vulkan not supported on this system");
	}

	// Configure GLFW window properties
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Using Vulkan, not OpenGL
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Hide window until fully configured
	glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);

	// Create GLFW window
	logger.debug("Creating window with dimensions " + std::to_string(WIDTH) + "x" + std::to_string(HEIGHT));
	window = glfwCreateWindow(WIDTH, HEIGHT, APP_NAME, nullptr, nullptr);
	if (!window)
	{
		logger.error("Failed to create GLFW window with initial parameters, trying fallback settings");

		// Try with different settings as fallback
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
		glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

		window = glfwCreateWindow(800, 600, APP_NAME, nullptr, nullptr);
		if (!window)
		{
			logger.error("Failed to create GLFW window even with fallback settings");
			glfwTerminate();
			throw std::runtime_error("Failed to create GLFW window after multiple attempts");
		}
	}

	// Set window size constraints
	logger.debug("Setting window size constraints");
	glfwSetWindowSizeLimits(window, 800, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);

	// Get and log monitor information
	int monitorCount;
	GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
	logger.debug("Found " + std::to_string(monitorCount) + " monitors");

	// Center window on primary monitor
	GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
	if (primaryMonitor)
	{
		const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
		if (mode)
		{
			// Log monitor details
			logger.debug("Primary monitor: " + std::to_string(mode->width) + "x" + std::to_string(mode->height) + " @ " + std::to_string(mode->refreshRate) + "Hz");

			int xpos = (mode->width - WIDTH) / 2;
			int ypos = (mode->height - HEIGHT) / 2;
			glfwSetWindowPos(window, xpos, ypos);
		}
		else
		{
			logger.warning("Could not get video mode for primary monitor");
		}
	}
	else
	{
		logger.warning("Could not find primary monitor");
	}

	// Add window size callback
	glfwSetFramebufferSizeCallback(window,
	        [](GLFWwindow* w, int width, int height)
	        {
		        // This will be called when the window is resized
		        Logger::getInstance().debug("Window resized: " + std::to_string(width) + "x" + std::to_string(height));
	        });

	// Make window visible after configuration
	glfwShowWindow(window);
	logger.debug("Window created successfully");
}

// Map VulkanError to human-readable strings using C++23 features
constexpr std::string_view getErrorString(VulkanError error)
{
	switch (error)
	{
		case VulkanError::InstanceCreationFailed:
			return "Failed to create Vulkan instance";
		case VulkanError::SurfaceCreationFailed:
			return "Failed to create window surface";
		case VulkanError::PhysicalDeviceSelectionFailed:
			return "Failed to select physical device";
		case VulkanError::LogicalDeviceCreationFailed:
			return "Failed to create logical device";
		case VulkanError::SwapchainCreationFailed:
			return "Failed to create swapchain";
		case VulkanError::RenderPassCreationFailed:
			return "Failed to create render pass";
		case VulkanError::FramebufferCreationFailed:
			return "Failed to create framebuffers";
		case VulkanError::CommandPoolCreationFailed:
			return "Failed to create command pool";
		case VulkanError::CommandBufferAllocationFailed:
			return "Failed to allocate command buffers";
		case VulkanError::SyncObjectCreationFailed:
			return "Failed to create synchronization objects";
		case VulkanError::DescriptorPoolCreationFailed:
			return "Failed to create descriptor pool";
		case VulkanError::ImGuiInitializationFailed:
			return "Failed to initialize ImGui";
		case VulkanError::SwapchainAcquisitionFailed:
			return "Failed to acquire swapchain image";
		case VulkanError::CommandBufferBeginFailed:
			return "Failed to begin command buffer";
		case VulkanError::DrawCommandSubmissionFailed:
			return "Failed to submit draw commands";
		case VulkanError::PresentationFailed:
			return "Failed to present frame";
		default:
			return "Unknown Vulkan error";
	}
}

// Main function with modern C++ and improved error handling
int main(int argc, char* argv[])
{
	bool isDebuggerPresent = IsDebuggerPresent();
	Constants::Runtime::IsDebuggerPresent = isDebuggerPresent;

	// Get the logger
	Logger& logger = Logger::getInstance();
	logger.setLogLevel(LogLevel::TRACE);

	// Create game client
	std::shared_ptr<GameClient> client = std::make_shared<GameClient>();

	try
	{
		// Initialize GLFW
		GLFWwindow* window;
		initWindow(window);

		// Create our Vulkan resource handler
		VulkanResourceHandler vulkan(APP_NAME);

		// Initialize all Vulkan resources with proper error handling
		if (auto result = vulkan.initialize(window, WIDTH, HEIGHT, MAX_FRAMES_IN_FLIGHT); !result)
		{
			logger.error(std::format("Vulkan initialization failed: {}", static_cast<std::string_view>(getErrorString(result.error()))));
			glfwTerminate();
			return EXIT_FAILURE;
		}

		// Apply theme
		client->applyTheme();

		// Main loop
		uint32_t currentFrame = 0;
		bool applicationRunning = true;

		while (applicationRunning && !glfwWindowShouldClose(window))
		{
			glfwPollEvents();

			// Update network
			client->updateNetwork();

			// Begin frame rendering with proper error handling
			uint32_t imageIndex;
			if (auto result = vulkan.beginFrame(imageIndex, currentFrame); !result)
			{
				if (result.error() == VulkanError::SwapchainAcquisitionFailed)
				{
					// Try to recreate the swapchain - this is a recoverable error
					int width, height;
					glfwGetFramebufferSize(window, &width, &height);

					if (width > 0 && height > 0)
					{
						if (auto recreateResult = vulkan.recreateSwapchain(width, height); !recreateResult)
						{
							logger.error("Failed to recreate swapchain, exiting...");
							applicationRunning = false;
						}
						continue; // Skip this frame and try again
					}
					else
					{
						// Window was minimized or otherwise invalid - just wait
						glfwWaitEvents();
						continue;
					}
				}
				else
				{
					// Unrecoverable error
					logger.error(std::format("Failed to begin frame: {}", static_cast<std::string_view>(getErrorString(result.error()))));
					applicationRunning = false;
					continue;
				}
			}

			// Create the docking environment
			ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

			// Draw client UI
			client->drawUI();

			// End frame rendering with proper error handling
			if (auto result = vulkan.endFrame(imageIndex, currentFrame); !result)
			{
				if (result.error() == VulkanError::PresentationFailed)
				{
					// Try to recreate the swapchain - this is a recoverable error
					int width, height;
					glfwGetFramebufferSize(window, &width, &height);

					if (width > 0 && height > 0)
					{
						if (auto recreateResult = vulkan.recreateSwapchain(width, height); !recreateResult)
						{
							logger.error("Failed to recreate swapchain, exiting...");
							applicationRunning = false;
						}
					}
					else
					{
						// Window was minimized or otherwise invalid - just wait
						glfwWaitEvents();
					}
				}
				else
				{
					// Unrecoverable error
					logger.error(std::format("Failed to end frame: {}", static_cast<std::string_view>(getErrorString(result.error()))));
					applicationRunning = false;
				}
			}

			// Update frame index
			currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
		}

		// Clean up GLFW
		glfwDestroyWindow(window);
		glfwTerminate();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
