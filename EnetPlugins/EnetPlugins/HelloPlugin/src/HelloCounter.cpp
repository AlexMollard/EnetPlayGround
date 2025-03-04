#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#include <chrono>
#include <unordered_map>

#include "PluginInterface.h"

class HelloPlugin : public IPlugin
{
public:
	HelloPlugin()
	      : lastBroadcastTime(0), broadcastIntervalMs(60000)
	{
	} // Broadcast every minute

	// Updated onLoad to accept function pointers
	bool onLoad(ServerFunctions* functions) override
	{
		if (functions != nullptr)
		{
			this->serverFuncs = *functions;
		}

		// Use a direct check to avoid crashes
		serverFuncs.getLogger()->info("HelloPlugin: Plugin loaded!");
		loaded = true;
		return true;
	}

	bool getLoaded() const override
	{
		return loaded;
	}

	void onUnload() override
	{
		serverFuncs.getLogger()->info("HelloPlugin: Plugin unloaded!");
	}

	// Information methods unchanged
	std::string getName() const override
	{
		return "HelloPlugin";
	}

	std::string getVersion() const override
	{
		return "1.0";
	}

	std::string getAuthor() const override
	{
		return "AlexM";
	}

	// Updated event hooks to use function pointers
	void onPlayerConnect(Player& player) override
	{
		serverFuncs.broadcastSystemMessage("A new user is trying to login!");
	}

	void onPlayerLogin(Player& player) override
	{
		serverFuncs.broadcastSystemMessage("Hello " + player.name + " im from the hello plugin!");
	}

	void onChatMessage(const std::string& sender, const std::string& message) override
	{
		if (message.find("hello") != std::string::npos)
		{
			helloCounter[sender]++;
			serverFuncs.broadcastSystemMessage(sender + " has said hello " + std::to_string(helloCounter[sender]) + " times!");
		}
	}

	bool onPlayerCommand(Player& player, const std::string& command, const std::vector<std::string>& args) override
	{
		if (command == "hello")
		{
			serverFuncs.sendSystemMessage(player, "Hello, " + player.name + "!");
			return true;
		}
		return false;
	}

	// onServerTick remains unchanged
	void onServerTick() override
	{
		uint32_t currentTime = getCurrentTimeMs();
		if (currentTime - lastBroadcastTime > broadcastIntervalMs)
		{
			serverFuncs.getLogger()->info("HelloPlugin: Plugin tick event");
			lastBroadcastTime = currentTime;
		}
	}

private:
	uint32_t lastBroadcastTime;
	uint32_t broadcastIntervalMs;
	std::unordered_map<std::string, int> helloCounter;
	bool loaded = false;

	uint32_t getCurrentTimeMs()
	{
		return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
	}
};

// Export the required functions
extern "C"
{
	PLUGIN_API IPlugin* CreatePlugin()
	{
		return new HelloPlugin();
	}

	PLUGIN_API void DestroyPlugin(IPlugin* plugin)
	{
		delete plugin;
	}
}
