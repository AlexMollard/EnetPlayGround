#include "GameClient.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <conio.h>
#include <hello_imgui/hello_imgui.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include "Constants.h"
#include "IconsLucide.h"
#include "Utils.h"

// Constructor
GameClient::GameClient()
      : lastUpdateTime(getCurrentTimeMs())
{
	// Set up console
	consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (consoleHandle == INVALID_HANDLE_VALUE)
	{
		logger.error("Failed to get console handle");
	}

	// Create NetworkManager
	networkManager = std::make_shared<NetworkManager>(logger);

	// Create AuthManager
	authManager = std::make_shared<AuthManager>(logger, networkManager);

	// Try to load stored credentials if none provided
	if (myPlayerName.empty() || myPassword.empty())
	{
		std::string loadedUsername, loadedPassword;
		if (authManager->loadCredentials(loadedUsername, loadedPassword))
		{
			myPlayerName = loadedUsername;
			myPassword = loadedPassword;

			// Copy loaded credentials to the ImGui input buffers
			if (!myPlayerName.empty())
			{
				strncpy_s(loginUsernameBuffer, myPlayerName.c_str(), sizeof(loginUsernameBuffer) - 1);
			}
			if (!myPassword.empty())
			{
				strncpy_s(loginPasswordBuffer, myPassword.c_str(), sizeof(loginPasswordBuffer) - 1);

				// If we loaded credentials, potentially auto-login
				autoLogin = true;
			}
		}
	}

	// Initialize update timing
	lastPositionUpdateTime = lastUpdateTime;
	lastConnectionCheckTime = lastUpdateTime;

	// Start in login screen state
	connectionState = ConnectionState::LoginScreen;
}

// Destructor
GameClient::~GameClient()
{
	disconnect();
	logger.debug("Client shutdown complete");
}

// Initialize the client
bool GameClient::initialize()
{
	logger.debug("Initializing client...");

	// Initialize NetworkManager
	if (!networkManager->initialize())
	{
		logger.error("Failed to initialize network manager");
		return false;
	}

	logger.debug("Client initialized successfully");
	return true;
}

// Disconnect from the server
void GameClient::disconnect(bool showMessage)
{
	// Let NetworkManager handle the actual disconnection
	networkManager->disconnect(showMessage);

	// Update client state
	connectionState = ConnectionState::LoginScreen;
}

void GameClient::handleServerDisconnection()
{
	connectionState = ConnectionState::LoginScreen;
	addChatMessage("System", "Lost connection to server");

	// Try to reconnect if not exiting
	if (!shouldExit && !reconnecting && autoLogin)
	{
		addChatMessage("System", "Attempting to reconnect...");
		networkManager->reconnectToServer();
	}
}

// Send a chat message
void GameClient::sendChatMessage(const std::string& message)
{
	if (!authManager->isAuthenticated() || message.empty())
		return;

	std::string chatMsg = "CHAT:" + message;
	networkManager->sendPacket(chatMsg);

	// Also add to local chat
	addChatMessage("You", message);
}

// Process a chat command
void GameClient::processChatCommand(const std::string& command)
{
	if (command.empty())
	{
		return;
	}

	// Keep a few client-side commands for UI and local functionality
	if (command[0] == '/')
	{
		// Extract command and arguments
		std::string cmd = command.substr(1);
		size_t spacePos = cmd.find(' ');
		std::string args;

		if (spacePos != std::string::npos)
		{
			args = cmd.substr(spacePos + 1);
			cmd = cmd.substr(0, spacePos);
		}

		// Convert command to lowercase
		std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return std::tolower(c); });

		// Process local-only commands
		if (cmd == "quit" || cmd == "exit")
		{
			shouldExit = true;
			return;
		}
		else if (cmd == "clear")
		{
			clearChatMessages();
			return;
		}
		else if (cmd == "debug")
		{
			showDebug = !showDebug;
			logger.setLogLevel(LogLevel::DEBUG);
			addChatMessage("System", "Debug mode " + std::string(showDebug ? "enabled" : "disabled"));
			return;
		}
		else if (cmd == "stats")
		{
			showStats = !showStats;
			addChatMessage("System", "Network stats display " + std::string(showStats ? "enabled" : "disabled"));
			return;
		}
		else if (cmd == "reconnect")
		{
			addChatMessage("System", "Attempting to reconnect...");
			// Use the NetworkManager instead of calling reconnectToServer directly
			networkManager->reconnectToServer();
			return;
		}
		else if (cmd == "signout" || cmd == "logout")
		{
			signOut();
			return;
		}

		// Send all other commands to the server
		if (networkManager->isConnectedToServer() && authManager->isAuthenticated())
		{
			std::string commandMsg = "COMMAND:" + command.substr(1); // Remove the leading '/'
			// Use the NetworkManager to send packets
			networkManager->sendPacket(commandMsg, true);
			logger.debug("Sent command to server: " + commandMsg);
		}
		else
		{
			addChatMessage("System", "Not connected to server. Cannot process command.");
		}
	}
	else
	{
		// Not a command, treat as regular chat
		sendChatMessage(command);
	}
}

// Handle received packet
void GameClient::handlePacket(const ENetPacket* packet)
{
	std::string message(reinterpret_cast<const char*>(packet->data), packet->dataLength);

	// First, let AuthManager check if this is an auth response
	bool wasAuthPacket = authManager->processAuthResponse(
	        packet->data,
	        packet->dataLength,
	        // Auth success callback
	        [this](uint32_t playerId)
	        {
		        this->myPlayerId = playerId;
		        connectionState = ConnectionState::Connected;

		        // Lets get the player position from the server
		        std::string request = "POSITION:";
		        networkManager->sendPacket(request);
	        },
	        // Auth failed callback
	        [this](const std::string& errorMsg)
	        {
		        loginErrorMessage = "Authentication failed: " + errorMsg;
		        connectionState = ConnectionState::LoginScreen;
	        });

	// If already handled by AuthManager, we're done
	if (wasAuthPacket)
	{
		return;
	}

	if (message.substr(0, 13) == "REG_RESPONSE:")
	{
		std::string response = message.substr(13);

		if (response.substr(0, 7) == "SUCCESS")
		{
			// Registration successful
			addChatMessage("System", "Registration successful!");
			connectionState = ConnectionState::LoginScreen;

			// Auto-fill login credentials
			strncpy_s(loginUsernameBuffer, registerUsernameBuffer, sizeof(loginUsernameBuffer));
			strncpy_s(loginPasswordBuffer, registerPasswordBuffer, sizeof(loginPasswordBuffer));

			// Clear registration form
			registerUsernameBuffer[0] = '\0';
			registerPasswordBuffer[0] = '\0';
			registerConfirmPasswordBuffer[0] = '\0';

			// We are ready to pass the login screen so clear the chat messages
			clearChatMessages();
		}
		else
		{
			// Registration failed
			registerErrorMessage = "Registration failed: " + response.substr(6);
			connectionState = ConnectionState::RegisterScreen;
			networkManager->disconnect(false);
		}

		return;
	}

	// Don't log position updates to avoid spam
	if (message.substr(0, 12) != "WORLD_STATE:" && message.substr(0, 5) != "PONG:")
	{
		logger.logNetworkEvent("Received: " + message);
	}

	// Handle different message types
	if (message.substr(0, 12) == "WORLD_STATE:")
	{
		// Handle world state update
		parseWorldState(message.substr(12));
	}
	else if (message.substr(0, 9) == "POSITION:")
	{
		// format: POSITION:x18.254965,y0.000000,z-22.989958
		// Process chat message from server
		//size_t colonPos = message.find(':', 5);
		//if (colonPos != std::string::npos)
		//{
		//	std::string x = message.substr(colonPos + 1, message.find(',', colonPos + 1) - colonPos - 1);
		//	std::string y = message.substr(message.find(',', colonPos + 1) + 1, message.find(',', message.find(',', colonPos + 1) + 1) - message.find(',', colonPos + 1) - 1);
		//	std::string z = message.substr(message.find(',', message.find(',', colonPos + 1) + 1) + 1);
		//
		//	logger.log("Received position update: X=" + x + " Y=" + y + " Z=" + z);

		//	myPosition.x = std::stof(x);
		//	myPosition.y = std::stof(y);
		//	myPosition.z = std::stof(z);
		//}
	}
	else if (message.substr(0, 5) == "CHAT:")
	{
		// Process chat message from server
		size_t colonPos = message.find(':', 5);
		if (colonPos != std::string::npos)
		{
			std::string sender = message.substr(5, colonPos - 5);
			// If the sender is the currently authenticated user, skip the message
			if (sender == myPlayerName)
				return;

			std::string content = message.substr(colonPos + 1);
			addChatMessage(sender, content);
		}
	}
	else if (message.substr(0, 5) == "PONG:")
	{
		// This is now handled by NetworkManager
	}
	else if (message.substr(0, 7) == "SYSTEM:")
	{
		// System message from server
		addChatMessage("Server", message.substr(7));
	}
	else if (message.substr(0, 9) == "TELEPORT:")
	{
		// Handle teleport message from server
		try
		{
			auto parts = splitString(message.substr(9), ',');
			if (parts.size() >= 3)
			{
				float x = std::stof(parts[0]);
				float y = std::stof(parts[1]);
				float z = std::stof(parts[2]);

				// Update position
				myPosition.x = x;
				myPosition.y = y;
				myPosition.z = z;
				lastSentPosition = myPosition; // Prevent immediate position update

				addChatMessage("System", "Teleported to X=" + parts[0] + " Y=" + parts[1] + " Z=" + parts[2]);
			}
		}
		catch (const std::exception& e)
		{
			logger.error("Failed to parse TELEPORT message: " + std::string(e.what()));
		}
	}
	else if (message.substr(0, 15) == "COMMAND_RESULT:")
	{
		// Handle command results from server
		addChatMessage("Server", message.substr(15));
	}
}

// Helper function to split strings (similar to the one in server)
std::vector<std::string> GameClient::splitString(const std::string& str, char delimiter)
{
	std::vector<std::string> tokens;
	std::stringstream ss(str);
	std::string token;

	while (std::getline(ss, token, delimiter))
	{
		tokens.push_back(token);
	}

	return tokens;
}

// Parse world state update
void GameClient::parseWorldState(const std::string& stateData)
{
	// Mark all players for potential removal
	for (auto& pair: otherPlayers)
	{
		pair.second.status = "stale";
	}

	// Parse player data
	size_t startPos = 0;
	size_t endPos = 0;

	while ((endPos = stateData.find(';', startPos)) != std::string::npos)
	{
		std::string playerData = stateData.substr(startPos, endPos - startPos);
		startPos = endPos + 1;

		try
		{
			// Parse player data
			size_t commaPos = 0;
			size_t prevCommaPos = 0;

			// Get player ID
			commaPos = playerData.find(',');
			if (commaPos == std::string::npos)
				continue;
			uint32_t id = std::stoi(playerData.substr(prevCommaPos, commaPos - prevCommaPos));
			prevCommaPos = commaPos + 1;

			// Skip self
			if (id == myPlayerId)
				continue;

			// Get player name
			commaPos = playerData.find(',', prevCommaPos);
			if (commaPos == std::string::npos)
				continue;
			std::string name = playerData.substr(prevCommaPos, commaPos - prevCommaPos);
			prevCommaPos = commaPos + 1;

			// Get X position
			commaPos = playerData.find(',', prevCommaPos);
			if (commaPos == std::string::npos)
				continue;
			float x = std::stof(playerData.substr(prevCommaPos, commaPos - prevCommaPos));
			prevCommaPos = commaPos + 1;

			// Get Y position
			commaPos = playerData.find(',', prevCommaPos);
			if (commaPos == std::string::npos)
				continue;
			float y = std::stof(playerData.substr(prevCommaPos, commaPos - prevCommaPos));
			prevCommaPos = commaPos + 1;

			// Get Z position
			float z = std::stof(playerData.substr(prevCommaPos));

			// Update or add player
			auto it = otherPlayers.find(id);
			if (it != otherPlayers.end())
			{
				// Existing player, update info
				it->second.position.x = x;
				it->second.position.y = y;
				it->second.position.z = z;
				it->second.lastSeen = getCurrentTimeMs();
				it->second.status = "active";
			}
			else
			{
				// New player
				PlayerInfo newPlayer;
				newPlayer.id = id;
				newPlayer.name = name;
				newPlayer.position.x = x;
				newPlayer.position.y = y;
				newPlayer.position.z = z;
				newPlayer.lastSeen = getCurrentTimeMs();
				newPlayer.status = "active";

				otherPlayers[id] = newPlayer;

				// Notify about new player
				addChatMessage("System", "Player joined: " + name);
			}
		}
		catch (const std::exception& e)
		{
			logger.error("Error parsing player data: " + std::string(e.what()) + ", Data: " + playerData);
		}
	}

	// Remove stale players (not in the update)
	auto it = otherPlayers.begin();
	while (it != otherPlayers.end())
	{
		if (it->second.status == "stale")
		{
			it = otherPlayers.erase(it);
		}
		else
		{
			++it;
		}
	}
}

// Add a chat message
void GameClient::addChatMessage(const std::string& sender, const std::string& content)
{
	ChatMessage msg;
	msg.sender = sender;
	msg.content = content;
	msg.timestamp = time(0);

	chatMessages.push_back(msg);

	// Keep only the last N messages
	while (chatMessages.size() > MESSAGE_HISTORY_SIZE)
	{
		chatMessages.pop_front();
	}
}

// Clear chat history
void GameClient::clearChatMessages()
{
	chatMessages.clear();
}

void GameClient::handleImGuiInput()
{
	// Get ImGui IO to check key states
	ImGuiIO& io = ImGui::GetIO();

	// Only process input when we're connected or in the login screen
	if (connectionState == ConnectionState::LoginScreen)
	{
		// In login screen, ESC should exit the application
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			shouldExit = true;
		}

		// Other login-specific input handling would go here
		return;
	}

	// Below only applies to connected state

	// First, handle global keys that work regardless of chat focus
	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
	{
		if (chatFocused)
		{
			// If in chat mode, ESC should exit chat mode first
			chatFocused = false;
			chatInputBuffer[0] = '\0';
		}
		else
		{
			// If not in chat mode, ESC should trigger exit
			shouldExit = true;
		}
	}

	// Toggle function keys should work regardless of chat focus
	if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
	{
		showDebug = !showDebug;
		logger.setLogLevel(LogLevel::DEBUG);
		addChatMessage("System", std::string("Debug mode ") + (showDebug ? "enabled" : "disabled"));
	}

	if (ImGui::IsKeyPressed(ImGuiKey_F3, false))
	{
		showStats = !showStats;
		addChatMessage("System", std::string("Network stats ") + (showStats ? "enabled" : "disabled"));
	}

	// Handle toggle of chat focus with T key
	if (ImGui::IsKeyPressed(ImGuiKey_T, false) && !chatFocused)
	{
		chatFocused = true;
		chatInputBuffer[0] = '\0'; // Clear the input buffer when entering chat
		return;                    // Don't process movement in the same frame as entering chat
	}

	// Only process movement keys when not in chat mode and when fully connected
	if (!chatFocused && !io.WantTextInput && connectionState == ConnectionState::Connected)
	{
		// Get elapsed time since last frame for smooth movement
		uint32_t currentTime = getCurrentTimeMs();
		float deltaTime = (currentTime - lastUpdateTime) / 1000.0f; // Convert to seconds
		lastUpdateTime = currentTime;

		// Use deltaTime for smooth movement regardless of frame rate
		// Cap to reasonable values to prevent extreme movement on lag spikes
		if (deltaTime > 0.1f)
			deltaTime = 0.1f;
		const float BASE_SPEED = 5.0f; // Units per second
		const float MOVE_SPEED = BASE_SPEED * deltaTime;

		// WASD movement on XZ plane
		if (ImGui::IsKeyDown(ImGuiKey_W))
			myPosition.z += MOVE_SPEED;
		if (ImGui::IsKeyDown(ImGuiKey_S))
			myPosition.z -= MOVE_SPEED;
		if (ImGui::IsKeyDown(ImGuiKey_A))
			myPosition.x -= MOVE_SPEED;
		if (ImGui::IsKeyDown(ImGuiKey_D))
			myPosition.x += MOVE_SPEED;

		// QE for vertical movement on Y axis
		if (ImGui::IsKeyDown(ImGuiKey_Q))
			myPosition.y += MOVE_SPEED;
		if (ImGui::IsKeyDown(ImGuiKey_E))
			myPosition.y -= MOVE_SPEED;

		// Check for command keys
		if (ImGui::IsKeyPressed(ImGuiKey_Slash, false))
		{
			// Start chat with "/" for command entry
			chatFocused = true;
			chatInputBuffer[0] = '/';
			chatInputBuffer[1] = '\0';
		}

		// Shortcut keys for common commands
		if (ImGui::IsKeyPressed(ImGuiKey_P, false) && (io.KeyCtrl || io.KeyShift))
		{
			// Ctrl+P or Shift+P for player list
			processChatCommand("/players");
		}

		if (ImGui::IsKeyPressed(ImGuiKey_L, false) && io.KeyCtrl)
		{
			// Ctrl+L to clear chat
			processChatCommand("/clear");
		}

		if (ImGui::IsKeyPressed(ImGuiKey_R, false) && io.KeyCtrl)
		{
			// Ctrl+R to attempt reconnection
			processChatCommand("/reconnect");
		}
	}
}

// Updated to synchronous connection
void GameClient::startConnection()
{
	connectionProgress = 0.0f;
	connectionState = ConnectionState::Connecting;
	loginErrorMessage.clear();

	// Update state
	connectionProgress = 0.1f;

	// Ensure NetworkManager is initialized first
	if (!networkManager->clientSetup())
	{
		if (!initialize())
		{
			loginErrorMessage = "Failed to initialize client";
			connectionState = ConnectionState::LoginScreen;
			return;
		}
	}

	// Connect to server using NetworkManager
	bool connected = networkManager->connectToServer(networkManager->getServerAddress().c_str(), networkManager->getServerPort());
	if (!connected)
	{
		loginErrorMessage = "Failed to connect to server";
		connectionState = ConnectionState::LoginScreen;
		return;
	}

	connectionProgress = 0.5f;

	// Start authentication
	bool authStarted = authManager->authenticate(
	        myPlayerName,
	        myPassword,
	        rememberCredentials,
	        [this](uint32_t playerId)
	        {
		        this->myPlayerId = playerId;
		        connectionState = ConnectionState::Connected;
	        },
	        [this](const std::string& errorMsg)
	        {
		        loginErrorMessage = "Authentication failed: " + errorMsg;
		        connectionState = ConnectionState::LoginScreen;
		        networkManager->disconnect(false);
	        });

	if (!authStarted)
	{
		networkManager->disconnect(false);
		loginErrorMessage = "Failed to start authentication";
		connectionState = ConnectionState::LoginScreen;
	}
	else
	{
		connectionState = ConnectionState::Authenticating;
	}
}

// Process a single network update cycle
void GameClient::updateNetwork()
{
	// Only process network updates if connected or connecting
	if (connectionState == ConnectionState::LoginScreen)
	{
		return;
	}

	// Get current time for delta calculations
	uint32_t currentTime = getCurrentTimeMs();

	// Use NetworkManager to process packets and handle events
	networkManager->update(
	        // Position update callback
	        [this]()
	        {
		        uint32_t currentTime = getCurrentTimeMs();
		        if (currentTime - lastPositionUpdateTime >= positionUpdateRateMs && authManager->isAuthenticated())
		        {
			        networkManager->sendPositionUpdate(myPosition.x, myPosition.y, myPosition.z, lastSentPosition.x, lastSentPosition.y, lastSentPosition.z, useCompressedUpdates, movementThreshold);
			        lastSentPosition = myPosition;
			        lastPositionUpdateTime = currentTime;
		        }
	        },
	        // Packet handler callback
	        [this](const ENetPacket* packet) { handlePacket(packet); },
	        // Disconnect callback
	        [this]() { handleServerDisconnection(); });

	// Connection health checks at regular intervals
	if (currentTime - lastConnectionCheckTime >= 1000)
	{
		networkManager->sendPing();
		networkManager->checkConnectionHealth([this]() { handleServerDisconnection(); });
		lastConnectionCheckTime = currentTime;
	}
}

void GameClient::drawStatusBar()
{
	// gradient status bar
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 statusMin = ImGui::GetCursorScreenPos();
	ImVec2 statusMax = ImVec2(statusMin.x + ImGui::GetContentRegionAvail().x - 10, statusMin.y + 30);

	// Status bar content
	ImGui::BeginChild("StatusBarContent", ImVec2(ImGui::GetContentRegionAvail().x, 30), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// Network stats with icons
	ImGui::SetCursorPos(ImVec2(20, 5));
	ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.80f, 0.90f), ICON_LC_WIFI " %ums | " ICON_LC_PACKAGE " %u/%u | " ICON_LC_HARD_DRIVE " %u/%u bytes", networkManager->getPing(), networkManager->getPacketsSent(), networkManager->getPacketsReceived(), networkManager->getBytesSent(), networkManager->getBytesReceived());

	// Players online with icon
	float playerOnlineTextWidth = ImGui::CalcTextSize(ICON_LC_USERS " Players Online: 999").x;
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - playerOnlineTextWidth - 20);
	ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.00f), ICON_LC_USERS " Players Online: %zu", otherPlayers.size() + 1); // +1 for self

	ImGui::EndChild();
}

void GameClient::handleChatCommandHistory()
{
	// Add command history support
	if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && chatFocused && !commandHistory.empty())
	{
		if (commandHistoryIndex == -1)
		{
			commandHistoryIndex = commandHistory.size() - 1;
		}
		else if (commandHistoryIndex > 0)
		{
			commandHistoryIndex--;
		}

		if (commandHistoryIndex >= 0 && commandHistoryIndex < commandHistory.size())
		{
			strncpy_s(chatInputBuffer, commandHistory[commandHistoryIndex].c_str(), sizeof(chatInputBuffer) - 1);
		}
	}

	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && chatFocused && !commandHistory.empty() && commandHistoryIndex != -1)
	{
		if (commandHistoryIndex < static_cast<int>(commandHistory.size()) - 1)
		{
			commandHistoryIndex++;
			strncpy_s(chatInputBuffer, commandHistory[commandHistoryIndex].c_str(), sizeof(chatInputBuffer) - 1);
		}
		else
		{
			commandHistoryIndex = -1;
			chatInputBuffer[0] = '\0';
		}
	}

	// Escape key exits chat focus
	if (ImGui::IsKeyPressed(ImGuiKey_Escape) && chatFocused)
	{
		chatFocused = false;
		chatInputBuffer[0] = '\0';
	}
}

void GameClient::processChatInput()
{
	if (chatInputBuffer[0] != '\0')
	{
		std::string chatStr(chatInputBuffer);
		processChatCommand(chatStr);

		// Add to command history
		if (!commandHistory.empty() && commandHistory.back() != chatStr)
		{
			commandHistory.push_back(chatStr);
		}
		else if (commandHistory.empty())
		{
			commandHistory.push_back(chatStr);
		}

		// Limit history size
		while (commandHistory.size() > MAX_HISTORY_COMMANDS)
		{
			commandHistory.pop_front();
		}

		commandHistoryIndex = -1;
		chatInputBuffer[0] = '\0'; // Clear input
	}

	// Keep focus if still in chat input field
	if (ImGui::IsItemDeactivated())
	{
		chatFocused = false;
	}
}

void GameClient::initiateRegistration()
{
	// Get values from input fields
	std::string username = registerUsernameBuffer;
	std::string password = registerPasswordBuffer;
	std::string confirmPassword = registerConfirmPasswordBuffer;

	// Clear previous error
	registerErrorMessage.clear();

	// Validate input
	if (username.empty())
	{
		registerErrorMessage = "Username cannot be empty";
		return;
	}

	if (password.empty())
	{
		registerErrorMessage = "Password cannot be empty";
		return;
	}

	if (password != confirmPassword)
	{
		registerErrorMessage = "Passwords do not match";
		return;
	}

	// Validate username length
	if (username.length() < 3)
	{
		registerErrorMessage = "Username must be at least 3 characters";
		return;
	}

	// Validate password length
	if (password.length() < 6)
	{
		registerErrorMessage = "Password must be at least 6 characters";
		return;
	}

	// Ensure NetworkManager is initialized
	if (!networkManager->clientSetup())
	{
		if (!initialize())
		{
			registerErrorMessage = "Failed to initialize client";
			return;
		}
	}

	// Connect to the server
	if (!networkManager->connectToServer(networkManager->getServerAddress().c_str(), networkManager->getServerPort()))
	{
		registerErrorMessage = "Failed to connect to server";
		return;
	}

	// Send registration request
	std::string registerPacket = "COMMAND:register " + username + " " + password;
	networkManager->sendPacket(registerPacket);

	// Set state to wait for registration response
	connectionState = ConnectionState::Authenticating;

	myPlayerName = username;

	// Copy credentials to login screen for convenience
	strncpy_s(loginUsernameBuffer, username.c_str(), sizeof(loginUsernameBuffer) - 1);
	strncpy_s(loginPasswordBuffer, password.c_str(), sizeof(loginPasswordBuffer) - 1);
}

void GameClient::initiateConnection()
{
	// Update credentials from input fields
	myPlayerName = loginUsernameBuffer;
	myPassword = loginPasswordBuffer;

	// Validate credentials
	if (myPlayerName.empty() || myPassword.empty())
	{
		loginErrorMessage = "Please enter both username and password";
		return;
	}

	// Clear previous error message
	loginErrorMessage.clear();

	// Start connection
	startConnection();

	// We are ready to pass the login screen so clear the chat messages
	clearChatMessages();
}

void GameClient::signOut()
{
	// Disconnect from the server
	disconnect(true);

	// Clear any sensitive session data
	myPlayerId = 0;

	// If we're not remembering credentials, clear them
	if (!rememberCredentials)
	{
		loginPasswordBuffer[0] = '\0';
		myPassword.clear();
	}

	// Return to login screen
	connectionState = ConnectionState::LoginScreen;

	// Add a system message
	addChatMessage("System", "You have been signed out");

	logger.debug("User signed out");

	clearChatMessages();
}

void GameClient::drawLoginScreen()
{
	// Center the login window on screen
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 windowSize = ImVec2(400, 350); // Using original size to ensure no scrolling
	ImVec2 windowPos = ImVec2((io.DisplaySize.x - windowSize.x) * 0.5f, (io.DisplaySize.y - windowSize.y) * 0.5f);

	// Set window position and size
	ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

	// Start the window - using game's existing style
	ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

	// Calculate content width for centering
	const float contentWidth = ImGui::GetContentRegionAvail().x;

	// Game title with proper centering
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Default font

	// Center the title text properly
	ImVec2 titleSize = ImGui::CalcTextSize("MMO CLIENT");
	ImGui::SetCursorPosX((contentWidth - titleSize.x) * 0.5f);
	ImGui::Text("MMO CLIENT");
	ImGui::PopFont();

	// Center version text properly
	char versionText[64];
	snprintf(versionText, sizeof(versionText), "Version %s", VERSION);
	ImVec2 versionSize = ImGui::CalcTextSize(versionText);
	ImGui::SetCursorPosX((contentWidth - versionSize.x) * 0.5f);
	ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", versionText);

	// Separator with proper spacing
	ImGui::Separator();
	ImGui::Spacing();

	// If connection is in progress, show connecting indicator
	if (connectionState == ConnectionState::Connecting || connectionState == ConnectionState::Authenticating)
	{
		// Center text with icon
		const char* connectingText = ICON_LC_WIFI " Connecting to server...";
		ImVec2 textSize = ImGui::CalcTextSize(connectingText);
		ImGui::SetCursorPosX((contentWidth - textSize.x) * 0.5f);
		ImGui::Text("%s", connectingText);

		// Progress bar
		const float progressBarWidth = contentWidth * 0.9f;
		ImGui::SetCursorPosX((contentWidth - progressBarWidth) * 0.5f);
		ImGui::ProgressBar(connectionProgress, ImVec2(progressBarWidth, 20), "");
		ImGui::Spacing();

		// Cancel button with icon
		const float buttonWidth = 120.0f;
		ImGui::SetCursorPosX((contentWidth - buttonWidth) * 0.5f);
		if (ImGui::Button(ICON_LC_X " Cancel", ImVec2(buttonWidth, 30)))
		{
			disconnect(false);
			connectionProgress = 0.0f;
			loginErrorMessage = "Connection canceled by user";
		}
	}
	else
	{
		float labelWidth = 0.0f; // Width for labels

		// Server selection with consistent alignment and icon
		ImGui::AlignTextToFramePadding();
		ImGui::Text(ICON_LC_SERVER " Server:");
		ImGui::SameLine();
		labelWidth = ImGui::GetCursorPosX();
		ImGui::SetNextItemWidth(contentWidth - labelWidth);
		char serverBuf[128];
		strncpy_s(serverBuf, networkManager->getServerAddress().c_str(), sizeof(serverBuf) - 1);
		if (ImGui::InputText("##Server", serverBuf, sizeof(serverBuf)))
		{
			networkManager->setServerAddress(serverBuf);
		}
		ImGui::Spacing();

		// Username field with consistent alignment and icon
		ImGui::AlignTextToFramePadding();
		ImGui::Text(ICON_LC_USER " Username:");
		ImGui::SameLine();
		labelWidth = ImGui::GetCursorPosX();
		ImGui::SetNextItemWidth(contentWidth - labelWidth);
		ImGui::InputText("##Username", loginUsernameBuffer, sizeof(loginUsernameBuffer));

		// Password field with better alignment and icon
		ImGui::AlignTextToFramePadding();
		ImGui::Text(ICON_LC_COMMAND " Password:");
		ImGui::SameLine();
		labelWidth = ImGui::GetCursorPosX();
		ImGuiInputTextFlags passwordFlags = showPassword ? 0 : ImGuiInputTextFlags_Password;
		ImGui::SetNextItemWidth(contentWidth - labelWidth - 80);
		ImGui::InputText("##Password", loginPasswordBuffer, sizeof(loginPasswordBuffer), passwordFlags);
		ImGui::SameLine();
		if (ImGui::Button(showPassword ? ICON_LC_EYE_OFF " Hide" : ICON_LC_EYE " Show", ImVec2(70, 0)))
		{
			showPassword = !showPassword;
		}
		ImGui::Spacing();

		// Remember credentials with proper alignment
		ImGui::Checkbox("Remember credentials", &rememberCredentials);
		ImGui::Spacing();

		// Error message with better styling
		if (!loginErrorMessage.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));

			// Measure text to decide on centering or wrapping
			ImVec2 textSize = ImGui::CalcTextSize((ICON_LC_CIRCLE_ALERT " " + loginErrorMessage).c_str());
			if (textSize.x > contentWidth - 20.0f)
			{
				// For long messages, use wrapping
				ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + contentWidth);
				ImGui::TextWrapped("%s %s", ICON_LC_CIRCLE_ALERT, loginErrorMessage.c_str());
				ImGui::PopTextWrapPos();
			}
			else
			{
				// For short messages, center
				ImGui::SetCursorPosX((contentWidth - textSize.x) * 0.5f);
				ImGui::Text("%s %s", ICON_LC_CIRCLE_ALERT, loginErrorMessage.c_str());
			}

			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		// Connect button with icon
		const float buttonWidth = 120.0f;
		ImGui::SetCursorPosX((contentWidth - buttonWidth) * 0.5f);
		if (ImGui::Button(ICON_LC_LOG_IN " Connect", ImVec2(buttonWidth, 30)))
		{
			// Initiate connection
			initiateConnection();
		}
	}

	if (connectionState == ConnectionState::LoginScreen && !connectionInProgress)
	{
		ImGui::Spacing();
		ImGui::Spacing();

		// "Don't have an account?" text
		const char* registerText = "Don't have an account?";
		ImVec2 registerTextSize = ImGui::CalcTextSize(registerText);
		ImGui::SetCursorPosX((contentWidth - registerTextSize.x) * 0.5f);
		ImGui::Text("%s", registerText);

		// Register button
		const float registerButtonWidth = 120.0f;
		ImGui::SetCursorPosX((contentWidth - registerButtonWidth) * 0.5f);
		if (ImGui::Button(ICON_LC_USER_PLUS " Register", ImVec2(registerButtonWidth, 30)))
		{
			connectionState = ConnectionState::RegisterScreen;
			registerErrorMessage.clear();
		}
	}

	ImGui::End();
}

void GameClient::drawRegisterScreen()
{
	// Center the register window on screen
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 windowSize = ImVec2(400, 400); // Slightly taller than login window
	ImVec2 windowPos = ImVec2((io.DisplaySize.x - windowSize.x) * 0.5f, (io.DisplaySize.y - windowSize.y) * 0.5f);

	// Set window position and size
	ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

	// Start the window - using game's existing style
	ImGui::Begin("Register", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

	// Calculate content width for centering
	const float contentWidth = ImGui::GetContentRegionAvail().x;

	// Game title with proper centering
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Default font

	// Center the title text properly
	ImVec2 titleSize = ImGui::CalcTextSize("CREATE ACCOUNT");
	ImGui::SetCursorPosX((contentWidth - titleSize.x) * 0.5f);
	ImGui::Text("CREATE ACCOUNT");
	ImGui::PopFont();

	// Center version text properly
	char versionText[64];
	snprintf(versionText, sizeof(versionText), "Version %s", VERSION);
	ImVec2 versionSize = ImGui::CalcTextSize(versionText);
	ImGui::SetCursorPosX((contentWidth - versionSize.x) * 0.5f);
	ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", versionText);

	// Separator with proper spacing
	ImGui::Separator();
	ImGui::Spacing();

	float labelWidth = 0.0f; // Width for labels

	// Server information
	ImGui::AlignTextToFramePadding();
	ImGui::Text(ICON_LC_SERVER " Server:");
	ImGui::SameLine();
	labelWidth = ImGui::GetCursorPosX();
	ImGui::SetNextItemWidth(contentWidth - labelWidth);
	char serverBuf[128];
	strncpy_s(serverBuf, networkManager->getServerAddress().c_str(), sizeof(serverBuf) - 1);
	if (ImGui::InputText("##RegServer", serverBuf, sizeof(serverBuf)))
	{
		networkManager->setServerAddress(serverBuf);
	}
	ImGui::Spacing();

	// Username field
	ImGui::AlignTextToFramePadding();
	ImGui::Text(ICON_LC_USER " Username:");
	ImGui::SameLine();
	labelWidth = ImGui::GetCursorPosX();
	ImGui::SetNextItemWidth(contentWidth - labelWidth);
	ImGui::InputText("##RegUsername", registerUsernameBuffer, sizeof(registerUsernameBuffer));

	// Password field
	ImGui::AlignTextToFramePadding();
	ImGui::Text(ICON_LC_COMMAND " Password:");
	ImGui::SameLine();
	labelWidth = ImGui::GetCursorPosX();
	ImGui::SetNextItemWidth(contentWidth - labelWidth - 80);
	ImGuiInputTextFlags passwordFlags = showPassword ? 0 : ImGuiInputTextFlags_Password;
	ImGui::InputText("##RegPassword", registerPasswordBuffer, sizeof(registerPasswordBuffer), passwordFlags);
	ImGui::SameLine();
	if (ImGui::Button(showPassword ? ICON_LC_EYE_OFF " Hide" : ICON_LC_EYE " Show", ImVec2(70, 0)))
	{
		showPassword = !showPassword;
	}

	// Confirm Password field
	ImGui::AlignTextToFramePadding();
	ImGui::Text(ICON_LC_CHECK " Confirm:");
	ImGui::SameLine();
	labelWidth = ImGui::GetCursorPosX();
	ImGui::SetNextItemWidth(contentWidth - labelWidth);
	ImGui::InputText("##RegConfirmPassword", registerConfirmPasswordBuffer, sizeof(registerConfirmPasswordBuffer), passwordFlags);

	ImGui::Spacing();

	// Remember credentials option
	ImGui::Checkbox("Remember credentials", &rememberCredentials);
	ImGui::Spacing();

	// Error message with better styling
	if (!registerErrorMessage.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));

		// Measure text to decide on centering or wrapping
		ImVec2 textSize = ImGui::CalcTextSize((ICON_LC_CIRCLE_ALERT " " + registerErrorMessage).c_str());
		if (textSize.x > contentWidth - 20.0f)
		{
			// For long messages, use wrapping
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + contentWidth);
			ImGui::TextWrapped("%s %s", ICON_LC_CIRCLE_ALERT, registerErrorMessage.c_str());
			ImGui::PopTextWrapPos();
		}
		else
		{
			// For short messages, center
			ImGui::SetCursorPosX((contentWidth - textSize.x) * 0.5f);
			ImGui::Text("%s %s", ICON_LC_CIRCLE_ALERT, registerErrorMessage.c_str());
		}

		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	// Register button
	const float buttonWidth = 120.0f;
	ImGui::SetCursorPosX((contentWidth - buttonWidth) * 0.5f);
	if (ImGui::Button(ICON_LC_USER_PLUS " Register", ImVec2(buttonWidth, 30)))
	{
		initiateRegistration();
	}

	ImGui::Spacing();

	// Back to login link
	const char* backText = ICON_LC_ARROW_LEFT " Back to Login";
	ImVec2 backSize = ImGui::CalcTextSize(backText);
	ImGui::SetCursorPosX((contentWidth - backSize.x) * 0.5f);

	if (ImGui::Button(backText))
	{
		connectionState = ConnectionState::LoginScreen;
		registerErrorMessage.clear();
	}

	ImGui::End();
}

void GameClient::drawConnectedUI()
{
	// Header with gradient
	drawHeader();

	// Content area with panels
	ImGui::Spacing();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));

	// Auto-layout with proper responsiveness
	float availWidth = ImGui::GetContentRegionAvail().x - 20; // Reserve some padding
	float playersPanelWidth = availWidth * 0.25f;  // 25% for players
	float chatPanelWidth = availWidth * 0.5f;      // 50% for chat
	float controlsPanelWidth = availWidth * 0.25f; // 25% for controls

	// Adjust width based on window size
	if (availWidth < 1200)
	{
		playersPanelWidth = availWidth * 0.3f;
		chatPanelWidth = availWidth * 0.4f;
		controlsPanelWidth = availWidth * 0.3f;
	}

	float contentHeight = ImGui::GetContentRegionAvail().y - 40; // Reserve space for footer

	// Left panel - Player list
	ImGui::BeginChild("PlayersContainer", ImVec2(playersPanelWidth, contentHeight), false);
	drawPlayersPanel(playersPanelWidth, contentHeight);
	ImGui::EndChild();

	ImGui::SameLine();

	// Middle panel - Chat
	ImGui::BeginChild("ChatContainer", ImVec2(chatPanelWidth, contentHeight), false);
	drawChatPanel(chatPanelWidth, contentHeight);
	ImGui::EndChild();

	ImGui::SameLine();

	// Right panel - Controls
	ImGui::BeginChild("ControlsContainer", ImVec2(controlsPanelWidth, contentHeight), false);
	drawControlsPanel(controlsPanelWidth, contentHeight);
	ImGui::EndChild();

	ImGui::PopStyleVar(); // ItemSpacing

	// footer/status bar
	drawStatusBar();
}

void GameClient::drawHeader()
{
	// gradient header
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 headerMin = ImGui::GetCursorScreenPos();
	ImVec2 headerMax = ImVec2(headerMin.x + ImGui::GetContentRegionAvail().x - 10, headerMin.y + 80);

	// Gradient background
	drawList->AddRectFilledMultiColor(headerMin,
	        headerMax,
	        ImGui::GetColorU32(ImVec4(0.12f, 0.12f, 0.18f, 1.0f)), // Left color
	        ImGui::GetColorU32(ImVec4(0.18f, 0.18f, 0.25f, 1.0f)), // Right color
	        ImGui::GetColorU32(ImVec4(0.16f, 0.16f, 0.22f, 1.0f)), // Bottom right
	        ImGui::GetColorU32(ImVec4(0.10f, 0.10f, 0.15f, 1.0f))  // Bottom left
	);

	// Content container
	ImGui::BeginChild("HeaderContent", ImVec2(ImGui::GetContentRegionAvail().x, 80), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// Left side - Title and version
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Larger font
	ImGui::SetCursorPos(ImVec2(20, 15));
	ImGui::Text("MMO CLIENT");
	// Same line but centered in the header we want to show the playersname and id
	ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(myPlayerName.c_str()).x) * 0.5f);
	ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", myPlayerName.c_str(), myPlayerId);
	ImGui::PopFont();

	ImGui::SetCursorPos(ImVec2(20, 50));
	ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "v%s", VERSION);

	// Right side - Status with badge
	float statusWidth = 180;
	ImVec2 statusPos = ImVec2(ImGui::GetContentRegionAvail().x - statusWidth / 2 - 30, 10);

	// Status indicator
	ImVec4 statusColor;
	const char* statusText;

	if (connectionInProgress)
	{
		statusColor = ImVec4(0.9f, 0.7f, 0.0f, 1.0f); // Amber
		statusText = "CONNECTING...";
	}
	else
	{
		switch (connectionState)
		{
			case ConnectionState::Connecting:
				statusColor = ImVec4(0.9f, 0.7f, 0.0f, 1.0f); // Amber
				statusText = "CONNECTING...";
				break;
			case ConnectionState::Authenticating:
				statusColor = ImVec4(0.9f, 0.7f, 0.0f, 1.0f); // Amber
				statusText = "AUTHENTICATING...";
				break;
			case ConnectionState::Connected:
				statusColor = ImVec4(0.2f, 0.8f, 0.4f, 1.0f); // Green
				statusText = "CONNECTED";
				break;
			default:
				statusColor = ImVec4(0.9f, 0.3f, 0.3f, 1.0f); // Red
				statusText = "DISCONNECTED";
				break;
		}
	}

	// badge with pill shape
	ImGui::SetCursorPos(statusPos);
	ImVec2 badgeMin = ImGui::GetCursorScreenPos();
	ImVec2 textSize = ImGui::CalcTextSize(statusText);
	ImVec2 badgeMax = ImVec2(badgeMin.x + textSize.x + 20, badgeMin.y + textSize.y + 10);
	float badgeRounding = (badgeMax.y - badgeMin.y) / 2.0f;

	drawList->AddRectFilled(badgeMin, badgeMax, ImGui::GetColorU32(statusColor), badgeRounding);

	// Center text in badge
	ImGui::SetCursorPos(ImVec2(statusPos.x + 10, statusPos.y + 5));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
	ImGui::Text("%s", statusText);
	ImGui::PopStyleColor();

	// buttons with icons
	float buttonWidth = 120;
	float buttonHeight = 30;
	ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionAvail().x - buttonWidth - 15, 45));

	// Sign Out button with hover effect
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 15.0f);

	if (ImGui::Button(ICON_LC_LOG_OUT " Sign Out", ImVec2(buttonWidth, buttonHeight)))
	{
		signOut();
	}

	ImGui::PopStyleVar();

	ImGui::EndChild();
}

void GameClient::drawPlayersPanel(float width, float height)
{
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));

	ImGui::BeginChild("PlayersPanel", ImVec2(width, height), true);

	// Panel header with icon
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);                          // Default font
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.59f, 0.98f, 1.00f)); // Accent color
	ImGui::Text(ICON_LC_USERS " PLAYERS ONLINE");
	ImGui::PopStyleColor();
	ImGui::PopFont();

	// separator with gradient
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 sepMin = ImGui::GetCursorScreenPos();
	ImVec2 sepMax = ImVec2(sepMin.x + width - 40, sepMin.y + 1);
	drawList->AddRectFilledMultiColor(sepMin,
	        sepMax,
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.7f)), // Left
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.1f)), // Right
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.1f)), // Right
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.7f))  // Left
	);
	ImGui::Dummy(ImVec2(0, 10)); // Space after separator

	// Network stats display
	if (showStats)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.78f, 0.74f, 1.00f)); // Muted color
		ImGui::BeginChild("NetworkStats", ImVec2(width - 40, 45), true);
		ImGui::TextWrapped("Ping: %ums | Packets: %u/%u | Data: %u/%u bytes", networkManager->getPing(), networkManager->getPacketsSent(), networkManager->getPacketsReceived(), networkManager->getBytesSent(), networkManager->getBytesReceived());
		ImGui::EndChild();
		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	// Display other players with card-like styling
	const float listHeight = height - 525; // Reserve space for the map
	ImGui::BeginChild("PlayersList", ImVec2(width - 40, listHeight), false);

	std::lock_guard<std::mutex> lock(playersMutex);
	if (otherPlayers.empty())
	{
		ImGui::TextColored(ImVec4(0.80f, 0.78f, 0.74f, 1.00f), ICON_LC_USERS " No other players online");
	}
	else
	{
		for (const auto& pair: otherPlayers)
		{
			const PlayerInfo& player = pair.second;

			// Player card with hover effect
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 10));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

			// Generate unique ID for this player card
			ImGui::PushID(player.id);

			bool isHovered = false;

			// Start of card
			ImGui::BeginChild(("Player_" + std::to_string(player.id)).c_str(), ImVec2(width - 60, 75), true);
			isHovered = ImGui::IsItemHovered();

			// Player icon with colored circle
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 iconPos = ImGui::GetCursorScreenPos();
			iconPos.x += 10;
			iconPos.y += 10;

			// Player name and ID
			ImGui::SetCursorPos(ImVec2(30, 8));
			ImGui::TextUnformatted(player.name.c_str());
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.70f, 0.68f, 0.64f, 1.00f), "(ID: %u)", player.id);

			// Position with icon
			ImGui::SetCursorPos(ImVec2(30, 35));
			ImGui::TextColored(ImVec4(0.80f, 0.78f, 0.74f, 1.00f), ICON_LC_MAP_PIN " %.1f, %.1f, %.1f", player.position.x, player.position.y, player.position.z);

			ImGui::EndChild(); // End of card

			// Card highlight effect on hover
			if (isHovered)
			{
				ImVec2 min = ImGui::GetItemRectMin();
				ImVec2 max = ImGui::GetItemRectMax();
				drawList->AddRect(ImVec2(min.x - 1, min.y - 1),
				        ImVec2(max.x + 1, max.y + 1),
				        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 1.00f)), // Accent color
				        6.0f,
				        0,
				        2.0f);
			}

			ImGui::PopID();
			ImGui::PopStyleVar(2); // FramePadding, FrameRounding
			ImGui::Spacing();
		}
	}
	ImGui::EndChild(); // End PlayersList

	// map section
	ImGui::Spacing();
	ImGui::Dummy(ImVec2(0, 5));

	// Map header with gradient separator
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.59f, 0.98f, 1.00f)); // Accent color
	ImGui::Text(ICON_LC_MAP " PLAYER POSITIONS");
	ImGui::PopStyleColor();

	// Separator with gradient
	ImDrawList* mapSepDrawList = ImGui::GetWindowDrawList();
	ImVec2 mapSepMin = ImGui::GetCursorScreenPos();
	ImVec2 mapSepMax = ImVec2(mapSepMin.x + width - 40, mapSepMin.y + 1);
	mapSepDrawList->AddRectFilledMultiColor(mapSepMin,
	        mapSepMax,
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.7f)), // Left
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.1f)), // Right
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.1f)), // Right
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.7f))  // Left
	);
	ImGui::Dummy(ImVec2(0, 10)); // Space after separator

	// Calculate map size
	const float mapSize = width - 60;
	const float mapPadding = 10.0f;
	const ImVec2 mapPos = ImGui::GetCursorScreenPos();

	// Create a canvas for the map
	ImGui::InvisibleButton("PositionMap", ImVec2(mapSize, mapSize));

	// Draw map with soft shadow
	// Outer shadow
	drawList->AddRectFilled(ImVec2(mapPos.x + 4, mapPos.y + 4),
	        ImVec2(mapPos.x + mapSize + 4, mapPos.y + mapSize + 4),
	        ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.25f)), // Shadow color
	        10.0f                                                // Rounded corners
	);

	// Main map background
	drawList->AddRectFilled(ImVec2(mapPos.x, mapPos.y),
	        ImVec2(mapPos.x + mapSize, mapPos.y + mapSize),
	        ImGui::GetColorU32(ImVec4(0.15f, 0.16f, 0.21f, 1.00f)), // Dark background
	        10.0f                                                   // Rounded corners
	);

	// Map border
	drawList->AddRect(ImVec2(mapPos.x, mapPos.y),
	        ImVec2(mapPos.x + mapSize, mapPos.y + mapSize),
	        ImGui::GetColorU32(ImVec4(0.30f, 0.30f, 0.40f, 0.60f)), // Border color
	        10.0f,
	        0,
	        1.5f // Line thickness
	);

	// Draw grid lines
	const int gridLines = 4; // 4x4 grid
	const float gridStep = mapSize / gridLines;
	const float gridAlpha = 0.2f;

	for (int i = 1; i < gridLines; i++)
	{
		// Horizontal lines
		drawList->AddLine(ImVec2(mapPos.x, mapPos.y + i * gridStep), ImVec2(mapPos.x + mapSize, mapPos.y + i * gridStep), ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.7f, gridAlpha)));

		// Vertical lines
		drawList->AddLine(ImVec2(mapPos.x + i * gridStep, mapPos.y), ImVec2(mapPos.x + i * gridStep, mapPos.y + mapSize), ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.7f, gridAlpha)));
	}

	// World boundaries for scaling
	float worldMinX = -100.0f;
	float worldMaxX = 100.0f;
	float worldMinZ = -100.0f;
	float worldMaxZ = 100.0f;

	// Helper function to map world coordinates to map coordinates
	auto worldToMap = [&](float x, float z) -> ImVec2
	{
		float mapX = mapPos.x + mapPadding + (x - worldMinX) / (worldMaxX - worldMinX) * (mapSize - 2 * mapPadding);
		float mapY = mapPos.y + mapPadding + (z - worldMinZ) / (worldMaxZ - worldMinZ) * (mapSize - 2 * mapPadding);
		return ImVec2(mapX, mapY);
	};

	// Draw players on the map
	{

		// Draw local player (yellow dot with glow effect)
		ImVec2 playerPos = worldToMap(myPosition.x, -myPosition.z);

		// Glow effect
		const float glowSize = 12.0f;
		const int numSamples = 8;
		for (int i = 0; i < numSamples; i++)
		{
			float alpha = 0.5f * (1.0f - (float) i / numSamples);
			drawList->AddCircleFilled(playerPos, 5.0f + i * 0.8f, ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.2f, alpha)));
		}

		// Main dot
		drawList->AddCircleFilled(playerPos, 6.0f, ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.2f, 1.0f)));
		drawList->AddCircle(playerPos, 6.0f, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.5f)), 0, 1.5f);

		// Draw other players with subtle glow
		for (const auto& pair: otherPlayers)
		{
			const PlayerInfo& player = pair.second;
			ImVec2 otherPlayerPos = worldToMap(player.position.x, -player.position.z);

			// Subtle glow
			drawList->AddCircleFilled(otherPlayerPos, 7.0f, ImGui::GetColorU32(ImVec4(0.4f, 0.8f, 0.4f, 0.3f)));

			// Player dot
			drawList->AddCircleFilled(otherPlayerPos, 5.0f, ImGui::GetColorU32(ImVec4(0.4f, 0.8f, 0.4f, 1.0f)));
			drawList->AddCircle(otherPlayerPos, 5.0f, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.3f)), 0, 1.0f);

			// Tooltip on hover
			if (ImGui::IsMouseHoveringRect(ImVec2(otherPlayerPos.x - 8, otherPlayerPos.y - 8), ImVec2(otherPlayerPos.x + 8, otherPlayerPos.y + 8)))
			{
				ImGui::BeginTooltip();
				ImGui::Text("%s (ID: %u)", player.name.c_str(), player.id);
				ImGui::Text("Position: %.1f, %.1f, %.1f", player.position.x, player.position.y, player.position.z);
				ImGui::EndTooltip();
			}
		}
	}

	// compass with translucent background
	float compassRadius = 18.0f;
	float compassOffset = 25.0f;
	ImVec2 compassPos = ImVec2(mapPos.x + mapSize - compassOffset, mapPos.y + compassOffset);

	// Compass background with gradient
	drawList->AddCircleFilled(compassPos, compassRadius + 3.0f, ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.15f, 0.7f)));

	// Direction indicators
	// North (triangle)
	{
		ImVec2 n1(compassPos.x, compassPos.y - compassRadius + 4);
		ImVec2 n2(compassPos.x - 4, compassPos.y - compassRadius + 12);
		ImVec2 n3(compassPos.x + 4, compassPos.y - compassRadius + 12);
		drawList->AddTriangleFilled(n1, n2, n3, ImGui::GetColorU32(ImVec4(0.9f, 0.3f, 0.3f, 1.0f)));
	}

	// East (triangle)
	{
		ImVec2 e1(compassPos.x + compassRadius - 4, compassPos.y);
		ImVec2 e2(compassPos.x + compassRadius - 12, compassPos.y - 4);
		ImVec2 e3(compassPos.x + compassRadius - 12, compassPos.y + 4);
		drawList->AddTriangleFilled(e1, e2, e3, ImGui::GetColorU32(ImVec4(0.3f, 0.9f, 0.3f, 1.0f)));
	}

	// South (triangle)
	{
		ImVec2 s1(compassPos.x, compassPos.y + compassRadius - 4);
		ImVec2 s2(compassPos.x - 4, compassPos.y + compassRadius - 12);
		ImVec2 s3(compassPos.x + 4, compassPos.y + compassRadius - 12);
		drawList->AddTriangleFilled(s1, s2, s3, ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.9f, 1.0f)));
	}

	// West (triangle)
	{
		ImVec2 w1(compassPos.x - compassRadius + 4, compassPos.y);
		ImVec2 w2(compassPos.x - compassRadius + 12, compassPos.y - 4);
		ImVec2 w3(compassPos.x - compassRadius + 12, compassPos.y + 4);
		drawList->AddTriangleFilled(w1, w2, w3, ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.3f, 1.0f)));
	}

	// Direction text
	drawList->AddText(ImVec2(compassPos.x - 4, compassPos.y - compassRadius - 16), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.9f)), "N");
	drawList->AddText(ImVec2(compassPos.x + compassRadius + 4, compassPos.y - 8), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.9f)), "E");
	drawList->AddText(ImVec2(compassPos.x - 4, compassPos.y + compassRadius + 4), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.9f)), "S");
	drawList->AddText(ImVec2(compassPos.x - compassRadius - 10, compassPos.y - 8), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.9f)), "W");

	// Draw legend
	ImVec2 legendPos = ImVec2(mapPos.x + 15, mapPos.y + mapSize - 45);

	// Legend background
	drawList->AddRectFilled(ImVec2(legendPos.x - 5, legendPos.y - 5),
	        ImVec2(legendPos.x + 145, legendPos.y + 35),
	        ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.15f, 0.7f)),
	        5.0f // Rounded corners
	);

	// You legend item
	drawList->AddCircleFilled(ImVec2(legendPos.x + 8, legendPos.y + 8), 5.0f, ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.2f, 1.0f)));
	drawList->AddText(ImVec2(legendPos.x + 20, legendPos.y), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.9f)), "You");

	// Other players legend item
	drawList->AddCircleFilled(ImVec2(legendPos.x + 8, legendPos.y + 25), 5.0f, ImGui::GetColorU32(ImVec4(0.4f, 0.8f, 0.4f, 1.0f)));
	drawList->AddText(ImVec2(legendPos.x + 20, legendPos.y + 18), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.9f)), "Other players");

	ImGui::EndChild();     // End PlayersPanel
	ImGui::PopStyleVar(2); // ChildRounding, WindowPadding
}

void GameClient::drawChatPanel(float width, float height)
{
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));

	ImGui::BeginChild("ChatPanel", ImVec2(width, height), true);

	// Chat header with icon
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);                          // Default font
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.59f, 0.98f, 1.00f)); // Accent color
	ImGui::Text(ICON_LC_MESSAGE_CIRCLE " CHAT");
	ImGui::PopStyleColor();
	ImGui::PopFont();

	// separator with gradient
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 sepMin = ImGui::GetCursorScreenPos();
	ImVec2 sepMax = ImVec2(sepMin.x + width - 40, sepMin.y + 1);
	drawList->AddRectFilledMultiColor(sepMin,
	        sepMax,
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.7f)), // Left
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.1f)), // Right
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.1f)), // Right
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.7f))  // Left
	);
	ImGui::Dummy(ImVec2(0, 10)); // Space after separator

	// Chat messages
	float messagesHeight = height - 150; // Reserve space for input
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
	ImGui::BeginChild("ChatMessages", ImVec2(width - 40, messagesHeight), true);

	{
		std::lock_guard<std::mutex> chatLock(chatMutex);
		for (const auto& msg: chatMessages)
		{
			// Format timestamp
			std::string timeStr = Utils::formatTimestamp(msg.timestamp);

			// Format sender with different colors based on type
			ImVec4 senderColor;
			std::string senderIcon;

			if (msg.sender == "System" || msg.sender == "Server")
			{
				senderColor = ImVec4(0.85f, 0.75f, 0.55f, 1.0f); // Gold for system
				senderIcon = ICON_LC_CIRCLE_ALERT " ";
			}
			else if (msg.sender == "You")
			{
				senderColor = ImVec4(0.26f, 0.59f, 0.98f, 1.0f); // Accent blue for self
				senderIcon = ICON_LC_USER " ";
			}
			else
			{
				senderColor = ImVec4(0.4f, 0.8f, 0.4f, 1.0f); // Green for others
				senderIcon = ICON_LC_USER " ";
			}

			// Message bubble style
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
			ImGui::PushStyleColor(ImGuiCol_ChildBg,
			        (msg.sender == "You") ? ImVec4(0.23f, 0.35f, 0.48f, 0.5f) : // Light blue for your messages
			                ImVec4(0.20f, 0.20f, 0.25f, 0.7f)                   // Dark for others
			);

			// Begin message bubble
			ImGui::BeginChild(("Msg" + std::to_string(msg.timestamp)).c_str(), ImVec2(width - 80, 0), ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);

			// Add some extra space at the top of the bubble
			ImGui::Dummy(ImVec2(0, 4));

			ImGui::Indent(16.0f);

			// Print timestamp in muted color
			ImGui::TextColored(ImVec4(0.70f, 0.68f, 0.64f, 0.80f), "%s", timeStr.c_str());

			// Print sender name with icon and color
			ImGui::TextColored(senderColor, "%s%s:", senderIcon.c_str(), msg.sender.c_str());

			// Print message content with better wrapping
			ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
			ImGui::TextWrapped("%s", msg.content.c_str());
			ImGui::PopTextWrapPos();

			ImGui::Unindent(16.0f);

			// Add some extra space at the bottom of the bubble
			ImGui::Dummy(ImVec2(0, 4));

			// End message bubble
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2); // FramePadding, FrameRounding

			// Make sure the next bubble starts right at the end of this one
			// We dont want to let imgui handle the spacing here as it may add too much
			// So we call the setnextitemposition manually
			ImGui::SetCursorPosY(ImGui::GetCursorPosY());
			
		}

		// Auto-scroll to bottom if not manually scrolled
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20)
			ImGui::SetScrollHereY(1.0f);
	}

	ImGui::EndChild();    // End ChatMessages
	ImGui::PopStyleVar(); // ChildRounding

	ImGui::Spacing();

	// Chat input with styling
	bool inputEnabled = (connectionState == ConnectionState::Connected);

	if (!inputEnabled)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
	}

	if (chatFocused && !ImGui::IsAnyItemActive() && inputEnabled)
	{
		ImGui::SetKeyboardFocusHere();
	}

	// Input text flags
	ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
	if (!inputEnabled)
	{
		inputFlags |= ImGuiInputTextFlags_ReadOnly;
	}

	// Support for command history
	handleChatCommandHistory();

	// input field with button
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(15, 12));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 25.0f);

	if (chatFocused && inputEnabled)
	{
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.23f, 0.23f, 0.30f, 1.00f));
	}

	// Chat input field
	float buttonWidth = 100;
	float availableWidth = width - 40 - buttonWidth - ImGui::GetStyle().ItemSpacing.x;

	ImGui::SetNextItemWidth(availableWidth);
	bool enterPressed = ImGui::InputText("##ChatInput", chatInputBuffer, IM_ARRAYSIZE(chatInputBuffer), inputFlags);

	if (chatFocused && inputEnabled)
	{
		ImGui::PopStyleColor();
	}

	ImGui::SameLine();

	// Send button with styling
	ImGui::PushStyleColor(ImGuiCol_Button, inputEnabled ? ImVec4(0.26f, 0.59f, 0.98f, 0.80f) : ImVec4(0.26f, 0.59f, 0.98f, 0.40f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.63f, 1.0f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.51f, 0.90f, 1.0f));

	bool sendClicked = ImGui::Button(ICON_LC_SEND " Send", ImVec2(buttonWidth, 0)) && inputEnabled;

	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(2); // FramePadding, FrameRounding

	if (!inputEnabled)
	{
		ImGui::PopStyleVar();
	}

	// Process chat message
	if ((enterPressed || sendClicked) && inputEnabled)
	{
		processChatInput();
	}

	// Helper text
	if (strlen(chatInputBuffer) == 0 && !ImGui::IsItemActive())
	{
		float textWidth = ImGui::CalcTextSize("Type a message...").x;
		float textX = ImGui::GetItemRectMin().x - availableWidth;
		float textY = ImGui::GetItemRectMin().y + 12;

		ImGui::GetWindowDrawList()->AddText(ImVec2(textX, textY), ImGui::GetColorU32(ImVec4(0.60f, 0.60f, 0.60f, 0.50f)), "Type a message...");
	}

	ImGui::EndChild();     // End ChatPanel
	ImGui::PopStyleVar(2); // ChildRounding, WindowPadding
}

void GameClient::drawControlsPanel(float width, float height)
{
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));

	ImGui::BeginChild("ControlsPanel", ImVec2(width, height), true);

	// Panel header with icon
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);                          // Default font
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.59f, 0.98f, 1.00f)); // Accent color
	ImGui::Text(ICON_LC_SETTINGS " CONTROLS");
	ImGui::PopStyleColor();
	ImGui::PopFont();

	// separator with gradient
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 sepMin = ImGui::GetCursorScreenPos();
	ImVec2 sepMax = ImVec2(sepMin.x + width - 40, sepMin.y + 1);
	drawList->AddRectFilledMultiColor(sepMin,
	        sepMax,
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.7f)), // Left
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.1f)), // Right
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.1f)), // Right
	        ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.7f))  // Left
	);
	ImGui::Dummy(ImVec2(0, 10)); // Space after separator

	// Draw the controls UI with styling
	drawNetworkOptionsUI();

	ImGui::EndChild();     // End ControlsPanel
	ImGui::PopStyleVar(2); // ChildRounding, WindowPadding
}

void GameClient::drawNetworkOptionsUI()
{
	// Network settings
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 15));

	
	{
		// Position update rate
		int updateRate = positionUpdateRateMs;
		if (ImGui::SliderInt(ICON_LC_WIFI " Update Rate (ms)", &updateRate, 50, 500))
		{
			positionUpdateRateMs = updateRate;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::Text("Controls how often position updates are sent.\nLower values = more responsive but higher bandwidth.");
			ImGui::EndTooltip();
		}
		// Movement threshold
		float threshold = movementThreshold;
		if (ImGui::SliderFloat(ICON_LC_MAP_PIN " Movement Threshold", &threshold, 0.01f, 1.0f, "%.2f"))
		{
			movementThreshold = threshold;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::Text("Minimum distance to move before sending a position update.\nHigher values = less updates but less smooth movement.");
			ImGui::EndTooltip();
		}
		// Delta compression
		bool useDelta = useCompressedUpdates;
		if (ImGui::Checkbox(ICON_LC_COMMAND " Use Delta Compression", &useDelta))
		{
			useCompressedUpdates = useDelta;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::Text("Only send coordinates that changed significantly.\nReduces bandwidth at the cost of slightly more CPU.");
			ImGui::EndTooltip();
		}
		// Stats display
		if (showStats)
		{
			ImGui::Separator();
			ImGui::Text(ICON_LC_CHART_SPLINE " Bandwidth Stats");
			ImGui::Text("Position Updates: %d/sec", positionUpdateRateMs > 0 ? (1000 / positionUpdateRateMs) : 0);
			// Calculate average bandwidth for position updates
			float updatesPerSecond = 1000.0f / positionUpdateRateMs;
			float bytesPerUpdate = useCompressedUpdates ? 12.0f : 28.0f; // Estimated average sizes
			float bandwidthBps = updatesPerSecond * bytesPerUpdate;
			ImGui::Text(ICON_LC_TRENDING_UP " Estimated Position Bandwidth: %.2f KB/s", bandwidthBps / 1024.0f);
		}
	}

	ImGui::Spacing();
	ImGui::Dummy(ImVec2(0, 10));

	// Section: Display Options
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
		ImGui::Text(ICON_LC_MONITOR " Display Options");
		ImGui::PopStyleColor();
		ImGui::Separator();
		ImGui::Spacing();

		// Show Stats Checkbox
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::Checkbox("Show Network Statistics", &showStats);

		// Other display options can be added here
		ImGui::PopStyleVar();
	}

	ImGui::Spacing();
	ImGui::Dummy(ImVec2(0, 10));

	// Section: Actions
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
		ImGui::Text(ICON_LC_ACTIVITY " Actions");
		ImGui::PopStyleColor();
		ImGui::Separator();
		ImGui::Spacing();

		// button styling
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

		// Chat Clear button
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.40f, 0.60f, 0.80f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.45f, 0.65f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.35f, 0.55f, 1.0f));

		if (ImGui::Button(ICON_LC_X " Clear Chat", ImVec2(-1, 35)))
		{
			clearChatMessages();
		}

		ImGui::PopStyleColor(3);

		// Exit button
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.30f, 0.30f, 0.80f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.40f, 0.40f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.25f, 0.25f, 1.0f));

		if (ImGui::Button(ICON_LC_LOG_OUT " Exit Application", ImVec2(-1, 35)))
		{
			shouldExit = true;
		}

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
	}

	ImGui::PopStyleVar(2); // FramePadding, ItemSpacing
}

// Draw the UI with ImGui
void GameClient::drawUI()
{
	// Full window without decorations
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);

	ImGui::Begin("MMO Client", nullptr, window_flags);

	// Check connection state and draw appropriate UI
	if ((connectionState == ConnectionState::LoginScreen || connectionState == ConnectionState::RegisterScreen || connectionState == ConnectionState::Connecting || connectionState == ConnectionState::Authenticating) && !connectionInProgress)
	{
		if (connectionState == ConnectionState::RegisterScreen)
		{
			drawRegisterScreen();
		}
		else
		{
			drawLoginScreen();
		}
	}
	else
	{
		drawConnectedUI();
	}

	ImGui::End();

	// Check if we should exit
	if (shouldExit)
	{
		HelloImGui::GetRunnerParams()->appShallExit = true;
	}
}

// Get current time in milliseconds
uint32_t GameClient::getCurrentTimeMs()
{
	return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

void GameClient::setupImGuiTheme()
{
	ImGuiStyle& style = ImGui::GetStyle();

	// spacing and rounding
	style.WindowPadding = ImVec2(15, 15);
	style.FramePadding = ImVec2(10, 7);
	style.ItemSpacing = ImVec2(12, 8);
	style.ItemInnerSpacing = ImVec2(8, 6);
	style.ScrollbarSize = 15;
	style.GrabMinSize = 5;

	// Rounded corners throughout the UI
	style.WindowRounding = 12.0f;
	style.ChildRounding = 8.0f;
	style.FrameRounding = 6.0f;
	style.PopupRounding = 6.0f;
	style.ScrollbarRounding = 10.0f;
	style.GrabRounding = 6.0f;
	style.TabRounding = 6.0f;

	// borders
	style.SeparatorTextBorderSize = 1.0f;
	style.ChildBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;
	style.PopupBorderSize = 1.0f;
	style.TabBorderSize = 0.0f;

	// dark color scheme
	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f); // Bright text
	colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.16f, 1.00f); // Dark background with slight blue tint
	colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.11f, 0.14f, 0.94f);
	colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.30f, 0.50f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.26f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.32f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.13f, 0.53f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.40f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.50f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.60f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f); // Accent blue for checks
	colors[ImGuiCol_SliderGrab] = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);        // Semi-transparent accent for buttons
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f); // Full accent on hover
	colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);  // Darker accent when pressed
	colors[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.38f, 0.50f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.30f, 0.30f, 0.38f, 0.78f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.20f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
	colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.18f, 0.23f, 0.95f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
	colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.41f, 0.68f, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.15f, 0.15f, 0.19f, 1.00f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.18f, 0.18f, 0.23f, 1.00f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.70f);
}
