#pragma once

#include <concepts>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

// GLFW
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// Vulkan Bootstrap
#include <VkBootstrap.h>

// ImGui
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "Logger.h"

// Error handling
enum class VulkanError
{
	InstanceCreationFailed,
	SurfaceCreationFailed,
	PhysicalDeviceSelectionFailed,
	LogicalDeviceCreationFailed,
	SwapchainCreationFailed,
	RenderPassCreationFailed,
	FramebufferCreationFailed,
	CommandPoolCreationFailed,
	CommandBufferAllocationFailed,
	SyncObjectCreationFailed,
	DescriptorPoolCreationFailed,
	ImGuiInitializationFailed,
	SwapchainAcquisitionFailed,
	CommandBufferBeginFailed,
	DrawCommandSubmissionFailed,
	PresentationFailed,
	AllocatorCreationFailed
};

// Font loading structures
struct FontConfig
{
	std::string path;
	float size;
	const ImWchar* ranges;
	ImFontConfig* config;
};

/**
 * A class to manage Vulkan resources with proper lifetime management
 */
class VulkanResourceHandler
{
public:
	explicit VulkanResourceHandler(std::string_view appName);
	~VulkanResourceHandler();

	// Prevent copying and moving
	VulkanResourceHandler(const VulkanResourceHandler&) = delete;
	VulkanResourceHandler& operator=(const VulkanResourceHandler&) = delete;
	VulkanResourceHandler(VulkanResourceHandler&&) = delete;
	VulkanResourceHandler& operator=(VulkanResourceHandler&&) = delete;

	/**
     * Initialize all Vulkan resources
     * @param window GLFW window handle
     * @param width Initial window width
     * @param height Initial window height
     * @param maxFramesInFlight Maximum number of frames in flight
     * @return Expected containing nothing on success, or a VulkanError on failure
     */
	[[nodiscard]]
	std::expected<void, VulkanError> initialize(GLFWwindow* window, uint32_t width, uint32_t height, uint32_t maxFramesInFlight);

	/**
     * Window resize handling
     * @param width New window width
     * @param height New window height
     * @return Expected containing nothing on success, or a VulkanError on failure
     */
	[[nodiscard]]
	std::expected<void, VulkanError> recreateSwapchain(uint32_t width, uint32_t height);

	/**
     * Begin frame rendering
     * @param imageIndex Out parameter for swapchain image index
     * @param currentFrame Current frame index
     * @return Expected containing nothing on success, or a VulkanError on failure
     */
	[[nodiscard]]
	std::expected<void, VulkanError> beginFrame(uint32_t& imageIndex, uint32_t currentFrame);

	/**
     * End frame rendering
     * @param imageIndex Swapchain image index
     * @param currentFrame Current frame index
     * @return Expected containing nothing on success, or a VulkanError on failure
     */
	[[nodiscard]]
	std::expected<void, VulkanError> endFrame(uint32_t imageIndex, uint32_t currentFrame);

	// Getters
	[[nodiscard]] VkInstance getInstance() const noexcept
	{
		return m_instance.instance;
	}

	[[nodiscard]] VkDevice getDevice() const noexcept
	{
		return m_device.device;
	}

	[[nodiscard]] VkPhysicalDevice getPhysicalDevice() const noexcept
	{
		return m_device.physical_device;
	}

	[[nodiscard]] VkQueue getGraphicsQueue() const noexcept
	{
		return m_graphicsQueue;
	}

	[[nodiscard]] VkQueue getPresentQueue() const noexcept
	{
		return m_presentQueue;
	}

	[[nodiscard]] VkSwapchainKHR getSwapchain() const noexcept
	{
		return m_swapchain.swapchain;
	}

	[[nodiscard]] VkRenderPass getRenderPass() const noexcept
	{
		return m_renderPass;
	}

	[[nodiscard]] VkCommandBuffer getCurrentCommandBuffer() const noexcept
	{
		return m_currentCommandBuffer;
	}

	[[nodiscard]] VkCommandPool getCommandPool() const noexcept
	{
		return m_commandPool;
	}

	[[nodiscard]] VkDescriptorPool getDescriptorPool() const noexcept
	{
		return m_descriptorPool;
	}

	[[nodiscard]] uint32_t getSwapchainImageCount() const noexcept
	{
		return static_cast<uint32_t>(m_swapchainImages.size());
	}

	[[nodiscard]] VkExtent2D getSwapchainExtent() const noexcept
	{
		return m_swapchain.extent;
	}

	[[nodiscard]] vkb::DispatchTable& getDispatchTable() noexcept
	{
		return m_dispatchTable;
	}

	[[nodiscard]] vkb::InstanceDispatchTable& getInstanceDispatchTable() noexcept
	{
		return m_instanceDispatchTable;
	}

private:
	std::string m_appName;
	Logger& m_logger = Logger::getInstance();

	// VkBootstrap core objects
	vkb::Instance m_instance;
	vkb::InstanceDispatchTable m_instanceDispatchTable;
	vkb::Device m_device;
	vkb::DispatchTable m_dispatchTable;
	vkb::Swapchain m_swapchain;

	// Vulkan core objects
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkQueue m_graphicsQueue = VK_NULL_HANDLE;
	VkQueue m_presentQueue = VK_NULL_HANDLE;
	uint32_t m_graphicsQueueFamily = 0;

	// Rendering objects
	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkCommandPool m_commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_commandBuffers;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> m_framebuffers;

	// Swapchain resources
	std::vector<VkImage> m_swapchainImages;
	std::vector<VkImageView> m_swapchainImageViews;

	// Synchronization objects
	std::vector<VkSemaphore> m_imageAvailableSemaphores;
	std::vector<VkSemaphore> m_renderFinishedSemaphores;
	std::vector<VkFence> m_inFlightFences;
	std::vector<VkFence> m_imagesInFlight;

	// Current state
	GLFWwindow* m_window = nullptr;
	VkCommandBuffer m_currentCommandBuffer = VK_NULL_HANDLE;
	bool m_usesDynamicRendering = false;

	// Cleanup tracking
	struct ResourceCleanupFunc
	{
		std::function<void()> cleanup;
		int priority; // Higher priority means earlier cleanup
	};

	std::vector<ResourceCleanupFunc> m_cleanupFunctions;

	// Initialization methods
	[[nodiscard]] std::expected<void, VulkanError> initInstance();
	[[nodiscard]] std::expected<void, VulkanError> initSurface();
	[[nodiscard]] std::expected<void, VulkanError> initDevice();
	[[nodiscard]] std::expected<void, VulkanError> initAllocator();
	[[nodiscard]] std::expected<void, VulkanError> initSwapchain(uint32_t width, uint32_t height);
	[[nodiscard]] std::expected<void, VulkanError> initRenderPass();
	[[nodiscard]] std::expected<void, VulkanError> initFramebuffers();
	[[nodiscard]] std::expected<void, VulkanError> initCommandPool();
	[[nodiscard]] std::expected<void, VulkanError> initCommandBuffers();
	[[nodiscard]] std::expected<void, VulkanError> initSyncObjects(uint32_t maxFramesInFlight);
	[[nodiscard]] std::expected<void, VulkanError> initDescriptorPool();
	[[nodiscard]] std::expected<void, VulkanError> initImGui(uint32_t imageCount);

	// Cleanup methods
	void cleanupSwapchain();
	void cleanupAll();

	// Helper for tracking cleanup functions
	void addCleanupFunction(std::function<void()> func, int priority);

	// Font loading
	[[nodiscard]] static std::expected<void, std::string> loadFonts(Logger& logger);

	// Helper method to check and log available Vulkan version
	void checkVulkanVersion(VkPhysicalDevice physicalDevice);

	// ImGui style setup
	void setupImGuiStyle();
};
