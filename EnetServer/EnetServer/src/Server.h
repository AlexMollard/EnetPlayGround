#pragma once

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <enet/enet.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Configuration
#include "Constants.h"
#include "Logger.h"
#include "SpatialGrid.h"
#include "Utils.h"

// Player authentication data
struct AuthData
{
	std::string username;
	std::string passwordHash;
	uint32_t playerId;
	std::time_t lastLoginTime;
	std::time_t registrationTime;
	Position lastPosition;
	std::string lastIpAddress;
	uint32_t loginCount;
	bool isAdmin;
	std::string securityQuestion;
	std::string securityAnswerHash;
};

// Player session data
struct Player
{
	uint32_t id;
	std::string name;
	Position position;
	Position lastValidPosition;
	ENetPeer* peer;
	uint32_t lastUpdateTime;
	uint32_t lastPingTime;
	uint32_t connectionStartTime;
	uint32_t failedAuthAttempts;
	uint32_t totalBytesReceived;
	uint32_t totalBytesSent;
	uint32_t pingMs;
	bool isAuthenticated;
	bool isAdmin;
	std::string ipAddress;
	std::set<uint32_t> visiblePlayers; // IDs of players currently visible to this player
	uint32_t lastPositionUpdateTime;   // Time of the last position update received
};

// Chat message
struct ChatMessage
{
	std::string sender;
	std::string content;
	uint32_t timestamp;
	bool isGlobal;
	bool isSystem;
	float range;              // For local chat, how far the message travels
	std::string targetPlayer; // For private messages
};

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

	// Utilities
	Logger logger;
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
	void sendSystemMessage(Player& player, const std::string& message);
	void sendSystemMessage(uint32_t playerId, const std::string& message);
	void sendTeleport(Player& player, const Position& position);
	void broadcastWorldState();
	void checkTimeouts();
	void broadcastSystemMessage(const std::string& message);
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
};
