#include "PluginManager.h"

#include <stdexcept>
#include <stop_token>
#include <thread>

#include "Logger.h"
#include "Server.h"

#ifdef _WIN32
#	define LOAD_LIBRARY(path) LoadLibraryA(path.c_str())
#	define CLOSE_LIBRARY(handle) FreeLibrary(handle)
#	define GET_SYMBOL(handle, name) GetProcAddress(handle, name)
#else
#	include <dlfcn.h>
#	define LOAD_LIBRARY(path) dlopen(path.c_str(), RTLD_NOW)
#	define CLOSE_LIBRARY(handle) dlclose(handle)
#	define GET_SYMBOL(handle, name) dlsym(handle, name)
#endif

PluginManager::PluginManager(GameServer* server)
      : server(server)
{
}

PluginManager::~PluginManager()
{
	// Unload all plugins
	std::vector<std::string> pluginNames;
	{
		std::shared_lock lock(pluginsMutex);
		for (const auto& pair: plugins)
		{
			pluginNames.push_back(pair.first);
		}
	}

	for (const auto& name: pluginNames)
	{
		unloadPlugin(name);
	}
}

bool PluginManager::loadPlugin(const std::string& path)
{
	// Load the library first without locking to minimize lock contention
	LibraryHandle handle = loadLibrary(path);
	if (!handle)
	{
#ifdef _WIN32
		DWORD error = GetLastError();
		char errorMsg[256];
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errorMsg, sizeof(errorMsg), NULL);
		server->logger.error("Failed to load plugin library: " + path + ", error: " + errorMsg);
#else
		server->logger.error("Failed to load plugin library: " + path + ", error: " + dlerror());
#endif
		return false;
	}

	// Get the create and destroy functions
	CreatePluginFunc createFunc = reinterpret_cast<CreatePluginFunc>(getSymbol(handle, "CreatePlugin"));
	DestroyPluginFunc destroyFunc = reinterpret_cast<DestroyPluginFunc>(getSymbol(handle, "DestroyPlugin"));

	if (!createFunc || !destroyFunc)
	{
		server->logger.error("Plugin does not export required functions: " + path);
		closeLibrary(handle);
		return false;
	}

	// Create the plugin instance
	IPlugin* plugin = nullptr;
	try
	{
		plugin = createFunc();
	}
	catch (const std::exception& e)
	{
		server->logger.error("Exception while creating plugin: " + std::string(e.what()));
		closeLibrary(handle);
		return false;
	}

	if (!plugin)
	{
		server->logger.error("Failed to create plugin instance: " + path);
		closeLibrary(handle);
		return false;
	}

	server->logger.info("Successfully loaded plugin: " + path);

	// Initialize the plugin
	bool initSuccess = false;
	try
	{
		// Create the functions structure on the stack instead of heap
		ServerFunctions functions;

		// Set up function pointers using std::function (ensure ServerFunctions struct is updated)
		functions.broadcastSystemMessage = [this](const std::string& msg) { server->broadcastSystemMessage(msg); };

		functions.sendSystemMessage = [this](Player& player, const std::string& msg) { server->sendSystemMessage(player, msg); };

		// Make sure this is actually defined in your ServerFunctions struct
		functions.getLogger = [this]() -> Logger* { return &server->logger; };

		// Pass both server and functions pointers according to the updated interface
		initSuccess = plugin->onLoad(&functions);
	}
	catch (const std::exception& e)
	{
		server->logger.error("Exception during plugin onLoad: " + std::string(e.what()));
		destroyFunc(plugin);
		closeLibrary(handle);
		return false;
	}

	if (!initSuccess)
	{
		server->logger.error("Plugin failed to initialize: " + path);
		destroyFunc(plugin);
		closeLibrary(handle);
		return false;
	}

	std::string pluginName = plugin->getName();

	// Now lock for modifying the plugins map
	{
		if (!tryAcquireUniqueLock())
		{
			server->logger.error("Timed out waiting for unique lock while loading plugin: " + path);
			// Clean up allocated resources
			return false;
		}

		// Check if plugin already exists
		if (plugins.find(pluginName) != plugins.end())
		{
			releaseUniqueLock();
			// Clean up as in original
			return false;
		}

		// Get last modified time of the file
		std::filesystem::file_time_type lastModified;
		try
		{
			lastModified = std::filesystem::last_write_time(path);
		}
		catch (const std::exception& e)
		{
			server->logger.warning("Could not get last modified time for plugin: " + std::string(e.what()));
			// Still continue loading the plugin
		}

		// Store plugin info
		PluginInfo info;
		info.handle = handle;
		info.instance = plugin;
		info.createFunc = createFunc;
		info.destroyFunc = destroyFunc;
		info.path = path;
		info.name = pluginName;
		info.isLoaded = true;
		info.lastModified = lastModified;

		plugins[pluginName] = std::move(info);
	}

	//server->logger.info("Plugin loaded: " + pluginName + " v" + plugin->getVersion() + " by " + plugin->getAuthor());

	releaseUniqueLock();
	return true;
}

bool PluginManager::unloadPlugin(const std::string& name)
{
	PluginInfo pluginToUnload;
	bool found = false;

	// First get plugin info and remove from map under lock
	if (!tryAcquireUniqueLock())
	{
		server->logger.error("Timed out waiting for unique lock while unloading plugin: " + name);
		return false;
	}

	auto it = plugins.find(name);
	if (it == plugins.end())
	{
		server->logger.error("Plugin not found: " + name);
		releaseUniqueLock();
		return false;
	}

	if (!it->second.isLoaded)
	{
		releaseUniqueLock();
		return true; // Already unloaded
	}

	// Make a copy of the plugin info
	pluginToUnload = it->second;
	found = true;

	// Remove from map immediately to prevent other threads from accessing
	plugins.erase(it);

	releaseUniqueLock();

	// Now unload the plugin outside the lock
	if (found)
	{
		// First mark as unloaded to prevent other threads from using it
		pluginToUnload.isLoaded.store(false);

		// Call plugin unload method
		try
		{
			if (pluginToUnload.instance)
			{
				pluginToUnload.instance->onUnload();
			}
		}
		catch (const std::exception& e)
		{
			server->logger.error("Exception in plugin '" + name + "' onUnload: " + e.what());
			// Continue with unload despite error
		}

		// Destroy the instance
		try
		{
			if (pluginToUnload.instance && pluginToUnload.destroyFunc)
			{
				pluginToUnload.destroyFunc(pluginToUnload.instance);
			}
		}
		catch (const std::exception& e)
		{
			server->logger.error("Exception destroying plugin '" + name + "': " + e.what());
		}

		// Close the library
		try
		{
			if (pluginToUnload.handle)
			{
				closeLibrary(pluginToUnload.handle);
			}
		}
		catch (const std::exception& e)
		{
			server->logger.error("Exception closing plugin library '" + name + "': " + e.what());
		}

		server->logger.info("Plugin unloaded: " + name);

		return true;
	}

	return false;
}

bool PluginManager::reloadPlugin(const std::string& name)
{
	std::string path;

	if (!tryAcquireSharedLock())
	{
		server->logger.error("Timed out waiting for shared lock while reloading plugin: " + name);
		return false;
	}

	auto it = plugins.find(name);
	if (it == plugins.end())
	{
		server->logger.error("Plugin not found: " + name);
		releaseSharedLock();
		return false;
	}

	path = it->second.path;
	releaseSharedLock();

	if (!unloadPlugin(name))
	{
		return false;
	}

	// Small delay to ensure the DLL/shared library is fully unloaded
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	return loadPlugin(path);
}

bool PluginManager::loadPluginsFromDirectory(const std::string& directory)
{
	server->logger.info("Loading plugins from directory: " + directory);

	try
	{
		if (!std::filesystem::exists(directory))
		{
			server->logger.info("Creating plugin directory: " + directory);
			std::filesystem::create_directories(directory);
			return true;
		}

		if (!std::filesystem::is_directory(directory))
		{
			server->logger.error("Plugin path is not a directory: " + directory);
			return false;
		}

		bool anyLoaded = false;

		for (const auto& entry: std::filesystem::directory_iterator(directory))
		{
			if (!entry.is_regular_file())
			{
				continue;
			}

			std::string extension = entry.path().extension().string();

#ifdef _WIN32
			if (extension == ".dll")
#else
			if (extension == ".so")
#endif
			{
				std::string path = entry.path().string();
				if (loadPlugin(path))
				{
					anyLoaded = true;
				}
			}
		}

		return anyLoaded;
	}
	catch (const std::exception& e)
	{
		server->logger.error("Error loading plugins from directory: " + std::string(e.what()));
		return false;
	}
}

void PluginManager::checkForPluginUpdates()
{
	// First get all plugin names without holding a prolonged lock
	std::vector<std::string> pluginNames;

	if (!tryAcquireSharedLock())
	{
		server->logger.warning("Timed out waiting for shared lock in checkForPluginUpdates - skipping this cycle");
		return;
	}

	for (const auto& pair: plugins)
	{
		if (pair.second.isLoaded)
		{
			pluginNames.push_back(pair.first);
		}
	}

	releaseSharedLock();

	// Check each plugin individually and reload if needed
	std::vector<std::pair<std::string, std::string>> pluginsToReload;

	for (const auto& name: pluginNames)
	{
		auto result = getPluginPathIfModified(name);
		if (result)
		{
			pluginsToReload.push_back(*result);
		}
	}

	// Now reload each modified plugin
	for (const auto& [name, path]: pluginsToReload)
	{
		server->logger.info("Auto-reloading modified plugin: " + name);

		if (unloadPlugin(name))
		{
			// Small delay to ensure the DLL/shared library is fully unloaded
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			loadPlugin(path);
		}
	}
}

bool PluginManager::isPluginModified(const std::string& name)
{
	std::shared_lock lock(pluginsMutex);

	auto it = plugins.find(name);
	if (it == plugins.end() || !it->second.isLoaded)
	{
		return false;
	}

	try
	{
		auto currentModTime = std::filesystem::last_write_time(it->second.path);
		return currentModTime > it->second.lastModified;
	}
	catch (const std::exception& e)
	{
		server->logger.error("Error checking plugin modification time: " + std::string(e.what()));
		return false;
	}
}

IPlugin* PluginManager::getPlugin(const std::string& name)
{
	std::shared_lock lock(pluginsMutex);

	auto it = plugins.find(name);
	if (it != plugins.end() && it->second.isLoaded)
	{
		return it->second.instance;
	}

	return nullptr;
}

std::vector<std::string> PluginManager::getLoadedPlugins() const
{
	std::shared_lock lock(pluginsMutex);

	std::vector<std::string> result;
	result.reserve(plugins.size());
	for (const auto& pair: plugins)
	{
		if (pair.second.isLoaded)
		{
			result.push_back(pair.first);
		}
	}

	return result;
}

// Event dispatching methods with thread-safe implementation
void PluginManager::dispatchPlayerConnect(Player& player)
{
	// Get a thread-safe copy of plugin instances
	auto pluginInstances = getPluginInstancesCopy();

	// Call the plugins without holding the lock
	for (const auto& [name, plugin]: pluginInstances)
	{
		try
		{
			plugin->onPlayerConnect(player);
		}
		catch (const std::exception& e)
		{
			server->logger.error("Exception in plugin '" + name + "' onPlayerConnect: " + std::string(e.what()));
		}
	}
}

void PluginManager::dispatchPlayerLogin(Player& player)
{
	// Get a thread-safe copy of plugin instances
	auto pluginInstances = getPluginInstancesCopy();

	// Call the plugins without holding the lock
	for (const auto& [name, plugin]: pluginInstances)
	{
		try
		{
			plugin->onPlayerLogin(player);
		}
		catch (const std::exception& e)
		{
			server->logger.error("Exception in plugin '" + name + "' onPlayerLogin: " + std::string(e.what()));
		}
	}
}

void PluginManager::dispatchPlayerDisconnect(Player& player)
{
	auto pluginInstances = getPluginInstancesCopy();

	for (const auto& [name, plugin]: pluginInstances)
	{
		try
		{
			plugin->onPlayerDisconnect(player);
		}
		catch (const std::exception& e)
		{
			server->logger.error("Exception in plugin '" + name + "' onPlayerDisconnect: " + std::string(e.what()));
		}
	}
}

void PluginManager::dispatchPlayerMessage(Player& player, const std::string& message)
{
	auto pluginInstances = getPluginInstancesCopy();

	for (const auto& [name, plugin]: pluginInstances)
	{
		try
		{
			plugin->onPlayerMessage(player, message);
		}
		catch (const std::exception& e)
		{
			server->logger.error("Exception in plugin '" + name + "' onPlayerMessage: " + std::string(e.what()));
		}
	}
}

void PluginManager::dispatchPlayerMove(Player& player, const Position& oldPos, const Position& newPos)
{
	auto pluginInstances = getPluginInstancesCopy();

	for (const auto& [name, plugin]: pluginInstances)
	{
		try
		{
			plugin->onPlayerMove(player, oldPos, newPos);
		}
		catch (const std::exception& e)
		{
			server->logger.error("Exception in plugin '" + name + "' onPlayerMove: " + std::string(e.what()));
		}
	}
}

void PluginManager::dispatchServerTick()
{
	auto pluginInstances = getPluginInstancesCopy();

	for (const auto& [name, plugin]: pluginInstances)
	{
		try
		{
			plugin->onServerTick();
		}
		catch (const std::exception& e)
		{
			server->logger.error("Exception in plugin '" + name + "' onServerTick: " + std::string(e.what()));
		}
	}
}

bool PluginManager::dispatchPlayerCommand(Player& player, const std::string& command, const std::vector<std::string>& args)
{
	auto pluginInstances = getPluginInstancesCopy();
	bool commandHandled = false;
	for (const auto& [name, plugin]: pluginInstances)
	{
		try
		{
			if (plugin->onPlayerCommand(player, command, args))
			{
				commandHandled = true;
			}
		}
		catch (const std::exception& e)
		{
			server->logger.error("Exception in plugin '" + name + "' onPlayerCommand: " + std::string(e.what()));
		}
	}

	return commandHandled;
}

void PluginManager::dispatchChatMessage(const std::string& sender, const std::string& message)
{
	auto pluginInstances = getPluginInstancesCopy();

	for (const auto& [name, plugin]: pluginInstances)
	{
		try
		{
			plugin->onChatMessage(sender, message);
		}
		catch (const std::exception& e)
		{
			server->logger.error("Exception in plugin '" + name + "' onChatMessage: " + std::string(e.what()));
		}
	}
}

// Method to get plugin information without directly returning references
std::optional<std::pair<std::string, std::string>> PluginManager::getPluginPathIfModified(const std::string& name) const
{
	if (!tryAcquireSharedLock())
	{
		server->logger.error("Timed out waiting for shared lock while checking plugin: " + name);
		return std::nullopt;
	}

	auto it = plugins.find(name);
	if (it == plugins.end() || !it->second.isLoaded)
	{
		releaseSharedLock();
		return std::nullopt;
	}

	std::string path = it->second.path;
	bool isModified = false;

	try
	{
		auto currentModTime = std::filesystem::last_write_time(path);
		isModified = currentModTime > it->second.lastModified;
	}
	catch (const std::exception& e)
	{
		server->logger.error("Error checking modification time: " + std::string(e.what()));
		releaseSharedLock();
		return std::nullopt;
	}

	releaseSharedLock();

	if (isModified)
	{
		return std::make_pair(name, path);
	}
	return std::nullopt;
}

std::vector<std::pair<std::string, IPlugin*>> PluginManager::getPluginInstancesCopy() const
{
	std::shared_lock lock(pluginsMutex);
	std::vector<std::pair<std::string, IPlugin*>> instances;
	instances.reserve(plugins.size());
	for (const auto& pair: plugins)
	{
		if (pair.second.isLoaded && pair.second.instance)
		{
			instances.emplace_back(pair.first, pair.second.instance);
		}
	}
	return instances;
}

// Platform-specific library loading helpers
LibraryHandle PluginManager::loadLibrary(const std::string& path)
{
	return LOAD_LIBRARY(path);
}

void PluginManager::closeLibrary(LibraryHandle handle)
{
	if (handle)
	{
		CLOSE_LIBRARY(handle);
	}
}

void* PluginManager::getSymbol(LibraryHandle handle, const std::string& symbolName)
{
	return GET_SYMBOL(handle, symbolName.c_str());
}
