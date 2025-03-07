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

// Configuration
#include "Constants.h"
#include "Logger.h"
#include "PluginManager.h"
#include "SpatialGrid.h"
#include "Structs.h"
#include "DatabaseManager.h"

// Command handler function type
using CommandHandler = std::function<void(Player&, const std::vector<std::string>&)>;

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

// Main game server class
class GameServer
{
public:
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

	void sendSystemMessage(Player& player, const std::string& message);
	void broadcastSystemMessage(const std::string& message);

	std::vector<std::string> getOnlinePlayerNames();

	Logger logger;

private:
	// Network
	ENetHost* server = nullptr;
	bool isRunning = false;

	// Configuration
	ServerConfig config;

	// Player data
	std::unordered_map<uint32_t, Player> players;
	std::mutex playersMutex;
	uint32_t nextPlayerId = 1;
	std::mutex peerDataMutex; // For thread-safe access to peer->data

	// Authentication storage
	std::unordered_map<std::string, AuthData> authenticatedPlayers;
	std::mutex authMutex;

	// Chat history
	std::deque<ChatMessage> chatHistory;
	std::mutex chatMutex;

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

	// Packet stats
	struct PacketStats
	{
		std::atomic<uint32_t> totalBytesSent{ 0 };
		std::atomic<uint32_t> totalBytesReceived{ 0 };
	};

	// Stats tracking
	std::unordered_map<uintptr_t, PacketStats> peerStats;
	std::mutex peerStatsMutex;

	// Pliugins
	std::unique_ptr<PluginManager> pluginManager;
	uint32_t pluginCheckIntervalMs = 5000; // Check for plugin updates every 5 seconds
	uint32_t lastPluginCheckTime = 0;

	// Private methods
	void networkThreadFunc();
	void updateThreadFunc();
	void saveThreadFunc();
	void handleClientConnect(const ENetEvent& event);
	void handleClientMessage(const ENetEvent& event);
	void handleClientDisconnect(const ENetEvent& event);
	void handleAuthMessage(const std::string& authDataStr, ENetPeer* peer);
	void handleRegistration(Player& player, const std::string& username, const std::string& password);
	void syncPlayerStats();
	void handleDeltaPositionUpdate(Player& player, const std::string& deltaData);
	void handleSendPosition(Player& player);
	void handleChatMessage(Player& player, const std::string& message);
	void handlePingMessage(Player& player, const std::string& pingData);
	void handleCommandMessage(Player& player, const std::string& commandStr);
	void sendAuthResponse(ENetPeer* peer, bool success, const std::string& message);
	void sendPacket(ENetPeer* peer, const std::string& message, bool reliable);
	void sendSystemMessage(uint32_t playerId, const std::string& message);
	void sendTeleport(Player& player, const Position& position);
	void broadcastWorldState();
	void checkTimeouts();
	void broadcastChatMessage(const std::string& sender, const std::string& message);
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
