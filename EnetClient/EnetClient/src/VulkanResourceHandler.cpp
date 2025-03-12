#include "VulkanResourceHandler.h"

#include "Constants.h"
#include "IconsLucide.h"

// VMA for Vulkan memory management
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

static VmaAllocator m_allocator = VK_NULL_HANDLE;

// Constants
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

// Implementation of font loading with modern C++ error handling
static std::expected<bool, std::string> LoadFont(const FontConfig& fontConfig, ImFontAtlas* fonts)
{
	if (!std::filesystem::exists(fontConfig.path))
	{
		return std::unexpected(std::format("Font file not found: {}", fontConfig.path));
	}

	auto font = fonts->AddFontFromFileTTF(fontConfig.path.c_str(), fontConfig.size, fontConfig.config, fontConfig.ranges);
	if (font == nullptr)
	{
		return std::unexpected(std::format("Failed to load font from: {}", fontConfig.path));
	}

	return true;
}

// Font loading implementation
std::expected<void, std::string> VulkanResourceHandler::loadFonts(Logger& logger)
{
	const std::string resDir = Constants::Runtime::IsDebuggerPresent ? "../../res/" : "";
	ImGuiIO& io = ImGui::GetIO();

	// Font sizes using C++20 designated initializers
	struct FontSizes
	{
		float base = 16.0f;   // Regular text
		float header = 22.0f; // Headers/titles
		float small = 14.0f;  // Small text
	} sizes;

	// ImWchar ranges for icons - using constexpr to enforce compile-time initialization
	static constexpr ImWchar icons_ranges[] = { ICON_MIN_LC, ICON_MAX_LC, 0 };

	// Define a helper to load a font with error handling
	auto loadFontWithConfig = [&logger](const FontConfig& config, ImFontAtlas* atlas) -> std::expected<void, std::string>
	{
		auto result = LoadFont(config, atlas);
		if (!result)
		{
			logger.error(result.error());
			return std::unexpected(result.error());
		}
		return {};
	};

	// --- Font 0: Regular font with icons ---
	{
		// Base font config using designated initializers
		ImFontConfig baseConfig{};
		baseConfig.SizePixels = sizes.base;
		strcpy_s(baseConfig.Name, "Regular");

		// Regular font
		FontConfig regularFont{ .path = resDir + "WorkSans-Regular.ttf", .size = sizes.base, .ranges = nullptr, .config = &baseConfig };

		auto result = loadFontWithConfig(regularFont, io.Fonts);
		if (!result)
		{
			return result;
		}

		// Icons config to merge with regular font
		ImFontConfig iconsConfig{};
		iconsConfig.MergeMode = true;
		iconsConfig.PixelSnapH = true;
		iconsConfig.OversampleH = 2;
		iconsConfig.OversampleV = 2;
		iconsConfig.GlyphOffset = ImVec2(0, 3);
		strcpy_s(iconsConfig.Name, "Icons-Regular");

		// Icon font to merge with regular font
		FontConfig iconFont{ .path = resDir + "lucide.ttf", .size = sizes.base, .ranges = icons_ranges, .config = &iconsConfig };

		result = loadFontWithConfig(iconFont, io.Fonts);
		if (!result)
		{
			return result;
		}
	}

	// --- Font 1: Header font with icons ---
	{
		// Header font config
		ImFontConfig headerConfig{};
		headerConfig.SizePixels = sizes.header;
		strcpy_s(headerConfig.Name, "Header");

		// Header font
		FontConfig headerFont{ .path = resDir + "WorkSans-Bold.ttf", .size = sizes.header, .ranges = nullptr, .config = &headerConfig };

		auto result = loadFontWithConfig(headerFont, io.Fonts);
		if (!result)
		{
			return result;
		}

		// Icons config to merge with header font
		ImFontConfig iconsConfig{};
		iconsConfig.MergeMode = true;
		iconsConfig.PixelSnapH = true;
		iconsConfig.OversampleH = 2;
		iconsConfig.OversampleV = 2;
		iconsConfig.GlyphOffset = ImVec2(0, 3);
		strcpy_s(iconsConfig.Name, "Icons-Header");

		// Icon font to merge with header font
		FontConfig iconFont{ .path = resDir + "lucide.ttf", .size = sizes.header, .ranges = icons_ranges, .config = &iconsConfig };

		result = loadFontWithConfig(iconFont, io.Fonts);
		if (!result)
		{
			return result;
		}
	}

	// --- Font 2: Bold font ---
	{
		// Bold font config
		ImFontConfig boldConfig{};
		boldConfig.SizePixels = sizes.base;
		strcpy_s(boldConfig.Name, "Bold");

		// Bold font
		FontConfig boldFont{ .path = resDir + "WorkSans-Bold.ttf", .size = sizes.base, .ranges = nullptr, .config = &boldConfig };

		auto result = loadFontWithConfig(boldFont, io.Fonts);
		if (!result)
		{
			return result;
		}
	}

	// --- Font 3: Monospace font ---
	{
		// Monospace font config
		ImFontConfig monoConfig{};
		monoConfig.SizePixels = sizes.base;
		strcpy_s(monoConfig.Name, "Monospace");

		// Monospace font
		FontConfig monoFont{ .path = resDir + "JetBrainsMono.ttf", .size = sizes.base, .ranges = nullptr, .config = &monoConfig };

		auto result = loadFontWithConfig(monoFont, io.Fonts);
		if (!result)
		{
			return result;
		}
	}

	// --- Font 4: Italics font ---
	{
		// Italics font config
		ImFontConfig italicsConfig{};
		italicsConfig.SizePixels = sizes.base;
		strcpy_s(italicsConfig.Name, "Italics");

		// Italics font
		FontConfig italicsFont{ .path = resDir + "WorkSans-Italic.ttf", .size = sizes.base, .ranges = nullptr, .config = &italicsConfig };

		auto result = loadFontWithConfig(italicsFont, io.Fonts);
		if (!result)
		{
			return result;
		}
	}

	io.FontGlobalScale = 1.0f;
	logger.debug("Fonts loaded!");

	// Log the number of loaded fonts for debugging using std::format for C++20
	logger.debug(std::format("Total fonts loaded: {}", io.Fonts->Fonts.Size));

	return {};
}

// Constructor
VulkanResourceHandler::VulkanResourceHandler(std::string_view appNameView)
      : m_appName(appNameView) // Convert string_view to string for storage
{
	m_logger.debug(std::format("Creating VulkanResourceHandler for application: {}", m_appName));
}

// Destructor
VulkanResourceHandler::~VulkanResourceHandler()
{
	m_logger.debug("Destroying VulkanResourceHandler");
	cleanupAll();
}

// Helper to add cleanup functions with priorities
void VulkanResourceHandler::addCleanupFunction(std::function<void()> func, int priority)
{
	m_cleanupFunctions.push_back({ .cleanup = std::move(func), .priority = priority });
}

// Initialize all Vulkan resources with modern error handling
std::expected<void, VulkanError> VulkanResourceHandler::initialize(GLFWwindow* window, uint32_t width, uint32_t height, uint32_t maxFramesInFlight)
{
	m_window = window;

	// Initialize in proper order with proper error handling
	if (auto result = initInstance(); !result)
	{
		return std::unexpected(result.error());
	}

	if (auto result = initSurface(); !result)
	{
		return std::unexpected(result.error());
	}

	if (auto result = initDevice(); !result)
	{
		return std::unexpected(result.error());
	}

	if (auto result = initAllocator(); !result)
	{
		return std::unexpected(result.error());
	}

	if (auto result = initSwapchain(width, height); !result)
	{
		return std::unexpected(result.error());
	}

	if (auto result = initRenderPass(); !result)
	{
		return std::unexpected(result.error());
	}

	if (auto result = initFramebuffers(); !result)
	{
		return std::unexpected(result.error());
	}

	if (auto result = initCommandPool(); !result)
	{
		return std::unexpected(result.error());
	}

	if (auto result = initCommandBuffers(); !result)
	{
		return std::unexpected(result.error());
	}

	if (auto result = initSyncObjects(maxFramesInFlight); !result)
	{
		return std::unexpected(result.error());
	}

	if (auto result = initDescriptorPool(); !result)
	{
		return std::unexpected(result.error());
	}

	if (auto result = initImGui(getSwapchainImageCount()); !result)
	{
		return std::unexpected(result.error());
	}

	m_logger.debug("VulkanResourceHandler initialized successfully");
	return {};
}

// Helper method to check and log available Vulkan version
void VulkanResourceHandler::checkVulkanVersion(VkPhysicalDevice physicalDevice)
{
	// Initialize VkPhysicalDeviceProperties2 structure
	VkPhysicalDeviceProperties2 deviceProps2{};
	deviceProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

	// Use vkGetPhysicalDeviceProperties2 to populate the structure
	vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProps2);

	// Access the properties field
	const VkPhysicalDeviceProperties& props = deviceProps2.properties;

	// Extract version components using Vulkan macros
	uint32_t majorVersion = VK_VERSION_MAJOR(props.apiVersion);
	uint32_t minorVersion = VK_VERSION_MINOR(props.apiVersion);
	uint32_t patchVersion = VK_VERSION_PATCH(props.apiVersion);

	// Log device information
	m_logger.info(std::format("Physical device: {}", props.deviceName));
	m_logger.info(std::format("API version: {}.{}.{}", majorVersion, minorVersion, patchVersion));
	m_logger.info(std::format("Driver version: {}", props.driverVersion));
	m_logger.info(std::format("Vendor ID: {}", props.vendorID));
	m_logger.info(std::format("Device ID: {}", props.deviceID));
	m_logger.info(std::format("Device type: {}", static_cast<int>(props.deviceType)));

	// Check API version support
	if (props.apiVersion >= VK_API_VERSION_1_4)
	{
		m_logger.info("Device supports Vulkan 1.4 or higher");
	}
	else if (props.apiVersion >= VK_API_VERSION_1_3)
	{
		m_logger.warning("Device only supports Vulkan 1.3");
	}
	else if (props.apiVersion >= VK_API_VERSION_1_2)
	{
		m_logger.warning("Device only supports Vulkan 1.2");
	}
	else if (props.apiVersion >= VK_API_VERSION_1_1)
	{
		m_logger.warning("Device only supports Vulkan 1.1");
	}
	else
	{
		m_logger.warning("Device only supports Vulkan 1.0");
	}
}

// Initialize Vulkan instance with VkBootstrap
std::expected<void, VulkanError> VulkanResourceHandler::initInstance()
{
	m_logger.debug("Initializing Vulkan instance with Vulkan 1.4 support");

	// Set up the debug messenger to use the logger
	auto debugCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	                             VkDebugUtilsMessageTypeFlagsEXT messageType,
	                             const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	                             void* pUserData) -> VkBool32
	{
		Logger& logger = Logger::getInstance();

		switch (messageSeverity)
		{
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
				logger.debug(std::format("Vulkan: {}", pCallbackData->pMessage));
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
				logger.info(std::format("Vulkan: {}", pCallbackData->pMessage));
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
				logger.warning(std::format("Vulkan: {}", pCallbackData->pMessage));
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
				logger.error(std::format("Vulkan: {}", pCallbackData->pMessage));
				break;
			default:
				logger.error(std::format("Vulkan (unknown severity): {}", pCallbackData->pMessage));
		}
		return VK_FALSE;
	};

	// Create additional extensions for Vulkan 1.4
	const std::vector<const char*> instanceExtensions = {
		VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
		VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
	};

	// Use VkBootstrap for instance creation
	vkb::InstanceBuilder instanceBuilder;
	auto inst_ret = instanceBuilder.set_app_name(m_appName.c_str())
	                        .enable_extensions(instanceExtensions)
	                        .request_validation_layers(true)
	                        .set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	                        .set_debug_callback(debugCallback)
	                        .require_api_version(1, 4, 0)
	                        .build(); // Now build the instance

	if (!inst_ret)
	{
		m_logger.error(std::format("Failed to create Vulkan instance: {}", inst_ret.error().message()));
		return std::unexpected(VulkanError::InstanceCreationFailed);
	}

	// Store the instance
	m_instance = inst_ret.value();
	m_instanceDispatchTable = m_instance.make_table();

	// Add cleanup function for instance
	addCleanupFunction(
	        [this]()
	        {
		        m_logger.debug("Destroying Vulkan instance");
		        vkb::destroy_instance(m_instance);
	        },
	        100); // High priority - destroy last

	m_logger.debug("Vulkan instance created successfully!");

	return {};
}

// Initialize window surface with modern error handling
std::expected<void, VulkanError> VulkanResourceHandler::initSurface()
{
	m_logger.debug("Creating window surface");

	VkResult result = glfwCreateWindowSurface(m_instance.instance, m_window, nullptr, &m_surface);
	if (result != VK_SUCCESS)
	{
		// Get error message from GLFW if available
		const char* errorMsg;
		int glfwError = glfwGetError(&errorMsg);
		if (glfwError != GLFW_NO_ERROR)
		{
			m_logger.error(std::format("GLFW error while creating surface: {}", errorMsg));
		}

		m_logger.error(std::format("Failed to create window surface: {}", static_cast<int>(result)));
		return std::unexpected(VulkanError::SurfaceCreationFailed);
	}

	// Add cleanup function for surface
	addCleanupFunction(
	        [this]()
	        {
		        m_logger.debug("Destroying window surface");
		        vkb::destroy_surface(m_instance, m_surface);
		        m_surface = VK_NULL_HANDLE;
	        },
	        90); // High priority - destroy before instance

	m_logger.debug("Window surface created successfully");
	return {};
}

// Initialize logical device and queues with enhanced VkBootstrap integration
std::expected<void, VulkanError> VulkanResourceHandler::initDevice()
{
	m_logger.debug("Selecting physical device and creating logical device with Vulkan 1.4 support");

	// Enable extended dynamic state features
	VkPhysicalDeviceExtendedDynamicState3FeaturesEXT extendedDynamicState3Features{};
	extendedDynamicState3Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
	extendedDynamicState3Features.extendedDynamicState3ColorBlendEnable = VK_TRUE;
	extendedDynamicState3Features.extendedDynamicState3ColorBlendEquation = VK_TRUE;
	extendedDynamicState3Features.extendedDynamicState3ColorWriteMask = VK_TRUE;
	extendedDynamicState3Features.extendedDynamicState3RasterizationSamples = VK_TRUE;
	extendedDynamicState3Features.extendedDynamicState3SampleMask = VK_TRUE;
	extendedDynamicState3Features.extendedDynamicState3AlphaToCoverageEnable = VK_TRUE;
	extendedDynamicState3Features.extendedDynamicState3AlphaToOneEnable = VK_TRUE;
	extendedDynamicState3Features.extendedDynamicState3LogicOpEnable = VK_TRUE;
	extendedDynamicState3Features.extendedDynamicState3ColorBlendAdvanced = VK_TRUE;

	VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicStateFeatures{};
	extendedDynamicStateFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
	extendedDynamicStateFeatures.extendedDynamicState = VK_TRUE;
	extendedDynamicStateFeatures.pNext = &extendedDynamicState3Features;

	// Enable mesh shader features if needed
	VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
	meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
	meshShaderFeatures.taskShader = VK_TRUE;
	meshShaderFeatures.meshShader = VK_TRUE;
	meshShaderFeatures.multiviewMeshShader = VK_TRUE;
	meshShaderFeatures.primitiveFragmentShadingRateMeshShader = VK_TRUE;
	meshShaderFeatures.meshShaderQueries = VK_TRUE;
	meshShaderFeatures.pNext = &extendedDynamicStateFeatures;

	// Enable Vulkan 1.3 features
	VkPhysicalDeviceVulkan13Features features13{};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;
	features13.maintenance4 = VK_TRUE;
	features13.pNext = &meshShaderFeatures;

	// Enable Vulkan 1.2 features
	VkPhysicalDeviceVulkan12Features features12{};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.bufferDeviceAddress = VK_TRUE;
	features12.descriptorIndexing = VK_TRUE;
	features12.pNext = &features13;

	// Enable Vulkan 1.1 features
	VkPhysicalDeviceVulkan11Features features11{};
	features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	features11.multiview = VK_TRUE;
	features11.pNext = &features12;

	// Set up basic device features
	VkPhysicalDeviceFeatures2 features2{};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.features.fillModeNonSolid = VK_TRUE;
	features2.features.wideLines = VK_TRUE;
	features2.features.geometryShader = VK_TRUE;
	features2.pNext = &features11;

	// Required device extensions
	const std::vector<const char*> deviceExtensions = { VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
		VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME,
		VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
		VK_EXT_MESH_SHADER_EXTENSION_NAME };

	// Use more features of VkBootstrap's physical device selection
	vkb::PhysicalDeviceSelector selector{ m_instance };
	auto phys_ret = selector.set_minimum_version(1, 4) // Require Vulkan 1.4
	                        .add_required_extensions(deviceExtensions)
	                        .set_required_features_11(features11)
	                        .set_required_features_12(features12)
	                        .set_required_features_13(features13)
	                        .set_required_features(features2.features)
	                        .set_surface(m_surface)
	                        .select();

	if (!phys_ret)
	{
		m_logger.error(std::format("Failed to select physical device: {}", phys_ret.error().message()));

		// Drop all required features so we can log the devices
		selector.set_required_features(VkPhysicalDeviceFeatures{});
		selector.set_required_features_11(VkPhysicalDeviceVulkan11Features{});
		selector.set_required_features_12(VkPhysicalDeviceVulkan12Features{});
		selector.set_required_features_13(VkPhysicalDeviceVulkan13Features{});
		selector.set_minimum_version(0, 0);   // Drop version requirement
		selector.add_required_extensions({}); // Drop extensions

		// Log all devices that we tried to select
		auto devices = selector.select_devices();
		if (devices)
		{
			m_logger.info("Devices checked during selection:");
			for (const auto& device: devices.value())
			{
				VkPhysicalDeviceProperties props;
				vkGetPhysicalDeviceProperties(device, &props);
				m_logger.info(std::format(
				        "  - {}: {}.{}.{}", props.deviceName, VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion)));
			}
		}
		else
		{
			m_logger.error("Failed to retrieve list of physical devices");
		}

		return std::unexpected(VulkanError::PhysicalDeviceSelectionFailed);
	}

	// Get the selected physical device
	auto& gpu = phys_ret.value();

	// Check and log available Vulkan version
	checkVulkanVersion(gpu.physical_device);

	// Verify Vulkan 1.4 support
	VkPhysicalDeviceProperties props = gpu.properties;
	if (props.apiVersion < VK_API_VERSION_1_4)
	{
		m_logger.error("Selected device does not support Vulkan 1.4");
		return std::unexpected(VulkanError::PhysicalDeviceSelectionFailed);
	}

	// Set flag for dynamic rendering
	m_usesDynamicRendering = true;

	// Enhanced device creation with VkBootstrap for Vulkan 1.4
	vkb::DeviceBuilder deviceBuilder{ gpu };

	// Add the extended dynamic state features to the pNext chain
	// (Other features are already in the chain from the PhysicalDeviceSelector)
	deviceBuilder.add_pNext(&extendedDynamicStateFeatures);

	auto dev_ret = deviceBuilder.build();

	if (!dev_ret)
	{
		m_logger.error(std::format("Failed to create logical device: {}", dev_ret.error().message()));
		return std::unexpected(VulkanError::LogicalDeviceCreationFailed);
	}

	// Store the device object directly
	m_device = dev_ret.value();
	m_dispatchTable = m_device.make_table();

	// Get queue handles using VkBootstrap's utility functions
	auto graphicsQueueResult = m_device.get_queue(vkb::QueueType::graphics);
	auto presentQueueResult = m_device.get_queue(vkb::QueueType::present);
	auto graphicsQueueFamilyResult = m_device.get_queue_index(vkb::QueueType::graphics);

	if (!graphicsQueueResult || !presentQueueResult || !graphicsQueueFamilyResult)
	{
		m_logger.error("Failed to get required queue handles");
		return std::unexpected(VulkanError::LogicalDeviceCreationFailed);
	}

	m_graphicsQueue = graphicsQueueResult.value();
	m_presentQueue = presentQueueResult.value();
	m_graphicsQueueFamily = graphicsQueueFamilyResult.value();

	// Add cleanup function for device using VkBootstrap's cleanup
	addCleanupFunction(
	        [this]()
	        {
		        m_logger.debug("Destroying logical device");
		        m_dispatchTable.deviceWaitIdle();
		        vkb::destroy_device(m_device);
	        },
	        80); // Destroy after all device resources but before surface/instance

	m_logger.debug("Logical device created successfully");
	return {};
}

// Initialize VMA allocator
std::expected<void, VulkanError> VulkanResourceHandler::initAllocator()
{
	m_logger.debug("Creating VMA allocator");

	// Setup the VMA allocator
	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.physicalDevice = m_device.physical_device;
	allocatorInfo.device = m_device.device;
	allocatorInfo.instance = m_instance.instance;

	// Setup Vulkan functions
	VmaVulkanFunctions vulkanFunctions{};
	vulkanFunctions.vkGetInstanceProcAddr = m_instance.fp_vkGetInstanceProcAddr;
	vulkanFunctions.vkGetDeviceProcAddr = m_device.fp_vkGetDeviceProcAddr;

	// Instance functions
	vulkanFunctions.vkGetPhysicalDeviceProperties = m_instanceDispatchTable.fp_vkGetPhysicalDeviceProperties;
	vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = m_instanceDispatchTable.fp_vkGetPhysicalDeviceMemoryProperties;

	// Device functions
	vulkanFunctions.vkAllocateMemory = m_dispatchTable.fp_vkAllocateMemory;
	vulkanFunctions.vkFreeMemory = m_dispatchTable.fp_vkFreeMemory;
	vulkanFunctions.vkMapMemory = m_dispatchTable.fp_vkMapMemory;
	vulkanFunctions.vkUnmapMemory = m_dispatchTable.fp_vkUnmapMemory;
	vulkanFunctions.vkFlushMappedMemoryRanges = m_dispatchTable.fp_vkFlushMappedMemoryRanges;
	vulkanFunctions.vkInvalidateMappedMemoryRanges = m_dispatchTable.fp_vkInvalidateMappedMemoryRanges;
	vulkanFunctions.vkBindBufferMemory = m_dispatchTable.fp_vkBindBufferMemory;
	vulkanFunctions.vkBindImageMemory = m_dispatchTable.fp_vkBindImageMemory;
	vulkanFunctions.vkGetBufferMemoryRequirements = m_dispatchTable.fp_vkGetBufferMemoryRequirements;
	vulkanFunctions.vkGetImageMemoryRequirements = m_dispatchTable.fp_vkGetImageMemoryRequirements;
	vulkanFunctions.vkCreateBuffer = m_dispatchTable.fp_vkCreateBuffer;
	vulkanFunctions.vkDestroyBuffer = m_dispatchTable.fp_vkDestroyBuffer;
	vulkanFunctions.vkCreateImage = m_dispatchTable.fp_vkCreateImage;
	vulkanFunctions.vkDestroyImage = m_dispatchTable.fp_vkDestroyImage;
	vulkanFunctions.vkCmdCopyBuffer = m_dispatchTable.fp_vkCmdCopyBuffer;

	allocatorInfo.pVulkanFunctions = &vulkanFunctions;

	// Enable buffer device address if available (Vulkan 1.2+)
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

	VkResult result = vmaCreateAllocator(&allocatorInfo, &m_allocator);
	if (result != VK_SUCCESS)
	{
		m_logger.error(std::format("Failed to create VMA allocator: {}", static_cast<int>(result)));
		return std::unexpected(VulkanError::AllocatorCreationFailed);
	}

	// Add cleanup function for allocator
	addCleanupFunction(
	        [this]()
	        {
		        m_logger.debug("Destroying VMA allocator");
		        vmaDestroyAllocator(m_allocator);
		        m_allocator = VK_NULL_HANDLE;
	        },
	        70); // Destroy before device but after all VMA-allocated resources

	m_logger.debug("VMA allocator created successfully");
	return {};
}

// Initialize swapchain and related resources with enhanced VkBootstrap usage
std::expected<void, VulkanError> VulkanResourceHandler::initSwapchain(uint32_t width, uint32_t height)
{
	m_logger.debug(std::format("Creating swapchain with dimensions {}x{}", width, height));

	// Wait for device to be idle
	m_dispatchTable.deviceWaitIdle();

	// Delete old image views
	for (auto& imageView: m_swapchainImageViews)
	{
		m_dispatchTable.destroyImageView(imageView, nullptr);
	}

	// Enhanced swapchain creation with more options
	vkb::SwapchainBuilder swapchainBuilder{ m_device, m_surface };

	auto swap_ret = swapchainBuilder.use_default_format_selection()
	                        .set_desired_format(VkSurfaceFormatKHR{ .format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
	                        .add_fallback_format(VkSurfaceFormatKHR{ .format = VK_FORMAT_R8G8B8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
	                        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // Use vsync present mode
	                        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
	                        .set_old_swapchain(m_swapchain)
	                        .set_desired_extent(width, height)
	                        .build();

	if (!swap_ret)
	{
		m_logger.error(std::format("Failed to create swapchain: {}", swap_ret.error().message()));
		return std::unexpected(VulkanError::SwapchainCreationFailed);
	}

	// Store VkBootstrap swapchain directly
	vkb::destroy_swapchain(m_swapchain);
	m_swapchain = swap_ret.value();

	// Get images and image views
	auto images_ret = m_swapchain.get_images();
	if (!images_ret)
	{
		m_logger.error(std::format("Failed to get swapchain images: {}", images_ret.error().message()));
		return std::unexpected(VulkanError::SwapchainCreationFailed);
	}
	m_swapchainImages = images_ret.value();

	auto views_ret = m_swapchain.get_image_views();
	if (!views_ret)
	{
		m_logger.error(std::format("Failed to get swapchain image views: {}", views_ret.error().message()));
		return std::unexpected(VulkanError::SwapchainCreationFailed);
	}
	m_swapchainImageViews = views_ret.value();

	// Add cleanup function for swapchain resources
	addCleanupFunction([this]() { cleanupSwapchain(); }, 50); // Medium priority - destroy after render resources

	m_logger.debug(std::format("Swapchain created successfully with {} images (format: {}, extent: {}x{})",
	        m_swapchain.image_count,
	        (int) m_swapchain.image_format,
	        m_swapchain.extent.width,
	        m_swapchain.extent.height));

	return {};
}

// Initialize render pass with Vulkan 1.4 dynamic rendering
std::expected<void, VulkanError> VulkanResourceHandler::initRenderPass()
{
	m_logger.debug("Creating render pass with Vulkan 1.4 dynamic rendering");

	if (m_usesDynamicRendering)
	{
		m_logger.info("Using Vulkan 1.4 dynamic rendering (no traditional render pass required)");

		// With dynamic rendering in Vulkan 1.4, we don't need to create a render pass
		// for our main rendering, but ImGui might still need one in some cases
		// We'll create a simple render pass for compatibility

		VkAttachmentDescription colorAttachment{ .format = m_swapchain.image_format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, // We'll clear in dynamic rendering
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };

		VkAttachmentReference colorAttachmentRef{ .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass{ .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &colorAttachmentRef };

		VkRenderPassCreateInfo renderPassInfo{ .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &colorAttachment,
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = 0,
			.pDependencies = nullptr };

		VkResult result = m_dispatchTable.createRenderPass(&renderPassInfo, nullptr, &m_renderPass);
		if (result != VK_SUCCESS)
		{
			m_logger.error(std::format("Failed to create compatibility render pass for ImGui: {}", static_cast<int>(result)));
			return std::unexpected(VulkanError::RenderPassCreationFailed);
		}

		m_logger.debug("Compatibility render pass created for ImGui");
	}
	else
	{
		m_logger.warning("Dynamic rendering not available, falling back to traditional render pass");

		// Create a traditional render pass
		VkAttachmentDescription colorAttachment{ .format = m_swapchain.image_format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };

		VkAttachmentReference colorAttachmentRef{ .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass{ .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &colorAttachmentRef };

		// Add subpass dependencies for proper synchronization
		VkSubpassDependency dependencies[2]{};

		// Dependency at the start of the render pass
		dependencies[0] = { .srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT };

		// Dependency at the end of the render pass
		dependencies[1] = { .srcSubpass = 0,
			.dstSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT };

		VkRenderPassCreateInfo renderPassInfo{ .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &colorAttachment,
			.subpassCount = 1,
			.pSubpasses = &subpass,
			.dependencyCount = 2,
			.pDependencies = dependencies };

		VkResult result = m_dispatchTable.createRenderPass(&renderPassInfo, nullptr, &m_renderPass);
		if (result != VK_SUCCESS)
		{
			m_logger.error(std::format("Failed to create render pass: {}", static_cast<int>(result)));
			return std::unexpected(VulkanError::RenderPassCreationFailed);
		}

		m_logger.debug("Traditional render pass created successfully");
	}

	// Add cleanup function for render pass
	addCleanupFunction(
	        [this]()
	        {
		        m_logger.debug("Destroying render pass");
		        m_dispatchTable.destroyRenderPass(m_renderPass, nullptr);
		        m_renderPass = VK_NULL_HANDLE;
	        },
	        40); // Destroy after framebuffers

	return {};
}

// Initialize framebuffers with modern C++ features
std::expected<void, VulkanError> VulkanResourceHandler::initFramebuffers()
{
	m_logger.debug("Creating framebuffers");

	// If using dynamic rendering, we might not need framebuffers for our main rendering path
	// but ImGui and other compatibility code might still need them

	m_framebuffers.resize(m_swapchainImageViews.size());

	// Create a framebuffer for each swapchain image view
	for (size_t i = 0; i < m_swapchainImageViews.size(); i++)
	{
		VkImageView attachments[] = { m_swapchainImageViews[i] };

		// Use designated initializers
		VkFramebufferCreateInfo framebufferInfo{ .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = m_renderPass,
			.attachmentCount = 1,
			.pAttachments = attachments,
			.width = m_swapchain.extent.width,
			.height = m_swapchain.extent.height,
			.layers = 1 };

		VkResult result = m_dispatchTable.createFramebuffer(&framebufferInfo, nullptr, &m_framebuffers[i]);
		if (result != VK_SUCCESS)
		{
			// Clean up already created framebuffers
			for (size_t j = 0; j < i; j++)
			{
				m_dispatchTable.destroyFramebuffer(m_framebuffers[j], nullptr);
			}

			m_logger.error(std::format("Failed to create framebuffer for image view {}: {}", i, static_cast<int>(result)));
			return std::unexpected(VulkanError::FramebufferCreationFailed);
		}
	}

	// Add cleanup function for framebuffers
	addCleanupFunction(
	        [this]()
	        {
		        m_logger.debug("Destroying framebuffers");
		        for (auto framebuffer: m_framebuffers)
		        {
			        m_dispatchTable.destroyFramebuffer(framebuffer, nullptr);
		        }
		        m_framebuffers.clear();
	        },
	        30); // Destroy right after image views

	m_logger.debug(std::format("Created {} framebuffers successfully", m_framebuffers.size()));
	return {};
}

// Initialize command pool
std::expected<void, VulkanError> VulkanResourceHandler::initCommandPool()
{
	m_logger.debug("Creating command pool");

	VkCommandPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = m_graphicsQueueFamily
	};

	VkResult result = m_dispatchTable.createCommandPool(&poolInfo, nullptr, &m_commandPool);
	if (result != VK_SUCCESS)
	{
		m_logger.error(std::format("Failed to create command pool: {}", static_cast<int>(result)));
		return std::unexpected(VulkanError::CommandPoolCreationFailed);
	}

	// Add cleanup function for command pool
	addCleanupFunction(
	        [this]()
	        {
		        m_logger.debug("Destroying command pool");
		        m_dispatchTable.destroyCommandPool(m_commandPool, nullptr);
		        m_commandPool = VK_NULL_HANDLE;
	        },
	        20); // Lower priority - destroy before device but after descriptor pool

	m_logger.debug("Command pool created successfully");
	return {};
}

// Initialize command buffers
std::expected<void, VulkanError> VulkanResourceHandler::initCommandBuffers()
{
	m_logger.debug("Creating command buffers");

	// Create some extra command buffers for flexibility
	m_commandBuffers.resize(m_swapchain.image_count + 10);

	VkCommandBufferAllocateInfo allocInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = m_commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size()) };

	VkResult result = m_dispatchTable.allocateCommandBuffers(&allocInfo, m_commandBuffers.data());
	if (result != VK_SUCCESS)
	{
		m_logger.error(std::format("Failed to allocate command buffers: {}", static_cast<int>(result)));
		return std::unexpected(VulkanError::CommandBufferAllocationFailed);
	}

	// No need for cleanup function - command buffers are freed when the command pool is destroyed

	m_logger.debug(std::format("Created {} command buffers successfully", m_commandBuffers.size()));
	return {};
}

// Initialize synchronization objects
std::expected<void, VulkanError> VulkanResourceHandler::initSyncObjects(uint32_t maxFramesInFlight)
{
	m_logger.debug("Creating synchronization objects");

	// Create semaphores and fences
	m_imageAvailableSemaphores.resize(maxFramesInFlight);
	m_renderFinishedSemaphores.resize(maxFramesInFlight);
	m_inFlightFences.resize(maxFramesInFlight);
	m_imagesInFlight.resize(m_swapchain.image_count, VK_NULL_HANDLE);

	VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	VkFenceCreateInfo fenceInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };

	for (size_t i = 0; i < maxFramesInFlight; i++)
	{
		if (m_dispatchTable.createSemaphore(&semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS
		        || m_dispatchTable.createSemaphore(&semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS
		        || m_dispatchTable.createFence(&fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
		{
			m_logger.error("Failed to create synchronization objects");
			return std::unexpected(VulkanError::SyncObjectCreationFailed);
		}
	}

	// Add cleanup function for sync objects
	addCleanupFunction(
	        [this]()
	        {
		        m_logger.debug("Destroying synchronization objects");
		        for (size_t i = 0; i < m_imageAvailableSemaphores.size(); i++)
		        {
			        m_dispatchTable.destroySemaphore(m_imageAvailableSemaphores[i], nullptr);
			        m_dispatchTable.destroySemaphore(m_renderFinishedSemaphores[i], nullptr);
			        m_dispatchTable.destroyFence(m_inFlightFences[i], nullptr);
		        }
		        m_imageAvailableSemaphores.clear();
		        m_renderFinishedSemaphores.clear();
		        m_inFlightFences.clear();
		        m_imagesInFlight.clear();
	        },
	        10); // Low priority - destroy early

	m_logger.debug("Synchronization objects created successfully");
	return {};
}

// Initialize descriptor pool for ImGui
std::expected<void, VulkanError> VulkanResourceHandler::initDescriptorPool()
{
	m_logger.debug("Creating descriptor pool");

	// Create descriptor pool for ImGui (and other needs)
	VkDescriptorPoolSize poolSizes[] = {
		{		        VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{		  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{		  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{   VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{   VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{       VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo poolInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 1000,
		.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes)),
		.pPoolSizes = poolSizes };

	VkResult result = m_dispatchTable.createDescriptorPool(&poolInfo, nullptr, &m_descriptorPool);
	if (result != VK_SUCCESS)
	{
		m_logger.error(std::format("Failed to create descriptor pool: {}", static_cast<int>(result)));
		return std::unexpected(VulkanError::DescriptorPoolCreationFailed);
	}

	// Add cleanup function for descriptor pool
	addCleanupFunction(
	        [this]()
	        {
		        m_logger.debug("Destroying descriptor pool");
		        m_dispatchTable.destroyDescriptorPool(m_descriptorPool, nullptr);
		        m_descriptorPool = VK_NULL_HANDLE;
	        },
	        15); // Destroy before command pool

	m_logger.debug("Descriptor pool created successfully");
	return {};
}

// Set up ImGui style similar to the reference implementation
void VulkanResourceHandler::setupImGuiStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	// Modern color palette with darker greys and green accent
	ImVec4 bgDark = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
	ImVec4 bgMid = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
	ImVec4 bgLight = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
	ImVec4 accent = ImVec4(0.10f, 0.60f, 0.30f, 1.00f);
	ImVec4 accentLight = ImVec4(0.20f, 0.70f, 0.40f, 1.00f);
	ImVec4 textPrimary = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
	ImVec4 textSecondary = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);

	// Set colors
	colors[ImGuiCol_Text] = textPrimary;
	colors[ImGuiCol_TextDisabled] = textSecondary;
	colors[ImGuiCol_WindowBg] = bgDark;
	colors[ImGuiCol_ChildBg] = bgMid;
	colors[ImGuiCol_PopupBg] = bgMid;
	colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.25f, 0.50f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = bgLight;
	colors[ImGuiCol_FrameBgHovered] = accent;
	colors[ImGuiCol_FrameBgActive] = accentLight;
	colors[ImGuiCol_TitleBg] = bgMid;
	colors[ImGuiCol_TitleBgActive] = accent;
	colors[ImGuiCol_TitleBgCollapsed] = bgDark;
	colors[ImGuiCol_MenuBarBg] = bgMid;
	colors[ImGuiCol_ScrollbarBg] = bgDark;
	colors[ImGuiCol_ScrollbarGrab] = bgLight;
	colors[ImGuiCol_ScrollbarGrabHovered] = accent;
	colors[ImGuiCol_ScrollbarGrabActive] = accentLight;
	colors[ImGuiCol_CheckMark] = accentLight;
	colors[ImGuiCol_SliderGrab] = accent;
	colors[ImGuiCol_SliderGrabActive] = accentLight;
	colors[ImGuiCol_Button] = bgLight;
	colors[ImGuiCol_ButtonHovered] = accent;
	colors[ImGuiCol_ButtonActive] = accentLight;
	colors[ImGuiCol_Header] = bgLight;
	colors[ImGuiCol_HeaderHovered] = accent;
	colors[ImGuiCol_HeaderActive] = accentLight;
	colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
	colors[ImGuiCol_SeparatorHovered] = accent;
	colors[ImGuiCol_SeparatorActive] = accentLight;
	colors[ImGuiCol_ResizeGrip] = bgLight;
	colors[ImGuiCol_ResizeGripHovered] = accent;
	colors[ImGuiCol_ResizeGripActive] = accentLight;
	colors[ImGuiCol_Tab] = bgMid;
	colors[ImGuiCol_TabHovered] = accent;
	colors[ImGuiCol_TabActive] = accentLight;
	colors[ImGuiCol_TabUnfocused] = bgDark;
	colors[ImGuiCol_TabUnfocusedActive] = bgLight;
	colors[ImGuiCol_PlotLines] = accent;
	colors[ImGuiCol_PlotLinesHovered] = accentLight;
	colors[ImGuiCol_PlotHistogram] = accent;
	colors[ImGuiCol_PlotHistogramHovered] = accentLight;
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.80f, 0.50f, 0.35f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(0.20f, 0.80f, 0.50f, 0.90f);
	colors[ImGuiCol_NavHighlight] = accent;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.20f, 0.80f, 0.50f, 0.90f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

	// Adjust style properties
	style.WindowPadding = ImVec2(10, 10);
	style.FramePadding = ImVec2(8, 4);
	style.ItemSpacing = ImVec2(10, 8);
	style.ItemInnerSpacing = ImVec2(8, 6);
	style.IndentSpacing = 25.0f;
	style.ScrollbarSize = 12.0f;
	style.GrabMinSize = 12.0f;

	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize = 1.0f;
	style.PopupBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;

	style.WindowRounding = 6.0f;
	style.ChildRounding = 6.0f;
	style.FrameRounding = 4.0f;
	style.PopupRounding = 6.0f;
	style.ScrollbarRounding = 6.0f;
	style.GrabRounding = 4.0f;
	style.TabRounding = 4.0f;

	// Load font if available
	ImGuiIO& io = ImGui::GetIO();
	std::string fontPath = "assets/fonts/JetBrainsMono-Regular.ttf";
	if (std::filesystem::exists(fontPath))
	{
		io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f);
	}
}

// Initialize ImGui with Vulkan 1.4 features
std::expected<void, VulkanError> VulkanResourceHandler::initImGui(uint32_t imageCount)
{
	m_logger.debug("Initializing ImGui");

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();

	// Enable advanced features
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable keyboard controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable gamepad controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable docking

	// Setup ImGui style
	ImGui::StyleColorsDark();
	setupImGuiStyle();

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForVulkan(m_window, true);

	// Load Vulkan functions with support for dynamic rendering
	ImGui_ImplVulkan_LoadFunctions(
	        [](const char* function_name, void* user_data)
	        {
		        VulkanResourceHandler* handler = static_cast<VulkanResourceHandler*>(user_data);

		        // Map KHR variants to core functions if using Vulkan 1.3+
		        if (std::string_view(function_name) == "vkCmdBeginRenderingKHR")
			        return handler->m_instanceDispatchTable.getInstanceProcAddr("vkCmdBeginRendering");
		        if (std::string_view(function_name) == "vkCmdEndRenderingKHR")
			        return handler->m_instanceDispatchTable.getInstanceProcAddr("vkCmdEndRendering");

		        return handler->m_instanceDispatchTable.getInstanceProcAddr(function_name);
	        },
	        this);

	// Set up ImGui initialization info
	ImGui_ImplVulkan_InitInfo initInfo{};
	initInfo.Instance = m_instance.instance;
	initInfo.PhysicalDevice = m_device.physical_device;
	initInfo.Device = m_device.device;
	initInfo.QueueFamily = m_graphicsQueueFamily;
	initInfo.Queue = m_graphicsQueue;
	initInfo.PipelineCache = VK_NULL_HANDLE;
	initInfo.DescriptorPool = m_descriptorPool;
	initInfo.Subpass = 0;
	initInfo.MinImageCount = m_swapchain.image_count;
	initInfo.ImageCount = m_swapchain.image_count;
	initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	initInfo.Allocator = nullptr;
	initInfo.CheckVkResultFn = [](VkResult result)
	{
		if (result != VK_SUCCESS)
		{
			Logger::getInstance().error(std::format("ImGui Vulkan error: {}", static_cast<int>(result)));
			if (result < 0)
				std::abort();
		}
	};

	// Enable dynamic rendering for Vulkan 1.3+
	initInfo.UseDynamicRendering = m_usesDynamicRendering;

	// Set up pipeline rendering info for dynamic rendering
	VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
	if (m_usesDynamicRendering)
	{
		pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		pipelineRenderingInfo.colorAttachmentCount = 1;
		pipelineRenderingInfo.pColorAttachmentFormats = &m_swapchain.image_format;
		pipelineRenderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
		pipelineRenderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
		initInfo.PipelineRenderingCreateInfo = pipelineRenderingInfo;
	}

	// Initialize ImGui for Vulkan
	initInfo.RenderPass = m_renderPass;
	ImGui_ImplVulkan_Init(&initInfo);

	// Load custom fonts
	if (auto result = loadFonts(m_logger); !result)
	{
		m_logger.warning(std::format("Font loading issue: {}", result.error()));
	}

	// Upload fonts
	{
		// Create temporary command buffer for font upload
		VkCommandBuffer commandBuffer;
		VkCommandBufferAllocateInfo allocInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = m_commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1
		};

		VkResult result = m_dispatchTable.allocateCommandBuffers(&allocInfo, &commandBuffer);
		if (result != VK_SUCCESS)
		{
			m_logger.error(std::format("Failed to allocate command buffer for ImGui font upload: {}", static_cast<int>(result)));
			return std::unexpected(VulkanError::CommandBufferAllocationFailed);
		}

		// Begin command buffer
		VkCommandBufferBeginInfo beginInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };

		m_dispatchTable.beginCommandBuffer(commandBuffer, &beginInfo);

		// Create font texture
		ImGui_ImplVulkan_CreateFontsTexture(); // Either pass the command buffer or use the default version

		// End and submit command buffer
		m_dispatchTable.endCommandBuffer(commandBuffer);

		VkSubmitInfo submitInfo{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commandBuffer };

		m_dispatchTable.queueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
		m_dispatchTable.queueWaitIdle(m_graphicsQueue);

		// IMPORTANT: DON'T destroy font textures here!
		// ImGui_ImplVulkan_DestroyFontsTexture();

		// Free command buffer
		m_dispatchTable.freeCommandBuffers(m_commandPool, 1, &commandBuffer);
	}

	// Add cleanup function for ImGui
	addCleanupFunction(
	        [this]()
	        {
		        m_logger.debug("Shutting down ImGui");
		        ImGui_ImplVulkan_Shutdown();
		        ImGui_ImplGlfw_Shutdown();
		        ImGui::DestroyContext();
	        },
	        5); // Very low priority - destroy very early

	m_logger.debug("ImGui initialized successfully");
	return {};
}

// Begin frame rendering with Vulkan 1.4 dynamic rendering
std::expected<void, VulkanError> VulkanResourceHandler::beginFrame(uint32_t& imageIndex, uint32_t currentFrame)
{
	// Wait for previous frame to finish
	VkResult waitResult = m_dispatchTable.waitForFences(1, &m_inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
	if (waitResult != VK_SUCCESS)
	{
		m_logger.error(std::format("Failed to wait for fence: {}", static_cast<int>(waitResult)));
		return std::unexpected(VulkanError::SwapchainAcquisitionFailed);
	}

	// Acquire next swapchain image
	VkResult result = m_dispatchTable.acquireNextImageKHR(m_swapchain.swapchain, UINT64_MAX, m_imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		// Swapchain is out of date (e.g., window resized)
		int width, height;
		glfwGetFramebufferSize(m_window, &width, &height);
		if (width > 0 && height > 0)
		{
			auto recreateResult = recreateSwapchain(width, height);
			if (!recreateResult)
			{
				return std::unexpected(recreateResult.error());
			}
		}
		return std::unexpected(VulkanError::SwapchainAcquisitionFailed);
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		m_logger.error(std::format("Failed to acquire swapchain image: {}", static_cast<int>(result)));
		return std::unexpected(VulkanError::SwapchainAcquisitionFailed);
	}

	// Check if a previous frame is using this image
	if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE)
	{
		m_dispatchTable.waitForFences(1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
	}

	// Mark the image as now being in use by this frame
	m_imagesInFlight[imageIndex] = m_inFlightFences[currentFrame];

	// Reset fence for this frame
	m_dispatchTable.resetFences(1, &m_inFlightFences[currentFrame]);

	// Reset and begin command buffer
	m_currentCommandBuffer = m_commandBuffers[imageIndex];
	m_dispatchTable.resetCommandBuffer(m_currentCommandBuffer, 0);

	VkCommandBufferBeginInfo beginInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = 0 };

	if (m_dispatchTable.beginCommandBuffer(m_currentCommandBuffer, &beginInfo) != VK_SUCCESS)
	{
		m_logger.error("Failed to begin recording command buffer");
		return std::unexpected(VulkanError::CommandBufferBeginFailed);
	}

	// If using dynamic rendering, set up the rendering directly
	if (m_usesDynamicRendering)
	{
		// Transition the image layout for rendering
		VkImageMemoryBarrier2 imageBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = m_swapchainImages[imageIndex],
			.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
		};

		VkDependencyInfo dependencyInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &imageBarrier };

		m_dispatchTable.cmdPipelineBarrier2(m_currentCommandBuffer, &dependencyInfo);

		// Begin dynamic rendering
		VkRenderingAttachmentInfo colorAttachment{ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = m_swapchainImageViews[imageIndex],
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = { .color = { 0.1f, 0.1f, 0.1f, 1.0f } } };

		VkRenderingInfo renderingInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = { .offset = { 0, 0 }, .extent = m_swapchain.extent },
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachment
		};

		m_dispatchTable.cmdBeginRendering(m_currentCommandBuffer, &renderingInfo);
	}
	else
	{
		// Fall back to traditional render pass
		VkRenderPassBeginInfo renderPassInfo{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = m_renderPass,
			.framebuffer = m_framebuffers[imageIndex],
			.renderArea = { .offset = { 0, 0 }, .extent = m_swapchain.extent }
		};

		VkClearValue clearColor = {
			.color = { 0.1f, 0.1f, 0.1f, 1.0f }
		};
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearColor;

		m_dispatchTable.cmdBeginRenderPass(m_currentCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
	}

	// Begin ImGui frame
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	return {};
}

// End frame rendering and submit with Vulkan 1.4 features
std::expected<void, VulkanError> VulkanResourceHandler::endFrame(uint32_t imageIndex, uint32_t currentFrame)
{
	// Render ImGui
	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_currentCommandBuffer);

	// End rendering based on mode
	if (m_usesDynamicRendering)
	{
		// End dynamic rendering
		m_dispatchTable.cmdEndRendering(m_currentCommandBuffer);

		// Transition the image layout for presentation
		VkImageMemoryBarrier2 imageBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			.dstAccessMask = 0,
			.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = m_swapchainImages[imageIndex],
			.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 }
		};

		VkDependencyInfo dependencyInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &imageBarrier };

		m_dispatchTable.cmdPipelineBarrier2(m_currentCommandBuffer, &dependencyInfo);
	}
	else
	{
		// End traditional render pass
		m_dispatchTable.cmdEndRenderPass(m_currentCommandBuffer);
	}

	// End command buffer
	VkResult endResult = m_dispatchTable.endCommandBuffer(m_currentCommandBuffer);
	if (endResult != VK_SUCCESS)
	{
		m_logger.error(std::format("Failed to record command buffer: {}", static_cast<int>(endResult)));
		return std::unexpected(VulkanError::CommandBufferBeginFailed);
	}

	// Submit command buffer with appropriate synchronization
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

	VkSubmitInfo submitInfo{ .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &m_imageAvailableSemaphores[currentFrame],
		.pWaitDstStageMask = waitStages,
		.commandBufferCount = 1,
		.pCommandBuffers = &m_currentCommandBuffer,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &m_renderFinishedSemaphores[currentFrame] };

	VkResult submitResult = m_dispatchTable.queueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[currentFrame]);
	if (submitResult != VK_SUCCESS)
	{
		m_logger.error(std::format("Failed to submit draw command buffer: {}", static_cast<int>(submitResult)));
		return std::unexpected(VulkanError::DrawCommandSubmissionFailed);
	}

	// Present the frame
	VkPresentInfoKHR presentInfo{ .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &m_renderFinishedSemaphores[currentFrame],
		.swapchainCount = 1,
		.pSwapchains = &m_swapchain.swapchain,
		.pImageIndices = &imageIndex };

	VkResult presentResult = m_dispatchTable.queuePresentKHR(m_presentQueue, &presentInfo);

	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
	{
		// Get new window size and recreate swapchain
		int width, height;
		glfwGetFramebufferSize(m_window, &width, &height);

		if (width > 0 && height > 0)
		{
			if (auto recreateResult = recreateSwapchain(width, height); !recreateResult)
			{
				return std::unexpected(recreateResult.error());
			}

			if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
			{
				// Only return an error for out-of-date, not for suboptimal
				return std::unexpected(VulkanError::PresentationFailed);
			}
		}
	}
	else if (presentResult != VK_SUCCESS)
	{
		m_logger.error(std::format("Failed to present swapchain image: {}", static_cast<int>(presentResult)));
		return std::unexpected(VulkanError::PresentationFailed);
	}

	return {};
}

// Recreate swapchain (for window resize) with Vulkan 1.4 support
std::expected<void, VulkanError> VulkanResourceHandler::recreateSwapchain(uint32_t width, uint32_t height)
{
	m_logger.debug(std::format("Recreating swapchain for size {}x{}", width, height));

	// Wait for device to be idle
	m_dispatchTable.deviceWaitIdle();

	// Clean up old framebuffers
	for (auto framebuffer: m_framebuffers)
	{
		m_dispatchTable.destroyFramebuffer(framebuffer, nullptr);
	}
	m_framebuffers.clear();

	// Create new swapchain and related resources with error handling
	if (auto result = initSwapchain(width, height); !result)
	{
		return std::unexpected(result.error());
	}

	if (auto result = initFramebuffers(); !result)
	{
		return std::unexpected(result.error());
	}

	m_logger.debug("Swapchain recreated successfully");
	return {};
}

// Clean up swapchain and related resources with Vulkan 1.4 support
void VulkanResourceHandler::cleanupSwapchain()
{
	m_logger.debug("Cleaning up swapchain resources");

	// Clean up framebuffers
	for (auto framebuffer: m_framebuffers)
	{
		m_dispatchTable.destroyFramebuffer(framebuffer, nullptr);
	}
	m_framebuffers.clear();

	// Using VkBootstrap's cleanup to destroy the swapchain
	if (m_swapchain.swapchain != VK_NULL_HANDLE)
	{
		vkb::destroy_swapchain(m_swapchain);
	}
}

// Clean up all resources in the correct order
void VulkanResourceHandler::cleanupAll()
{
	m_logger.debug("Cleaning up all Vulkan resources");

	// Wait for device to finish operations
	if (m_device.device != VK_NULL_HANDLE)
	{
		m_dispatchTable.deviceWaitIdle();
	}

	// Sort cleanup functions by priority (higher priority = later cleanup)
	std::sort(m_cleanupFunctions.begin(), m_cleanupFunctions.end(), [](const ResourceCleanupFunc& a, const ResourceCleanupFunc& b) { return a.priority < b.priority; });

	// Execute cleanup functions in order
	for (const auto& cleanup: m_cleanupFunctions)
	{
		cleanup.cleanup();
	}

	m_cleanupFunctions.clear();
	m_logger.debug("All Vulkan resources cleaned up successfully");
}
