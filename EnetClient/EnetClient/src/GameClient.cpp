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

// Constructor
GameClient::GameClient() : lastUpdateTime(getCurrentTimeMs())
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
		        addChatMessage("System", "Authentication successful! Welcome to the server.");

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
			addChatMessage("System", "Player left: " + it->second.name);
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
	msg.timestamp = getCurrentTimeMs();

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
	addChatMessage("System", "Chat history cleared");
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

	// Add a system message to chat for better UX
	addChatMessage("System", "Connecting to server at " + networkManager->getServerAddress() + ":" + std::to_string(networkManager->getServerPort()) + "...");

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
		        addChatMessage("System", "Authentication successful! Welcome to the server.");
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

void GameClient::drawNetworkOptionsUI()
{
	if (ImGui::CollapsingHeader("Network Options"))
	{
		ImGui::Indent(10);

		// Position update rate
		int updateRate = positionUpdateRateMs;
		if (ImGui::SliderInt("Update Rate (ms)", &updateRate, 50, 500))
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
		if (ImGui::SliderFloat("Movement Threshold", &threshold, 0.01f, 1.0f, "%.2f"))
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
		if (ImGui::Checkbox("Use Delta Compression", &useDelta))
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
			ImGui::Text("Bandwidth Stats");
			ImGui::Text("Position Updates: %d/sec", positionUpdateRateMs > 0 ? (1000 / positionUpdateRateMs) : 0);

			// Calculate average bandwidth for position updates
			float updatesPerSecond = 1000.0f / positionUpdateRateMs;
			float bytesPerUpdate = useCompressedUpdates ? 12.0f : 28.0f; // Estimated average sizes
			float bandwidthBps = updatesPerSecond * bytesPerUpdate;

			ImGui::Text("Estimated Position Bandwidth: %.2f KB/s", bandwidthBps / 1024.0f);
		}

		ImGui::Unindent(10);
	}
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
		// Center text
		const char* connectingText = "Connecting to server...";
		ImVec2 textSize = ImGui::CalcTextSize(connectingText);
		ImGui::SetCursorPosX((contentWidth - textSize.x) * 0.5f);
		ImGui::Text("%s", connectingText);

		// Progress bar
		const float progressBarWidth = contentWidth * 0.9f;
		ImGui::SetCursorPosX((contentWidth - progressBarWidth) * 0.5f);
		ImGui::ProgressBar(connectionProgress, ImVec2(progressBarWidth, 20), "");
		ImGui::Spacing();

		// Cancel button
		const float buttonWidth = 120.0f;
		ImGui::SetCursorPosX((contentWidth - buttonWidth) * 0.5f);
		if (ImGui::Button("Cancel", ImVec2(buttonWidth, 30)))
		{
			disconnect(false);
			connectionProgress = 0.0f;
			loginErrorMessage = "Connection canceled by user";
		}
	}
	else
	{
		// Login form with better alignment
		const float labelWidth = 80.0f; // Width for labels

		// Server selection with consistent alignment
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Server:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(contentWidth - labelWidth);
		char serverBuf[128];
		strncpy_s(serverBuf, networkManager->getServerAddress().c_str(), sizeof(serverBuf) - 1);
		if (ImGui::InputText("##Server", serverBuf, sizeof(serverBuf)))
		{
			networkManager->setServerAddress(serverBuf);
		}
		ImGui::Spacing();

		// Username field with consistent alignment
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Username:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(contentWidth - labelWidth);
		ImGui::InputText("##Username", loginUsernameBuffer, sizeof(loginUsernameBuffer));

		// Password field with better alignment
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Password:");
		ImGui::SameLine();
		ImGuiInputTextFlags passwordFlags = showPassword ? 0 : ImGuiInputTextFlags_Password;
		ImGui::SetNextItemWidth(contentWidth - labelWidth - 60);
		ImGui::InputText("##Password", loginPasswordBuffer, sizeof(loginPasswordBuffer), passwordFlags);
		ImGui::SameLine();
		if (ImGui::Button(showPassword ? "Hide" : "Show", ImVec2(50, 0)))
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
			ImVec2 textSize = ImGui::CalcTextSize(loginErrorMessage.c_str());
			if (textSize.x > contentWidth - 20.0f)
			{
				// For long messages, use wrapping
				ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + contentWidth);
				ImGui::TextWrapped("%s", loginErrorMessage.c_str());
				ImGui::PopTextWrapPos();
			}
			else
			{
				// For short messages, center
				ImGui::SetCursorPosX((contentWidth - textSize.x) * 0.5f);
				ImGui::Text("%s", loginErrorMessage.c_str());
			}

			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		// Connect button
		const float buttonWidth = 120.0f;
		ImGui::SetCursorPosX((contentWidth - buttonWidth) * 0.5f);
		if (ImGui::Button("Connect", ImVec2(buttonWidth, 30)))
		{
			// Initiate connection
			initiateConnection();
		}
	}

	ImGui::End();
}

// Draw the UI with ImGui
void GameClient::drawUI()
{
	// Use the full window with no padding and no window decorations
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);

	ImGui::Begin("MMO Client", nullptr, window_flags);

	// Check connection state and draw appropriate UI
	if ((connectionState == ConnectionState::LoginScreen || connectionState == ConnectionState::Connecting || connectionState == ConnectionState::Authenticating) && !connectionInProgress)
	{
		drawLoginScreen();
	}
	else
	{
		// Create a nice header section with better styling
		ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Use default font but could be larger font if available
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));

		// Header section with status
		{
			ImGui::BeginChild("HeaderSection", ImVec2(ImGui::GetContentRegionAvail().x, 60), true, ImGuiWindowFlags_NoScrollbar);

			// Left side - title and version
			ImGui::Text("MMO Client v%s", VERSION);

			// Right side - connection status
			float statusWidth = 150;
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - statusWidth);

			// Show different status based on connection state
			ImVec4 statusColor;
			const char* statusText;

			if (connectionInProgress)
			{
				statusColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
				statusText = "CONNECTING...";
			}
			else
			{
				switch (connectionState)
				{
					case ConnectionState::Connecting:
						statusColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
						statusText = "CONNECTING...";
						break;
					case ConnectionState::Authenticating:
						statusColor = ImVec4(0.8f, 0.8f, 0.2f, 1.0f);
						statusText = "AUTHENTICATING...";
						break;
					case ConnectionState::Connected:
						statusColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
						statusText = "CONNECTED";
						break;
					default:
						statusColor = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
						statusText = "DISCONNECTED";
						break;
				}
			}

			ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
			ImGui::TextUnformatted(statusText);
			ImGui::PopStyleColor();

			// Player info below the header
			ImGui::Text("Player: %s (ID: %u)", myPlayerName.c_str(), myPlayerId);
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - 250);
			ImGui::Text("Position: X=%.1f Y=%.1f Z=%.1f", myPosition.x, myPosition.y, myPosition.z);

			ImGui::EndChild();
		}

		ImGui::PopStyleVar(); // ItemSpacing
		ImGui::PopFont();

		// Layout with 3 panels using columns
		ImGui::Columns(3, "MainLayout", false);

		// Left panel - Player list
		{
			ImGui::BeginChild("PlayersPanel", ImVec2(ImGui::GetColumnWidth(), ImGui::GetContentRegionAvail().y - 60), true);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.90f, 0.85f, 1.00f)); // Primary color (#EEE5DA)
			ImGui::TextUnformatted("PLAYERS ONLINE");
			ImGui::PopStyleColor();
			ImGui::Separator();

			// Network stats display if enabled
			if (showStats)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.78f, 0.74f, 1.00f)); // Muted primary color
				ImGui::TextWrapped("Ping: %ums | Packets: %u/%u | Data: %u/%u bytes", networkManager->getPing(), networkManager->getPacketsSent(), networkManager->getPacketsReceived(), networkManager->getBytesSent(), networkManager->getBytesReceived());
				ImGui::PopStyleColor();
				ImGui::Separator();
			}

			// Display other players
			{
				const float listHeight = ImGui::GetContentRegionAvail().y - 340; // Reserve space for the map
				ImGui::BeginChild("PlayersList", ImVec2(ImGui::GetContentRegionAvail().x, listHeight), false);

				std::lock_guard<std::mutex> lock(playersMutex);
				if (otherPlayers.empty())
				{
					ImGui::TextColored(ImVec4(0.80f, 0.78f, 0.74f, 1.00f), "No other players online");
				}
				else
				{
					for (const auto& pair: otherPlayers)
					{
						const PlayerInfo& player = pair.second;
						// Show a colored indicator and player name with ID
						ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "●"); // Keep green for player indicator
						ImGui::SameLine();
						ImGui::TextUnformatted(player.name.c_str());
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(0.70f, 0.68f, 0.64f, 1.00f), "(ID: %u)", player.id);
						// Show position indented
						ImGui::Indent(10);
						ImGui::TextColored(ImVec4(0.80f, 0.78f, 0.74f, 1.00f), "Position: %.1f, %.1f, %.1f", player.position.x, player.position.y, player.position.z);
						ImGui::Unindent(10);
					}
				}
				ImGui::EndChild();
			}

			// Player position diagram
			{
				ImGui::Separator();
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.90f, 0.85f, 1.00f)); // Primary color
				ImGui::TextUnformatted("PLAYER POSITIONS");
				ImGui::PopStyleColor();

				// Calculate map size and position
				const float mapSize = ImGui::GetContentRegionAvail().x - 10;
				const float mapPadding = 5.0f;
				const ImVec2 mapPos = ImGui::GetCursorScreenPos();
				const ImVec2 mapCenter = ImVec2(mapPos.x + mapSize / 2, mapPos.y + mapSize / 2);

				// Create a square canvas for the map
				ImGui::InvisibleButton("PositionMap", ImVec2(mapSize, mapSize));
				ImDrawList* drawList = ImGui::GetWindowDrawList();

				// Draw map background and border
				drawList->AddRectFilled(ImVec2(mapPos.x, mapPos.y),
				        ImVec2(mapPos.x + mapSize, mapPos.y + mapSize),
				        ImGui::GetColorU32(ImVec4(0.15f, 0.14f, 0.14f, 1.00f)), // Background color
				        4.0f                                                    // Rounded corners
				);
				drawList->AddRect(ImVec2(mapPos.x, mapPos.y),
				        ImVec2(mapPos.x + mapSize, mapPos.y + mapSize),
				        ImGui::GetColorU32(ImVec4(0.30f, 0.30f, 0.30f, 0.50f)), // Border color
				        4.0f,                                                   // Rounded corners
				        0,
				        1.5f // Line thickness
				);

				// Draw grid lines
				const int gridLines = 4; // 4x4 grid
				const float gridStep = mapSize / gridLines;
				const float gridAlpha = 0.3f;

				for (int i = 1; i < gridLines; i++)
				{
					// Horizontal lines
					drawList->AddLine(ImVec2(mapPos.x, mapPos.y + i * gridStep), ImVec2(mapPos.x + mapSize, mapPos.y + i * gridStep), ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, gridAlpha)));

					// Vertical lines
					drawList->AddLine(ImVec2(mapPos.x + i * gridStep, mapPos.y), ImVec2(mapPos.x + i * gridStep, mapPos.y + mapSize), ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, gridAlpha)));
				}

				// Get world boundaries for scaling
				// Note: In a real application, you'd determine these dynamically based on the game world
				// For this example, I'm using hardcoded values that should be replaced with your game data
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
					std::lock_guard<std::mutex> lock(playersMutex);

					// Draw local player (assuming there's a localPlayer object)
					ImVec2 playerPos = worldToMap(myPosition.x, -myPosition.z);
					// Draw local player as a yellow dot with a border
					drawList->AddCircleFilled(playerPos, 5.0f, ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.2f, 1.0f)));
					drawList->AddCircle(playerPos, 5.0f, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.5f)), 0, 1.5f);

					// Draw other players
					for (const auto& pair: otherPlayers)
					{
						const PlayerInfo& player = pair.second;
						ImVec2 playerPos = worldToMap(player.position.x, -player.position.z);

						// Draw player as a green dot
						drawList->AddCircleFilled(playerPos, 4.0f, ImGui::GetColorU32(ImVec4(0.4f, 0.8f, 0.4f, 1.0f)));
						drawList->AddCircle(playerPos, 4.0f, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.5f)), 0, 1.0f);

						// Draw player name if mouse is hovering over the dot
						if ((ImGui::IsItemHovered() || true) && ImGui::IsMouseHoveringRect(ImVec2(playerPos.x - 5, playerPos.y - 5), ImVec2(playerPos.x + 5, playerPos.y + 5)))
						{
							ImGui::BeginTooltip();
							ImGui::Text("%s (ID: %u)", player.name.c_str(), player.id);
							ImGui::Text("Position: %.1f, %.1f, %.1f", player.position.x, player.position.y, player.position.z);
							ImGui::EndTooltip();
						}
					}
				}

				// Add compass indicators
				float compassRadius = 15.0f;
				float compassOffset = 20.0f;
				ImVec2 compassPos = ImVec2(mapPos.x + mapSize - compassOffset, mapPos.y + compassOffset);

				// Draw compass circle
				drawList->AddCircleFilled(compassPos, compassRadius, ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.15f, 0.7f)));
				drawList->AddCircle(compassPos, compassRadius, ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.7f)));

				// Draw direction markers (N, E, S, W)
				drawList->AddText(ImVec2(compassPos.x - 4, compassPos.y - compassRadius - 12), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.7f)), "N");
				drawList->AddText(ImVec2(compassPos.x + compassRadius + 4, compassPos.y - 8), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.7f)), "E");
				drawList->AddText(ImVec2(compassPos.x - 4, compassPos.y + compassRadius), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.7f)), "S");
				drawList->AddText(ImVec2(compassPos.x - compassRadius - 10, compassPos.y - 8), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.7f)), "W");

				// Draw legend
				ImVec2 legendPos = ImVec2(mapPos.x + 10, mapPos.y + mapSize - 25);
				drawList->AddCircleFilled(ImVec2(legendPos.x + 5, legendPos.y + 5), 4.0f, ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.2f, 1.0f)));
				drawList->AddText(ImVec2(legendPos.x + 15, legendPos.y), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.7f)), "You");

				drawList->AddCircleFilled(ImVec2(legendPos.x + 50, legendPos.y + 5), 4.0f, ImGui::GetColorU32(ImVec4(0.4f, 0.8f, 0.4f, 1.0f)));
				drawList->AddText(ImVec2(legendPos.x + 60, legendPos.y), ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.7f)), "Other players");
			}

			ImGui::EndChild();
		}
		ImGui::NextColumn();

		// Middle panel - Chat
		{
			ImGui::BeginChild("ChatPanel", ImVec2(ImGui::GetColumnWidth(), ImGui::GetContentRegionAvail().y - 30), true);

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.90f, 0.85f, 1.00f)); // Primary color (#EEE5DA)
			ImGui::TextUnformatted("CHAT");
			ImGui::PopStyleColor();

			ImGui::Separator();

			// Chat message display with scrolling
			ImGui::BeginChild("ChatMessages", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 10), true);
			{
				std::lock_guard<std::mutex> chatLock(chatMutex);
				for (const auto& msg: chatMessages)
				{
					// Format timestamp (could be improved)
					std::string timeStr = "[" + std::to_string(msg.timestamp) + "]";

					// Format sender with different colors based on type
					ImVec4 senderColor;
					if (msg.sender == "System" || msg.sender == "Server")
						senderColor = ImVec4(0.85f, 0.75f, 0.55f, 1.0f); // Gold-ish for system
					else if (msg.sender == "You")
						senderColor = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); // Keep green for self
					else
						senderColor = ImVec4(0.93f, 0.90f, 0.85f, 1.0f); // Primary color for others

					// Print timestamp in muted color
					ImGui::TextColored(ImVec4(0.70f, 0.68f, 0.64f, 1.00f), "%s", timeStr.c_str());
					ImGui::SameLine();

					// Print sender name with color
					ImGui::TextColored(senderColor, "%s:", msg.sender.c_str());
					ImGui::SameLine();

					// Print message content
					ImGui::TextWrapped("%s", msg.content.c_str());
				}

				// Auto-scroll to bottom if not manually scrolled
				if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20)
					ImGui::SetScrollHereY(1.0f);
			}
			ImGui::EndChild();

			// Chat input with send button - only enabled when connected
			bool inputEnabled = (connectionState == ConnectionState::Connected);

			if (!inputEnabled)
			{
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
			}

			if (chatFocused && !ImGui::IsAnyItemActive() && inputEnabled)
			{
				ImGui::SetKeyboardFocusHere();
			}

			// Create flags for input text
			ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
			if (!inputEnabled)
			{
				inputFlags |= ImGuiInputTextFlags_ReadOnly;
			}

			// Add command history support
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && chatFocused && !commandHistory.empty() && inputEnabled)
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

			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && chatFocused && !commandHistory.empty() && commandHistoryIndex != -1 && inputEnabled)
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

			float availableWidth = ImGui::GetContentRegionAvail().x - 60 - ImGui::GetStyle().ItemSpacing.x;

			// Begin a group to manage the layout
			ImGui::BeginGroup();

			// Apply your style if focused
			if (chatFocused && inputEnabled)
			{
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.24f, 0.24f, 1.00f));
			}

			// Use the calculated width for the input text
			ImGui::SetNextItemWidth(availableWidth);
			bool enterPressed = ImGui::InputText("##ChatInput", chatInputBuffer, IM_ARRAYSIZE(chatInputBuffer), inputFlags);

			if (chatFocused && inputEnabled)
			{
				ImGui::PopStyleColor();
			}

			ImGui::SameLine();
			bool sendClicked = ImGui::Button("Send", ImVec2(60, 0)) && inputEnabled;

			// End the group
			ImGui::EndGroup();

			if (!inputEnabled)
			{
				ImGui::PopStyleVar();
			}

			if ((enterPressed || sendClicked) && inputEnabled)
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

			ImGui::EndChild();
		}
		ImGui::NextColumn();

		// Right panel - Controls
		{
			ImGui::BeginChild("Controls", ImVec2(ImGui::GetColumnWidth(), ImGui::GetContentRegionAvail().y - 30), true);
			drawNetworkOptionsUI();

			ImGui::EndChild();
		}
		ImGui::Columns(1);

		// Bottom status bar
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.20f, 0.19f, 0.19f, 1.00f)); // Light background
		ImGui::BeginChild("StatusBar", ImVec2(ImGui::GetContentRegionAvail().x, 25), true, ImGuiWindowFlags_NoScrollbar);

		// Adjust vertical position to center text
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5);
		ImGui::Text("Ping: %ums | Packets: %u/%u | Data: %u/%u bytes", networkManager->getPing(), networkManager->getPacketsSent(), networkManager->getPacketsReceived(), networkManager->getBytesSent(), networkManager->getBytesReceived());
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 150);
		ImGui::Text("Players Online: %zu", otherPlayers.size() + 1); // +1 for self

		ImGui::EndChild();
		ImGui::PopStyleColor(); // ChildBg
	}

	ImGui::End();

	// Check if we should exit
	if (shouldExit)
	{
		// Signal ImGui to exit
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
	ImVec4* colors = style.Colors;

	// Color palette
	ImVec4 bgColor = ImVec4(0.15f, 0.14f, 0.14f, 1.00f);          // #262424
	ImVec4 lightBgColor = ImVec4(0.20f, 0.19f, 0.19f, 1.00f);     // Slightly lighter than #262424
	ImVec4 veryLightBgColor = ImVec4(0.25f, 0.24f, 0.24f, 1.00f); // Even lighter

	ImVec4 primaryColor = ImVec4(0.93f, 0.90f, 0.85f, 1.00f);       // #EEE5DA
	ImVec4 primaryColorHover = ImVec4(0.96f, 0.93f, 0.88f, 1.00f);  // Slightly lighter
	ImVec4 primaryColorActive = ImVec4(0.90f, 0.87f, 0.82f, 1.00f); // Slightly darker

	ImVec4 textColor = ImVec4(0.93f, 0.90f, 0.85f, 1.00f); // #EEE5DA
	ImVec4 textColorDisabled = ImVec4(0.93f, 0.90f, 0.85f, 0.50f);
	ImVec4 borderColor = ImVec4(0.30f, 0.30f, 0.30f, 0.50f);
	ImVec4 accentColor = ImVec4(0.70f, 0.70f, 0.60f, 1.00f); // Muted gold accent

	// Global style settings
	style.FrameRounding = 4.0f;
	style.GrabRounding = 4.0f;
	style.WindowRounding = 8.0f;
	style.ChildRounding = 4.0f;
	style.PopupRounding = 4.0f;
	style.ScrollbarRounding = 4.0f;
	style.TabRounding = 4.0f;
	style.WindowBorderSize = 1.0f;
	style.ChildBorderSize = 1.0f;
	style.PopupBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;
	style.TabBorderSize = 0.0f;
	style.ItemSpacing = ImVec2(8.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
	style.IndentSpacing = 20.0f;
	style.ScrollbarSize = 10.0f;
	style.GrabMinSize = 7.0f;
	style.WindowPadding = ImVec2(10.0f, 10.0f);
	style.FramePadding = ImVec2(6.0f, 4.0f);
	style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
	style.ButtonTextAlign = ImVec2(0.5f, 0.5f);

	// Colors
	colors[ImGuiCol_Text] = textColor;
	colors[ImGuiCol_TextDisabled] = textColorDisabled;

	colors[ImGuiCol_WindowBg] = bgColor;
	colors[ImGuiCol_ChildBg] = bgColor;
	colors[ImGuiCol_PopupBg] = bgColor;

	colors[ImGuiCol_Border] = borderColor;
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

	colors[ImGuiCol_FrameBg] = lightBgColor;
	colors[ImGuiCol_FrameBgHovered] = veryLightBgColor;
	colors[ImGuiCol_FrameBgActive] = veryLightBgColor;

	colors[ImGuiCol_TitleBg] = bgColor;
	colors[ImGuiCol_TitleBgActive] = lightBgColor;
	colors[ImGuiCol_TitleBgCollapsed] = bgColor;

	colors[ImGuiCol_MenuBarBg] = lightBgColor;

	colors[ImGuiCol_ScrollbarBg] = bgColor;
	colors[ImGuiCol_ScrollbarGrab] = lightBgColor;
	colors[ImGuiCol_ScrollbarGrabHovered] = veryLightBgColor;
	colors[ImGuiCol_ScrollbarGrabActive] = primaryColor;

	colors[ImGuiCol_CheckMark] = primaryColor;

	colors[ImGuiCol_SliderGrab] = primaryColor;
	colors[ImGuiCol_SliderGrabActive] = primaryColorActive;

	colors[ImGuiCol_Button] = lightBgColor;
	colors[ImGuiCol_ButtonHovered] = veryLightBgColor;
	colors[ImGuiCol_ButtonActive] = primaryColor;

	colors[ImGuiCol_Header] = lightBgColor;
	colors[ImGuiCol_HeaderHovered] = veryLightBgColor;
	colors[ImGuiCol_HeaderActive] = primaryColor;

	colors[ImGuiCol_Separator] = borderColor;
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.78f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

	colors[ImGuiCol_ResizeGrip] = ImVec4(primaryColor.x, primaryColor.y, primaryColor.z, 0.20f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(primaryColor.x, primaryColor.y, primaryColor.z, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = primaryColor;

	colors[ImGuiCol_Tab] = lightBgColor;
	colors[ImGuiCol_TabHovered] = veryLightBgColor;
	colors[ImGuiCol_TabActive] = primaryColor;
	colors[ImGuiCol_TabUnfocused] = bgColor;
	colors[ImGuiCol_TabUnfocusedActive] = lightBgColor;

	colors[ImGuiCol_PlotLines] = primaryColor;
	colors[ImGuiCol_PlotLinesHovered] = primaryColorHover;
	colors[ImGuiCol_PlotHistogram] = primaryColor;
	colors[ImGuiCol_PlotHistogramHovered] = primaryColorHover;

	colors[ImGuiCol_TextSelectedBg] = ImVec4(primaryColor.x, primaryColor.y, primaryColor.z, 0.35f);

	colors[ImGuiCol_DragDropTarget] = primaryColor;
	colors[ImGuiCol_NavHighlight] = primaryColor;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
}
