#pragma once
#include <string>
#include <vector>
#include <Logger.h>
#include <functional>
#include "Structs.h"

// Define export/import macros
#ifdef _WIN32
#	ifdef PLUGIN_EXPORTS
#		define PLUGIN_API __declspec(dllexport)
#	else
#		define PLUGIN_API __declspec(dllimport)
#	endif
#else
#	define PLUGIN_API
#endif

// Function pointer structure for server operations
struct ServerFunctions
{
	std::function<void(const std::string&)> broadcastSystemMessage;
	std::function<void(const Player&, const std::string&)> sendSystemMessage;
	std::function<Logger*()> getLogger;
	// Ill add more soon
};

// Plugin interface
class PLUGIN_API IPlugin
{
public:
	virtual ~IPlugin() = default;

	// Updated lifecycle method with function pointers
	virtual bool onLoad(ServerFunctions* functions) = 0;
	virtual void onUnload() = 0;
	virtual bool getLoaded() const = 0;

	// Information methods
	virtual std::string getName() const = 0;
	virtual std::string getVersion() const = 0;
	virtual std::string getAuthor() const = 0;

	// Event hooks
	virtual void onPlayerConnect(Player& player)
	{
	}

	virtual void onPlayerLogin(Player& player)
	{
	}

	virtual void onPlayerDisconnect(Player& player)
	{
	}

	virtual void onPlayerMessage(Player& player, const std::string& message)
	{
	}

	virtual void onPlayerMove(Player& player, const Position& oldPos, const Position& newPos)
	{
	}

	virtual void onServerTick()
	{
	}

	virtual bool onPlayerCommand(const Player& player, const std::string& command, const std::vector<std::string>& args)
	{
		return false;
	}

	virtual void onChatMessage(const std::string& sender, const std::string& message)
	{
	}

protected:
	ServerFunctions serverFuncs;
};

// Plugin creation/destruction function signatures - these must be exported from the DLL
extern "C"
{
#ifdef _WIN32
	PLUGIN_API IPlugin* CreatePlugin();
	PLUGIN_API void DestroyPlugin(IPlugin* plugin);
#else
	IPlugin* CreatePlugin();
	void DestroyPlugin(IPlugin* plugin);
#endif
}

// Define function pointer types
typedef IPlugin* (*CreatePluginFunc)();
typedef void (*DestroyPluginFunc)(IPlugin*);
