#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#include <chrono>
#include <unordered_map>

#include "PluginInterface.h"
#include "IconsLucide.h"

class BogosPlugin : public IPlugin
{
public:
	BogosPlugin()
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
		serverFuncs.getLogger()->info("BogosPlugin: Plugin loaded!");
		loaded = true;
		return true;
	}

	bool getLoaded() const override
	{
		return loaded;
	}

	void onUnload() override
	{
		serverFuncs.getLogger()->info("BogosPlugin: Plugin unloaded!");
	}

	// Information methods unchanged
	std::string getName() const override
	{
		return "BogosPlugin";
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
	}

	void onPlayerLogin(Player& player) override
	{
	}

	void onChatMessage(const std::string& sender, const std::string& message) override
	{
		if (message.find("bogos") != std::string::npos)
		{
			helloCounter[sender]++;
			serverFuncs.broadcastSystemMessage("binted!\n" + std::string(ICON_LC_CAT));
		}
	}

	bool onPlayerCommand(const Player& player, const std::string& command, const std::vector<std::string>& args) override
	{
		if (command == "bogos")
		{
            serverFuncs.broadcastSystemMessage("binted!\n" + std::string(ICON_LC_CAT));
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
			serverFuncs.getLogger()->info("BogosPlugin: Plugin tick event");
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
		return new BogosPlugin();
	}

	PLUGIN_API void DestroyPlugin(IPlugin* plugin)
	{
		delete plugin;
	}
}
