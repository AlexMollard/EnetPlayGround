#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "PluginInterface.h"
#include "ThreadManager.h" // Added ThreadManager

#ifdef _WIN32
#	include <windows.h>
typedef HMODULE LibraryHandle;
#else
typedef void* LibraryHandle;
#endif

class GameServer;
struct Player;
struct Position;

struct PluginInfo
{
	LibraryHandle handle = nullptr;
	IPlugin* instance = nullptr;
	CreatePluginFunc createFunc = nullptr;
	DestroyPluginFunc destroyFunc = nullptr;
	std::string path;
	std::string name;
	std::atomic<bool> isLoaded{ false };
	std::filesystem::file_time_type lastModified;

	// Add copy constructor and assignment operator to handle atomic member
	PluginInfo() = default;

	PluginInfo(const PluginInfo& other)
	      : handle(other.handle), instance(other.instance), createFunc(other.createFunc), destroyFunc(other.destroyFunc), path(other.path), name(other.name), lastModified(other.lastModified)
	{
		isLoaded.store(other.isLoaded.load());
	}

	PluginInfo& operator=(const PluginInfo& other)
	{
		if (this != &other)
		{
			handle = other.handle;
			instance = other.instance;
			createFunc = other.createFunc;
			destroyFunc = other.destroyFunc;
			path = other.path;
			name = other.name;
			lastModified = other.lastModified;
			isLoaded.store(other.isLoaded.load());
		}
		return *this;
	}

	// Move operations should also be defined for completeness
	PluginInfo(PluginInfo&& other) noexcept
	      : handle(other.handle), instance(other.instance), createFunc(other.createFunc), destroyFunc(other.destroyFunc), path(std::move(other.path)), name(std::move(other.name)), lastModified(other.lastModified)
	{
		isLoaded.store(other.isLoaded.load());
		other.handle = nullptr;
		other.instance = nullptr;
		other.createFunc = nullptr;
		other.destroyFunc = nullptr;
		other.isLoaded.store(false);
	}

	PluginInfo& operator=(PluginInfo&& other) noexcept
	{
		if (this != &other)
		{
			handle = other.handle;
			instance = other.instance;
			createFunc = other.createFunc;
			destroyFunc = other.destroyFunc;
			path = std::move(other.path);
			name = std::move(other.name);
			lastModified = other.lastModified;
			isLoaded.store(other.isLoaded.load());

			other.handle = nullptr;
			other.instance = nullptr;
			other.createFunc = nullptr;
			other.destroyFunc = nullptr;
			other.isLoaded.store(false);
		}
		return *this;
	}
};

class PluginManager
{
public:
	PluginManager(GameServer* server);
	~PluginManager();

	// Plugin operations
	bool loadPlugin(const std::string& path);
	bool unloadPlugin(const std::string& name);
	bool reloadPlugin(const std::string& name);
	bool loadPluginsFromDirectory(const std::string& directory);
	void checkForPluginUpdates();

	// Plugin access
	IPlugin* getPlugin(const std::string& name);
	std::vector<std::string> getLoadedPlugins() const;

	// Event dispatching
	void dispatchPlayerConnect(Player& player);
	void dispatchPlayerLogin(Player& player);
	void dispatchPlayerDisconnect(Player& player);
	void dispatchPlayerMessage(Player& player, const std::string& message);
	void dispatchPlayerMove(Player& player, const Position& oldPos, const Position& newPos);
	void dispatchServerTick();
	bool dispatchPlayerCommand(const Player& player, const std::string& command, const std::vector<std::string>& args);
	void dispatchChatMessage(const std::string& sender, const std::string& message);

private:
	// Add these helper methods to safely acquire locks with timeouts
	bool tryAcquireSharedLock(std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) const
	{
		auto endTime = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < endTime)
		{
			if (pluginsMutex.try_lock_shared())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return false;
	}

	bool tryAcquireUniqueLock(std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
	{
		auto endTime = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < endTime)
		{
			if (pluginsMutex.try_lock())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return false;
	}

	void releaseSharedLock() const
	{
		pluginsMutex.unlock_shared();
	}

	void releaseUniqueLock()
	{
		pluginsMutex.unlock();
	}

	// Method to get plugin information without directly returning references
	std::optional<std::pair<std::string, std::string>> getPluginPathIfModified(const std::string& name) const;
	std::vector<std::pair<std::string, IPlugin*>> getPluginInstancesCopy() const;

	GameServer* server;
	std::unordered_map<std::string, PluginInfo> plugins;
	mutable std::shared_mutex pluginsMutex;

	// Reference to the ThreadManager from the GameServer
	ThreadManager* threadManager;

	// Internal helpers
	LibraryHandle loadLibrary(const std::string& path);
	void closeLibrary(LibraryHandle handle);
	void* getSymbol(LibraryHandle handle, const std::string& symbolName);
	bool isPluginModified(const std::string& name);
};
