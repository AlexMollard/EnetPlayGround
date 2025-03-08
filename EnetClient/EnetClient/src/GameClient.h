#pragma once

// Standard library includes
#include <deque>
#include <windows.h>
#include <winsock2.h>

// Project includes
#include <atomic>
#include <thread>

#include "AuthManager.h"
#include "Logger.h"
#include "NetworkManager.h"
#include "ThemeManager.h"
#include <corecrt.h>

//=============================================================================
// DATA STRUCTURES
//=============================================================================

/**
 * Position structure representing a 3D coordinate
 */
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

/**
 * Player information structure
 */
struct PlayerInfo
{
	uint32_t id = 0;
	std::string name;
	Position position;
	time_t lastSeen = 0;
	std::string status;
};

/**
 * Chat message structure
 */
struct ChatMessage
{
	std::string sender;
	std::string content;
	time_t timestamp = 0;
};

/**
 * Client connection states
 */
enum class ConnectionState
{
	Disconnected,
	LoginScreen,
	RegisterScreen,
	Connecting,
	Authenticating,
	Connected
};

//=============================================================================
// GAME CLIENT CLASS
//=============================================================================

/**
 * Main client class handling game state and UI
 */
class GameClient
{
public:
	//-------------------------------------------------------------------------
	// PUBLIC INTERFACE
	//-------------------------------------------------------------------------

	/**
     * Constructor
     * @param playerName Optional player name
     * @param password Optional password
     */
	GameClient();

	/**
     * Destructor
     */
	~GameClient();

	/**
     * Initialize the client
     * @return True if initialization succeeded
     */
	bool initialize();

	/**
     * Disconnect from the server
     * @param showMessage Whether to show a message
     */
	void disconnect(bool showMessage = true);

	/**
     * Handle ImGui input
     */
	void handleImGuiInput();

	/**
     * Process a single network update cycle
     */
	void updateNetwork();

	/**
     * Draw the UI with ImGui
     */
	void drawUI();

	/**
     * Check if client is in connected state
     * @return True if fully connected
     */
	bool isFullyConnected() const
	{
		return connectionState == ConnectionState::Connected;
	}

	Logger& getLogger()
	{
		return logger;
	}

	/**
	 * Calls the apply theme function from the theme manager
	 */
	void applyTheme();

private:
	//-------------------------------------------------------------------------
	// PRIVATE IMPLEMENTATION
	//-------------------------------------------------------------------------

	// Core components
	std::shared_ptr<NetworkManager> networkManager;
	std::shared_ptr<AuthManager> authManager;
	Logger logger;

	// Player data
	uint32_t myPlayerId = 0;
	std::string myPlayerName;
	std::string myPassword;
	Position myPosition;
	Position lastSentPosition;
	std::unordered_map<uint32_t, PlayerInfo> otherPlayers;
	std::mutex playersMutex;
	float movementThreshold = 0.1f;
	time_t positionUpdateRateMs = 50;
	time_t lastPositionUpdateTime = 0;
	bool useCompressedUpdates = true;

	// Status flags
	bool shouldExit = false;
	bool reconnecting = false;
	ConnectionState connectionState = ConnectionState::LoginScreen;

	bool connectionInProgress = false;
	float connectionProgress = 0.0f;

	// Login UI components
	char loginUsernameBuffer[64] = { 0 };
	char loginPasswordBuffer[64] = { 0 };
	std::string loginErrorMessage;
	bool showPassword = false;
	bool rememberCredentials = true;
	bool autoLogin = false;

	// Register UI components
	bool isRegistering = false;
	char registerUsernameBuffer[64] = { 0 };
	char registerPasswordBuffer[64] = { 0 };
	char registerConfirmPasswordBuffer[64] = { 0 };
	std::string registerErrorMessage;

	std::thread connectionThread;
	std::atomic<bool> connectionThreadRunning{ false };

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

	// Connection monitoring
	time_t lastUpdateTime = 0;
	time_t lastConnectionCheckTime = 0;

	// Utility objects
	HANDLE consoleHandle;

	// Themes 
	ThemeManager themeManager;

	// Thread management
	std::shared_ptr<ThreadManager> threadManager;

	//-------------------------------------------------------------------------
	// PRIVATE METHODS
	//-------------------------------------------------------------------------

	/**
     * Start connection to server
     */
	void startConnection();

	/**
     * Draw login screen
     */
	void drawLoginScreen();

	/**
     * Draw register screen
     */
	void drawRegisterScreen();

	/**
     * Draw the main ui when the user is signed in
     */
	void drawConnectedUI();

	/**
     * Draw the top section of the window
     */
	void drawHeader();

	/**
     * Draw the left section where the players are
     */
	void drawPlayersPanel(float width, float height);

	/**
     * Draw center panel, the chat
     */
	void drawChatPanel(float width, float height);

	/**
     * Draw the right panel where the settings are I might remove this tbh
     */
	void drawControlsPanel(float width, float height);

	/**
     * Draw the network settings the sit in the right siude panel
     */
	void drawNetworkOptionsUI(float width, float height);

	/**
     * Draw bottom status bar or footer
     */
	void drawStatusBar();
	
	/**
     * Logic for looping your history
     */
	void handleChatCommandHistory();

	/**
     * Logic that runs when the user sends anything in chat
     */
	void processChatInput();

	/**
	 * Kick off a registration attempt
	 */
	void initiateRegistration();

	/**
	 * Gets the current stats of the thread manager
	 */ 
	std::string getThreadStats() const;

	/**
     * Initiate connection with current credentials
     */
	void initiateConnection();

	/**
	* Logs the user out and returns to the login screen
	*/
	void signOut();

	/**
     * Send a chat message
     * @param message Message to send
     */
	void sendChatMessage(const std::string& message);

	/**
     * Process a chat command
     * @param command Command to process
     */
	void processChatCommand(const std::string& command);

	/**
     * Handle received packet
     * @param packet Packet to handle
     */
	void handlePacket(const ENetPacket* packet);

	/**
     * Helper function to split strings
     * @param str String to split
     * @param delimiter Delimiter character
     * @return Vector of split strings
     */
	std::vector<std::string> splitString(const std::string& str, char delimiter);

	/**
     * Parse world state update
     * @param stateData State data to parse
     */
	void parseWorldState(const std::string& stateData);

	/**
     * Add a chat message
     * @param sender Message sender
     * @param content Message content
     */
	void addChatMessage(const std::string& sender, const std::string& content);

	/**
     * Clear chat history
     */
	void clearChatMessages();

	/**
     * Handle server disconnection
     */
	void handleServerDisconnection();
};
