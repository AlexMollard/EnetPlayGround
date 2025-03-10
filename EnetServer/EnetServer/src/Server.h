#pragma once

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#include <atomic>
#include <corecrt.h>
#include <cstdint>
#include <deque>
#include <enet/enet.h>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vadefs.h>
#include <vector>
#include <future>

// Configuration
#include "Constants.h"
#include "DatabaseManager.h"
#include "Logger.h"
#include "PluginManager.h"
#include "SpatialGrid.h"
#include "Structs.h"
#include "ThreadManager.h"
#include "PacketManager.h"

// Command handler function type
using CommandHandler = std::function<void(const Player&, const std::vector<std::string>&)>;

// Server stats
struct ServerStats
{
	uint32_t startTime = 0;
	uint32_t totalConnections = 0;
	uint32_t authFailures = 0;
	uint32_t maxConcurrentPlayers = 0;
	uint32_t totalPacketsSent = 0;
	uint32_t totalPacketsReceived = 0;
	uint32_t totalBytesSent = 0;
	uint32_t totalBytesReceived = 0;
	uint32_t chatMessagesSent = 0;

	ServerStats();
	static uint32_t getCurrentTimeMs();

	uint32_t getUptimeSeconds() const;
};

// Config options
struct ServerConfig
{
	uint16_t port = DEFAULT_PORT;
	size_t maxPlayers = MAX_PLAYERS;
	uint32_t broadcastRateMs = BROADCAST_RATE_MS;
	uint32_t timeoutMs = PLAYER_TIMEOUT_MS;
	uint32_t saveIntervalMs = SAVE_INTERVAL_MS;
	bool enableMovementValidation = MOVEMENT_VALIDATION;
	float maxMovementSpeed = MAX_MOVEMENT_SPEED;
	float interestRadius = INTEREST_RADIUS;
	std::string adminPassword = ADMIN_PASSWORD;
	bool logToConsole = true;
	bool logToFile = true;
	int logLevel = 1; // 0=errors only, 1=normal, 2=debug
	bool enableChat = true;
	Position spawnPosition = { DEFAULT_SPAWN_X, DEFAULT_SPAWN_Y, DEFAULT_SPAWN_Z };

	// Database configuration
	std::string dbHost = DB_HOST;
	std::string dbUser = DB_USER;
	std::string dbPassword = DB_PASSWORD;
	std::string dbName = DB_NAME;
	int dbPort = DB_PORT;
	bool useDatabase = USE_DATABASE;
};

// Packet stats
struct PacketStats
{
	std::atomic<uint32_t> totalBytesSent{ 0 };
	std::atomic<uint32_t> totalBytesReceived{ 0 };
};

namespace GameResources
{
	// Helper to create resource IDs with the right type
	template<typename T>
	ResourceId create(const std::string& name)
	{
		return ThreadManager::createResourceId<T>(name);
	}

	// Main server resources
	const ResourceId PlayersId = create<Player>("players");
	const ResourceId AuthId = create<AuthData>("auth");
	const ResourceId ChatId = create<ChatMessage>("chat");
	const ResourceId NetworkId = create<ENetEvent>("network");
	const ResourceId SpatialGridId = create<SpatialGrid>("spatialGrid");
	const ResourceId PluginsId = create<PluginManager>("plugins");
	const ResourceId ConfigId = create<ServerConfig>("config");
	const ResourceId DatabaseId = create<DatabaseManager>("database");
	const ResourceId PeerDataId = create<void*>("peerData");
	const ResourceId PeerStatsId = create<PacketStats>("peerStats");
}

// Main game server class
class GameServer
{
public:
	friend class PluginManager;

	// Constructor and destructor
	GameServer();
	~GameServer();

	// Public methods
	bool initialize();
	void start();
	void shutdown();
	void run();

	bool loadPlugin(const std::string& path);
	bool unloadPlugin(const std::string& name);
	bool reloadPlugin(const std::string& name);
	bool loadPluginsFromDirectory(const std::string& directory);

	std::vector<std::string> getLoadedPlugins() const;

	void broadcastSystemMessage(const std::string& message);

	std::vector<std::string> getOnlinePlayerNames();

	Logger logger;

private:
	// Packet manager instance
	PacketManager packetManager;

	// Network
	ENetHost* server = nullptr;
	bool isRunning = false;
	bool stopRequested = false;

	// Thread Manager
	ThreadManager threadManager;

	// Periodic task handles - these will hold futures for repeating tasks
	std::future<void> networkTaskFuture;
	std::future<void> updateTaskFuture;
	std::future<void> saveTaskFuture;

	// Configuration
	ServerConfig config;

	// Player data
	std::unordered_map<uint32_t, Player> players;
	uint32_t nextPlayerId = 1;

	// Authentication storage
	std::unordered_map<std::string, AuthData> authenticatedPlayers;

	// Chat history
	std::deque<ChatMessage> chatHistory;

	// Command handlers
	std::unordered_map<std::string, CommandHandler> commandHandlers;

	// Server stats
	ServerStats stats;

	// Spatial partitioning
	SpatialGrid spatialGrid;

	// Database manager
	DatabaseManager dbManager;

	// Utilities
	std::thread networkThread;
	std::thread updateThread;
	std::thread saveThread;

	// Stats tracking
	std::unordered_map<uintptr_t, PacketStats> peerStats;

	// Pliugins
	std::unique_ptr<PluginManager> pluginManager;
	uint32_t pluginCheckIntervalMs = 5000; // Check for plugin updates every 5 seconds
	uint32_t lastPluginCheckTime = 0;

	// Private methods
	void networkTaskFunc();
	void updateTaskFunc();
	void saveTaskFunc();

	// Method to schedule recurring tasks
	void scheduleRecurringTask(std::function<void()> task, uint32_t intervalMs, std::future<void>& futureHandle, const std::string& taskName);

	void handleClientConnect(const ENetEvent& event);
	void handleClientMessage(const ENetEvent& event);
	void handleClientDisconnect(const ENetEvent& event);
	void handleAuthMessage(const std::string& authDataStr, ENetPeer* peer);
	void handleRegistration(const Player& player, const std::string& username, const std::string& password);
	void syncPlayerStats();
	void handleDeltaPositionUpdate(uint32_t playerId, const std::string& deltaData);
	void handleSendPosition(uint32_t playerId);
	void handleChatMessage(const Player& player, const std::string& message);
	void handlePingMessage(const Player& player, const std::string& pingData);
	void handleCommandMessage(const Player& player, const std::string& commandStr);

    void sendPacket(ENetPeer* peer, const GameProtocol::Packet& packet, bool reliable);
    void sendSystemMessage(const Player& player, const std::string& message);
    void sendAuthResponse(ENetPeer* peer, bool success, const std::string& message, uint32_t playerId = 0);
    void sendTeleport(const Player& player, const Position& position);
    void broadcastChatMessage(const std::string& sender, const std::string& message);
    void broadcastWorldState();
	void handlePacket(const Player& player, std::unique_ptr<GameProtocol::Packet> packet);

	void handlePositionUpdate(uint32_t playerId, const Position& newPos);

	void checkTimeouts();
	void savePlayerData(const std::string& username, const Position& lastPos);
	void loadAuthData();
	void saveAuthData();
	void loadConfig();
	void createDefaultConfig();
	void initializeCommandHandlers();
	bool kickPlayer(const std::string& playerName, const std::string& adminName = "Server");
	bool banPlayer(const std::string& playerName, const std::string& adminName = "Server");
	bool setPlayerAdmin(const std::string& playerName, bool isAdmin);
	void printServerStatus();
	void printPlayerList();
	void printConsoleHelp();
	void initializePluginCommandHandlers();
	void initializePluginSystem();
	bool initializeDatabase();
};
