#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <conio.h>
#include <ctime>
#include <deque>
#include <enet/enet.h>
#include <fstream>
#include <hello_imgui/hello_imgui.h>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <windows.h>

#include "Constants.h"
#include "Logger.h"

// Position structure
struct Position
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	bool operator==(const Position& other) const
	{
		return x == other.x && y == other.y && z == other.z;
	}

	bool operator!=(const Position& other) const
	{
		return !(*this == other);
	}
};

// Player info structure
struct PlayerInfo
{
	uint32_t id = 0;
	std::string name;
	Position position;
	uint32_t lastSeen = 0;
	std::string status;
};

// Chat message structure
struct ChatMessage
{
	std::string sender;
	std::string content;
	uint32_t timestamp;
};

// Main client class
class GameClient
{
private:
	// Network components
	ENetHost* client = nullptr;
	ENetPeer* server = nullptr;

	// Player data
	uint32_t myPlayerId = 0;
	std::string myPlayerName;
	std::string myPassword;
	Position myPosition;
	Position lastSentPosition;
	std::unordered_map<uint32_t, PlayerInfo> otherPlayers;
	std::mutex playersMutex;
	float movementThreshold = 0.1f;
	uint32_t positionUpdateRateMs = 100;
	uint32_t lastPositionUpdateTime = 0;
	bool useCompressedUpdates = true;

	// Server connection info
	std::string serverAddress = DEFAULT_SERVER;
	uint16_t serverPort = DEFAULT_PORT;

	// Status flags
	bool isConnected = false;
	bool isAuthenticated = false;
	bool shouldExit = false;
	bool reconnecting = false;

	// UI and display
	bool showDebug = false;
	bool showStats = false;
	bool showNetworkOptions = false;

	// Chat system
	std::deque<ChatMessage> chatMessages;
	std::mutex chatMutex;
	char chatInputBuffer[256] = { 0 };
	bool chatFocused = false;
	std::deque<std::string> commandHistory;
	int commandHistoryIndex = -1;

	// Stats and performance
	uint32_t packetsSent = 0;
	uint32_t packetsReceived = 0;
	uint32_t bytesSent = 0;
	uint32_t bytesReceived = 0;
	uint32_t lastPingTime = 0;
	uint32_t pingMs = 0;
	uint32_t lastNetworkActivity = 0;

	// Utility objects
	Logger logger;
	HANDLE consoleHandle;

	// Add these to the existing variables
	uint32_t lastServerResponseTime = 0;     // Timestamp of last server response
	uint32_t connectionCheckInterval = 1000; // Check connection every 1 second
	uint32_t serverResponseTimeout = 5000;   // Consider connection lost after 5 seconds of no response
	bool waitingForPingResponse = false;     // Flag to track ping responses
	uint32_t lastPingSentTime = 0;           // When the last ping was sent

	// Update timing
	uint32_t lastUpdateTime = 0;
	uint32_t lastConnectionCheckTime = 0;

public:
	// Constructor
	GameClient(const std::string& playerName = "", const std::string& password = "", bool debugMode = false);

	// Destructor
	~GameClient();

	// Initialize the client
	bool initialize();

	// Connect to the server
	bool connectToServer(const char* address, uint16_t port);

	// Attempt to reconnect to server
	bool reconnectToServer();

	// Authenticate with server
	bool authenticate();

	// Disconnect from the server
	void disconnect(bool showMessage = true);

	// Load saved credentials
	void loadCredentials();

	// Save credentials to file
	void saveCredentials();

	// Get credentials from user
	void getUserCredentials();

	// Send a packet to the server
	void sendPacket(const std::string& message, bool reliable = true);

	// Send a position update to the server
	void sendPositionUpdate();

	// Send ping to server to check connection
	void sendPing();

	void checkConnectionHealth();

	void handleServerDisconnection();

	// Send a chat message
	void sendChatMessage(const std::string& message);

	// Process a chat command
	void processChatCommand(const std::string& command);

	// Handle received packet
	void handlePacket(const ENetPacket* packet);

	// Helper function to split strings (similar to the one in server)
	std::vector<std::string> splitString(const std::string& str, char delimiter);

	// Parse world state update
	void parseWorldState(const std::string& stateData);

	// Add a chat message
	void addChatMessage(const std::string& sender, const std::string& content);

	// Clear chat history
	void clearChatMessages();

	void handleImGuiInput();

	// Process a single network update cycle
	void updateNetwork();

	void drawNetworkOptionsUI();

	// Draw the UI with ImGui
	void drawUI();

	// Get current time in milliseconds
	uint32_t getCurrentTimeMs();

	// Set up ImGui theme
	void setupImGuiTheme();
};
