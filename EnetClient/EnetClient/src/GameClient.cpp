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
#include "MarkdownHelper.h"
#include "Utils.h"

// Constructor
GameClient::GameClient(bool isDebuggerAttached)
      : lastUpdateTime(time(0)), themeManager(logger, isDebuggerAttached)
{
	// Set up console
	consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (consoleHandle == INVALID_HANDLE_VALUE)
	{
		logger.error("Failed to get console handle");
	}

	// Create the thread manager first - use slightly fewer threads than max
	// to leave some CPU for the main thread and UI
	size_t numThreads = max(1u, std::thread::hardware_concurrency() - 2);
	threadManager = std::make_shared<ThreadManager>(numThreads);

	// Create NetworkManager with thread manager
	networkManager = std::make_shared<NetworkManager>(logger, threadManager);

	// Create AuthManager with NetworkManager and thread manager
	authManager = std::make_shared<AuthManager>(logger, networkManager, threadManager);

	// Complete the circular reference so NetworkManager can forward auth responses
	networkManager->setAuthManager(authManager);

	// Try to load stored credentials if none provided
	if (myPlayerName.empty() || myPassword.empty())
	{
		std::string loadedUsername, loadedPassword;

		// Load credentials on thread pool and wait for result
		auto future = threadManager->scheduleTaskWithResult([this, &loadedUsername, &loadedPassword]() { return authManager->loadCredentials(loadedUsername, loadedPassword); });

		if (future.get()) // Wait for the result
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

	// First clean up NetworkManager and AuthManager to stop any ongoing operations
	networkManager.reset();
	authManager.reset();

	// Wait for all tasks to complete before shutting down
	if (threadManager)
	{
		threadManager->waitForTasks();
		threadManager.reset();
	}

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

	// Try to reconnect if not exiting
	if (!shouldExit && !reconnecting && autoLogin)
	{
		// Use thread manager for reconnection attempt
		threadManager->scheduleNetworkTask([this]() { networkManager->reconnectToServer(); });
	}
}

void GameClient::applyTheme()
{
	themeManager.applyTheme();
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

		// Add the command to the chat
		addChatMessage("You - command", command);

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
			logger.warning("Not connected to server or not authenticated, cannot send command: " + command);
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

	if (message.substr(0, 13) == "REG_RESPONSE:")
	{
		std::string response = message.substr(13);

		if (response.substr(0, 7) == "SUCCESS")
		{
			// Registration successful
			logger.info("Registration successful!");
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
				it->second.lastSeen = time(0);
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
				newPlayer.lastSeen = time(0);
				newPlayer.status = "active";

				otherPlayers[id] = newPlayer;
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
	std::lock_guard<std::mutex> guard(chatMutex);

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

// Updated to synchronous connection
void GameClient::startConnection()
{
	connectionProgress = 0.0f;
	connectionState = ConnectionState::Connecting;
	loginErrorMessage.clear();

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

	// Update state
	connectionProgress = 0.1f;

	// Use thread manager for connection
	threadManager->scheduleNetworkTask(
	        [this]()
	        {
		        // Connect to server using NetworkManager
		        bool connected = networkManager->connectToServer(networkManager->getServerAddress().c_str(), networkManager->getServerPort());

		        if (!connected)
		        {
			        // Update UI safely
			        threadManager->scheduleUITask(
			                [this]()
			                {
				                loginErrorMessage = "Failed to connect to server";
				                connectionState = ConnectionState::LoginScreen;
			                });
			        return;
		        }

		        // Update connection progress
		        threadManager->scheduleUITask([this]() { connectionProgress = 0.5f; });

		        // Start authentication
		        bool authStarted = authManager->authenticate(
		                myPlayerName,
		                myPassword,
		                rememberCredentials,
		                // Success callback
		                [this](uint32_t playerId)
		                {
			                threadManager->scheduleUITask(
			                        [this, playerId]()
			                        {
				                        this->myPlayerId = playerId;
				                        connectionState = ConnectionState::Connected;

				                        // Send a request to get player position
				                        std::string request = "POSITION:";
				                        networkManager->sendPacket(request);
			                        });
		                },
		                // Failure callback
		                [this](const std::string& errorMsg)
		                {
			                threadManager->scheduleUITask(
			                        [this, errorMsg]()
			                        {
				                        loginErrorMessage = "Authentication failed: " + errorMsg;
				                        connectionState = ConnectionState::LoginScreen;
			                        });
			                networkManager->disconnect(false);
		                });

		        if (!authStarted)
		        {
			        networkManager->disconnect(false);
			        threadManager->scheduleUITask(
			                [this]()
			                {
				                loginErrorMessage = "Failed to start authentication";
				                connectionState = ConnectionState::LoginScreen;
			                });
		        }
		        else
		        {
			        threadManager->scheduleUITask([this]() { connectionState = ConnectionState::Authenticating; });
		        }
	        });

	// Clear the chat messages
	clearChatMessages();
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
	time_t currentTime = time(0);

	// Use NetworkManager to process packets and handle events
	networkManager->update(
	        // Position update callback
	        [this]()
	        {
		        time_t currentTime = time(0);
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
	// Status bar content
	ImGui::BeginChild("StatusBarContent", ImVec2(ImGui::GetContentRegionAvail().x, 30), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// Network stats with icons
	ImGui::SetCursorPos(ImVec2(20, 5));
	ImGui::TextColored(themeManager.getCurrentTheme().textSecondary, ICON_LC_WIFI " %ums | " ICON_LC_PACKAGE " %u/%u | " ICON_LC_HARD_DRIVE " %u/%u bytes", networkManager->getPing(), networkManager->getPacketsSent(), networkManager->getPacketsReceived(), networkManager->getBytesSent(), networkManager->getBytesReceived());

	// Players online with icon
	float playerOnlineTextWidth = ImGui::CalcTextSize(ICON_LC_USERS " Players Online: 999").x;
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - playerOnlineTextWidth - 20);
	ImGui::TextColored(themeManager.getCurrentTheme().accentPrimary, ICON_LC_USERS " Players Online: %zu", otherPlayers.size() + 1); // +1 for self

	ImGui::EndChild();
}

void GameClient::processChatInput()
{
	if (chatInputBuffer[0] != '\0')
	{
		std::string chatStr(chatInputBuffer);
		processChatCommand(chatStr);
		chatInputBuffer[0] = '\0'; // Clear input
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

	// Use thread manager for registration process
	threadManager->scheduleNetworkTask(
	        [this, username, password]()
	        {
		        // Ensure NetworkManager is initialized
		        if (!networkManager->clientSetup())
		        {
			        if (!initialize())
			        {
				        threadManager->scheduleUITask([this]() { registerErrorMessage = "Failed to initialize client"; });
				        return;
			        }
		        }

		        // Connect to the server
		        if (!networkManager->connectToServer(networkManager->getServerAddress().c_str(), networkManager->getServerPort()))
		        {
			        threadManager->scheduleUITask([this]() { registerErrorMessage = "Failed to connect to server"; });
			        return;
		        }

		        // Send registration request
		        std::string registerPacket = "COMMAND:register " + username + " " + password;
		        networkManager->sendPacket(registerPacket);

		        // Update UI state
		        threadManager->scheduleUITask(
		                [this, username, password]()
		                {
			                // Set state to wait for registration response
			                connectionState = ConnectionState::Authenticating;
			                myPlayerName = username;

			                // Copy credentials to login screen for convenience
			                strncpy_s(loginUsernameBuffer, username.c_str(), sizeof(loginUsernameBuffer) - 1);
			                strncpy_s(loginPasswordBuffer, password.c_str(), sizeof(loginPasswordBuffer) - 1);
		                });
	        });
}

std::string GameClient::getThreadStats() const
{
	if (threadManager)
	{
		return threadManager->getStatistics();
	}
	return "Thread manager not initialized";
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

	logger.debug("User signed out");

	clearChatMessages();
}

void GameClient::drawLoginScreen()
{
	// Center the login window on screen
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 windowSize = ImVec2(475, 450);
	ImVec2 windowPos = ImVec2((io.DisplaySize.x - windowSize.x) * 0.5f, (io.DisplaySize.y - windowSize.y) * 0.5f);

	// Set window position and size
	ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

	// Start the window - using game's existing style
	ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

	// Calculate content width for centering
	const float contentWidth = windowSize.x;

	// Game title with proper centering
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Default font

	// Center the title text
	ImVec2 titleSize = ImGui::CalcTextSize(GAME_NAME);
	ImGui::SetCursorPosX((contentWidth - titleSize.x) * 0.5f);
	ImGui::Text(GAME_NAME);
	ImGui::PopFont();

	// Center version text
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
		// Server selection
		ImGui::AlignTextToFramePadding();
		ImGui::Text(ICON_LC_SERVER " Server:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		char serverBuf[128];
		strncpy_s(serverBuf, networkManager->getServerAddress().c_str(), sizeof(serverBuf) - 1);
		if (ImGui::InputText("##Server", serverBuf, sizeof(serverBuf)))
		{
			networkManager->setServerAddress(serverBuf);
		}

		ImGui::Dummy(ImVec2(0.0f, 4.0f));

		// Username field
		ImGui::AlignTextToFramePadding();
		ImGui::Text(ICON_LC_USER " Username:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		ImGui::InputText("##Username", loginUsernameBuffer, sizeof(loginUsernameBuffer));

		// Password field
		ImGui::AlignTextToFramePadding();
		ImGui::Text(ICON_LC_COMMAND " Password: ");
		ImGui::SameLine();
		float labelWidth = ImGui::GetCursorPosX();
		ImGuiInputTextFlags passwordFlags = showPassword ? 0 : ImGuiInputTextFlags_Password;
		ImGui::SetNextItemWidth(contentWidth - labelWidth - 120);
		ImGui::InputText("##Password", loginPasswordBuffer, sizeof(loginPasswordBuffer), passwordFlags);
		ImGui::SameLine();
		if (ImGui::Button(showPassword ? ICON_LC_EYE_OFF " Hide" : ICON_LC_EYE " Show", ImVec2(90, 0)))
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
	ImVec2 windowSize = ImVec2(475, 450);
	ImVec2 windowPos = ImVec2((io.DisplaySize.x - windowSize.x) * 0.5f, (io.DisplaySize.y - windowSize.y) * 0.5f);

	// Set window position and size
	ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

	// Start the window - using game's existing style
	ImGui::Begin("Register", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

	// Calculate content width for centering
	const float contentWidth = windowSize.x;

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

	// Server selection
	ImGui::AlignTextToFramePadding();
	ImGui::Text(ICON_LC_SERVER " Server:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1);
	char serverBuf[128];
	strncpy_s(serverBuf, networkManager->getServerAddress().c_str(), sizeof(serverBuf) - 1);
	if (ImGui::InputText("##Server", serverBuf, sizeof(serverBuf)))
	{
		networkManager->setServerAddress(serverBuf);
	}

	ImGui::Dummy(ImVec2(0.0f, 4.0f));

	// Username field
	ImGui::AlignTextToFramePadding();
	ImGui::Text(ICON_LC_USER " Username:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1);
	ImGui::InputText("##Username", loginUsernameBuffer, sizeof(loginUsernameBuffer));

	// Password field
	ImGui::AlignTextToFramePadding();
	ImGui::Text(ICON_LC_COMMAND " Password: ");
	ImGui::SameLine();
	float labelWidth = ImGui::GetCursorPosX();
	ImGuiInputTextFlags passwordFlags = showPassword ? 0 : ImGuiInputTextFlags_Password;
	ImGui::SetNextItemWidth(contentWidth - labelWidth - 120);
	ImGui::InputText("##Password", loginPasswordBuffer, sizeof(loginPasswordBuffer), passwordFlags);
	ImGui::SameLine();
	if (ImGui::Button(showPassword ? ICON_LC_EYE_OFF " Hide" : ICON_LC_EYE " Show", ImVec2(90, 0)))
	{
		showPassword = !showPassword;
	}

	// Confirm Password field
	ImGui::AlignTextToFramePadding();
	ImGui::Text(ICON_LC_CHECK " Confirm:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1);
	ImGui::InputText("##RegConfirmPassword", registerConfirmPasswordBuffer, sizeof(registerConfirmPasswordBuffer), passwordFlags);

	ImGui::Spacing();

	// Remember credentials option
	ImGui::Checkbox("Remember credentials", &rememberCredentials);
	ImGui::Spacing();

	// Error message with better styling
	if (!registerErrorMessage.empty())
	{
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

		ImGui::Spacing();
	}

	// Register button
	float buttonWidth = 120.0f;
	ImGui::SetCursorPosX((contentWidth - buttonWidth) * 0.5f);
	if (ImGui::Button(ICON_LC_USER_PLUS " Register", ImVec2(buttonWidth, 30)))
	{
		initiateRegistration();
	}

	ImGui::Spacing();

	// Back to login link
	buttonWidth *= 1.15f;
	ImGui::SetCursorPosX((contentWidth - buttonWidth) * 0.5f);
	if (ImGui::Button(ICON_LC_ARROW_LEFT " Back to Login", ImVec2(buttonWidth, 30)))
	{
		connectionState = ConnectionState::LoginScreen;
		registerErrorMessage.clear();
	}

	ImGui::End();
}

void GameClient::drawConnectedUI()
{
	// Header
	drawHeader();

	// Content area with panels
	ImGui::Spacing();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));

	// Auto-layout with proper responsiveness
	float availWidth = ImGui::GetContentRegionAvail().x - 20; // Reserve some padding
	float playersPanelWidth = availWidth * 0.25f;             // 25% for players
	float chatPanelWidth = availWidth * 0.5f;                 // 50% for chat
	float controlsPanelWidth = availWidth * 0.25f;            // 25% for controls

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
	// Content container
	ImGui::BeginChild("HeaderContent", ImVec2(ImGui::GetContentRegionAvail().x, 80), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// Left side - Title and version
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Larger font
	ImGui::SetCursorPosY(15);
	ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(GAME_NAME).x) * 0.5f);
	ImGui::TextColored(themeManager.getCurrentTheme().textAccent, GAME_NAME);

	// Player name centered in the header
	ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(myPlayerName.c_str()).x) * 0.5f);
	ImGui::TextColored(themeManager.getCurrentTheme().textSecondary, "%s", myPlayerName.c_str(), myPlayerId);
	ImGui::PopFont();

	ImGui::EndChild();
}

void GameClient::drawPlayersPanel(float width, float height)
{
	const UITheme& theme = themeManager.getCurrentTheme();

	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.childRounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));

	ImGui::BeginChild("PlayersPanel", ImVec2(width, height), true);

	// Panel header with icon
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Default font
	ImGui::TextColored(theme.textAccent, ICON_LC_USERS " PLAYERS ONLINE");
	ImGui::PopFont();

	// separator with gradient
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 sepMin = ImGui::GetCursorScreenPos();
	ImVec2 sepMax = ImVec2(sepMin.x + width - 40, sepMin.y + 1);
	drawList->AddRectFilledMultiColor(sepMin,
	        sepMax,
	        ImGui::GetColorU32(theme.gradientStart), // Left
	        ImGui::GetColorU32(theme.gradientEnd),   // Right
	        ImGui::GetColorU32(theme.gradientEnd),   // Right
	        ImGui::GetColorU32(theme.gradientStart)  // Left
	);
	ImGui::Dummy(ImVec2(0, 10)); // Space after separator

	// Network stats display
	if (showStats)
	{
		ImGui::BeginChild("NetworkStats", ImVec2(width - 40, 45), true);
		ImGui::TextColored(theme.textSecondary, "Ping: %ums | Packets: %u/%u | Data: %u/%u bytes", networkManager->getPing(), networkManager->getPacketsSent(), networkManager->getPacketsReceived(), networkManager->getBytesSent(), networkManager->getBytesReceived());
		ImGui::EndChild();
		ImGui::Spacing();
	}

	// Display other players with card-like styling
	const float listHeight = height - 525; // Reserve space for the map
	ImGui::BeginChild("PlayersList", ImVec2(width - 40, listHeight), false);

	std::lock_guard<std::mutex> lock(playersMutex);
	if (otherPlayers.empty())
	{
		ImGui::TextColored(theme.textSecondary, ICON_LC_USERS " No other players online");
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

			// Player name and ID
			ImGui::SetCursorPos(ImVec2(30, 8));
			ImGui::TextUnformatted(player.name.c_str());
			ImGui::SameLine();
			ImGui::TextColored(theme.textSecondary, "(ID: %u)", player.id);

			// Position with icon
			ImGui::SetCursorPos(ImVec2(30, 35));
			ImGui::TextColored(theme.textSecondary, ICON_LC_MAP_PIN " %.1f, %.1f, %.1f", player.position.x, player.position.y, player.position.z);

			ImGui::EndChild(); // End of card

			// Card highlight effect on hover
			if (isHovered)
			{
				ImVec2 min = ImGui::GetItemRectMin();
				ImVec2 max = ImGui::GetItemRectMax();
				drawList->AddRect(ImVec2(min.x - 1, min.y - 1),
				        ImVec2(max.x + 1, max.y + 1),
				        ImGui::GetColorU32(theme.accentPrimary), // Accent color
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
	ImGui::TextColored(theme.textAccent, ICON_LC_MAP " PLAYER POSITIONS");

	// Separator with gradient
	ImDrawList* mapSepDrawList = ImGui::GetWindowDrawList();
	ImVec2 mapSepMin = ImGui::GetCursorScreenPos();
	ImVec2 mapSepMax = ImVec2(mapSepMin.x + width - 40, mapSepMin.y + 1);
	mapSepDrawList->AddRectFilledMultiColor(mapSepMin,
	        mapSepMax,
	        ImGui::GetColorU32(theme.gradientStart), // Left
	        ImGui::GetColorU32(theme.gradientEnd),   // Right
	        ImGui::GetColorU32(theme.gradientEnd),   // Right
	        ImGui::GetColorU32(theme.gradientStart)  // Left
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
	        ImGui::GetColorU32(theme.bgTertiary), // Map background
	        10.0f                                 // Rounded corners
	);

	// Map border
	drawList->AddRect(ImVec2(mapPos.x, mapPos.y),
	        ImVec2(mapPos.x + mapSize, mapPos.y + mapSize),
	        ImGui::GetColorU32(theme.borderPrimary), // Border color
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
			ImVec4 glowColor = theme.playerSelf;
			glowColor.w = alpha;
			drawList->AddCircleFilled(playerPos, 5.0f + i * 0.8f, ImGui::GetColorU32(glowColor));
		}

		// Main dot
		drawList->AddCircleFilled(playerPos, 6.0f, ImGui::GetColorU32(theme.playerSelf));
		drawList->AddCircle(playerPos, 6.0f, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.5f)), 0, 1.5f);

		// Draw other players with subtle glow
		for (const auto& pair: otherPlayers)
		{
			const PlayerInfo& player = pair.second;
			ImVec2 otherPlayerPos = worldToMap(player.position.x, -player.position.z);

			// Subtle glow
			ImVec4 otherGlowColor = theme.playerOthers;
			otherGlowColor.w = 0.3f;
			drawList->AddCircleFilled(otherPlayerPos, 7.0f, ImGui::GetColorU32(otherGlowColor));

			// Player dot
			drawList->AddCircleFilled(otherPlayerPos, 5.0f, ImGui::GetColorU32(theme.playerOthers));
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

	// Compass with translucent background
	float compassRadius = 18.0f;
	float compassOffset = 25.0f;
	ImVec2 compassPos = ImVec2(mapPos.x + mapSize - compassOffset, mapPos.y + compassOffset);

	// Compass background with gradient
	drawList->AddCircleFilled(compassPos, compassRadius + 3.0f, ImGui::GetColorU32(ImVec4(0.15f, 0.15f, 0.15f, 0.7f)));

	// Direction indicators - North (triangle)
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
	drawList->AddText(ImVec2(compassPos.x - 4, compassPos.y - compassRadius - 16), ImGui::GetColorU32(theme.textPrimary), "N");
	drawList->AddText(ImVec2(compassPos.x + compassRadius + 4, compassPos.y - 8), ImGui::GetColorU32(theme.textPrimary), "E");
	drawList->AddText(ImVec2(compassPos.x - 4, compassPos.y + compassRadius + 4), ImGui::GetColorU32(theme.textPrimary), "S");
	drawList->AddText(ImVec2(compassPos.x - compassRadius - 10, compassPos.y - 8), ImGui::GetColorU32(theme.textPrimary), "W");

	// Draw legend
	ImVec2 legendPos = ImVec2(mapPos.x + 15, mapPos.y + mapSize - 45);

	// Legend background
	drawList->AddRectFilled(ImVec2(legendPos.x - 5, legendPos.y - 5),
	        ImVec2(legendPos.x + 145, legendPos.y + 35),
	        ImGui::GetColorU32(ImVec4(0.1f, 0.1f, 0.15f, 0.7f)),
	        5.0f // Rounded corners
	);

	// You legend item
	drawList->AddCircleFilled(ImVec2(legendPos.x + 8, legendPos.y + 8), 5.0f, ImGui::GetColorU32(theme.playerSelf));
	drawList->AddText(ImVec2(legendPos.x + 20, legendPos.y), ImGui::GetColorU32(theme.textPrimary), "You");

	// Other players legend item
	drawList->AddCircleFilled(ImVec2(legendPos.x + 8, legendPos.y + 25), 5.0f, ImGui::GetColorU32(theme.playerOthers));
	drawList->AddText(ImVec2(legendPos.x + 20, legendPos.y + 18), ImGui::GetColorU32(theme.textPrimary), "Other players");

	ImGui::EndChild();     // End PlayersPanel
	ImGui::PopStyleVar(2); // ChildRounding, WindowPadding
}

void GameClient::drawChatPanel(float width, float height)
{
	const UITheme& theme = themeManager.getCurrentTheme();

	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.childRounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));

	ImGui::BeginChild("ChatPanel", ImVec2(width, height), true);

	// Chat header with icon
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Default font
	ImGui::TextColored(theme.textAccent, ICON_LC_MESSAGE_CIRCLE " CHAT");
	ImGui::PopFont();

	// separator with gradient
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 sepMin = ImGui::GetCursorScreenPos();
	ImVec2 sepMax = ImVec2(sepMin.x + width - 40, sepMin.y + 1);
	drawList->AddRectFilledMultiColor(sepMin,
	        sepMax,
	        ImGui::GetColorU32(theme.gradientStart), // Left
	        ImGui::GetColorU32(theme.gradientEnd),   // Right
	        ImGui::GetColorU32(theme.gradientEnd),   // Right
	        ImGui::GetColorU32(theme.gradientStart)  // Left
	);
	ImGui::Dummy(ImVec2(0, 10)); // Space after separator

	// Chat messages
	float messagesHeight = height - 175; // Reserve space for input
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.frameRounding);
	ImGui::BeginChild("ChatMessages", ImVec2(width - 40, messagesHeight), true);
	{
		std::lock_guard<std::mutex> chatLock(chatMutex);

		// Variables for message grouping
		std::string currentSender = "";
		time_t groupStartTime = 0;
		bool isFirstInGroup = true;
		const int GROUP_TIME_THRESHOLD = 120; // Group messages within 2 minutes (adjust as needed)

		for (size_t i = 0; i < chatMessages.size(); i++)
		{
			const auto& msg = chatMessages[i];
			if (msg.timestamp == 0)
			{
				continue;
			}

			// Check if this message should start a new group
			bool startNewGroup = (currentSender != msg.sender) || (msg.timestamp - groupStartTime > GROUP_TIME_THRESHOLD);

			if (startNewGroup)
			{
				// Close previous group if it exists
				if (!isFirstInGroup)
				{
					ImGui::Unindent(16.0f);
					ImGui::Dummy(ImVec2(0, 4));
					ImGui::EndChild();
					ImGui::PopStyleColor();
					ImGui::PopStyleVar(2); // FramePadding, FrameRounding
				}

				// Start a new message group
				currentSender = msg.sender;
				groupStartTime = msg.timestamp;
				isFirstInGroup = false;

				// Format sender with different colors based on type
				ImVec4 senderColor;
				std::string senderIcon;
				if (msg.sender == "System" || msg.sender == "Server")
				{
					senderColor = theme.systemMessage; // Gold for system
					senderIcon = ICON_LC_CIRCLE_ALERT " ";
				}
				else if (msg.sender == "You")
				{
					senderColor = theme.accentPrimary; // Accent blue for self
					senderIcon = ICON_LC_USER " ";
				}
				else
				{
					senderColor = theme.playerOthers; // Green for others
					senderIcon = ICON_LC_USER " ";
				}

				// Message bubble style
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);

				// Set background color based on sender
				ImVec4 bubbleBgColor;
				if (msg.sender == "You")
				{
					// Your messages
					bubbleBgColor = theme.accentSecondary;
					bubbleBgColor.w = 0.5f; // Semi-transparent
				}
				else
				{
					// Others' messages
					bubbleBgColor = theme.bgTertiary;
					bubbleBgColor.w = 0.7f; // Semi-transparent
				}
				ImGui::PushStyleColor(ImGuiCol_ChildBg, bubbleBgColor);

				// Begin message bubble group with unique ID
				ImGui::BeginChild(("MsgGroup" + std::to_string(msg.timestamp)).c_str(), ImVec2(width - 80, 0), ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY);

				// Add some extra space at the top of the bubble
				ImGui::Dummy(ImVec2(0, 4));
				ImGui::Indent(16.0f);

				// Print sender name with icon and color
				ImGui::TextColored(senderColor, "%s%s:", senderIcon.c_str(), msg.sender.c_str());
			}

			if (startNewGroup)
			{
				// Format timestamp
				std::string timeStr = Utils::formatTimestamp(msg.timestamp);

				// First message in group shows timestamp at the top
				ImGui::TextColored(theme.textSecondary, "%s", timeStr.c_str());
			}

			// Render markdown content using MarkdownHelper
			MarkdownHelper::RenderMarkdown(msg.content, ImGui::GetContentRegionAvail().x, theme);
		}

		// Close the last group if there was one
		if (!isFirstInGroup)
		{
			ImGui::Unindent(16.0f);
			ImGui::Dummy(ImVec2(0, 4));
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2); // FramePadding, FrameRounding
		}

		// Auto-scroll to bottom if not manually scrolled
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20)
			ImGui::SetScrollHereY(1.0f);
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(); // ChildRounding

	ImGui::Spacing();

	// Input text flags - use EnterReturnsTrue to capture Enter key
	ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine;

	// Calculate dimensions for the multiline input
	float inputHeight = 40.0f; // Default height for multiline input
	float buttonWidth = 100;
	float buttonHeight = 40.0f;
	float availableWidth = width - 40 - buttonWidth - ImGui::GetStyle().ItemSpacing.x;

	// Create a container for input and button
	ImGui::BeginGroup();

	// Input field styling
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(15, 12));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 15.0f);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.bgInput);

	// Chat multiline input field without history callback
	ImGui::SetNextItemWidth(availableWidth);
	bool enterPressed = ImGui::InputTextMultiline("##ChatInput", chatInputBuffer, IM_ARRAYSIZE(chatInputBuffer), ImVec2(availableWidth, inputHeight), inputFlags);

	ImGui::PopStyleColor();

	ImGui::SameLine();

	// Vertically center the send button relative to the input field
	float yOffset = (inputHeight - buttonHeight) * 0.5f;
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yOffset);

	// Style for the send button
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 25.0f);
	bool sendClicked = ImGui::Button(ICON_LC_SEND " Send", ImVec2(buttonWidth, buttonHeight));
	ImGui::PopStyleVar(3); // FramePadding, FrameRounding, FrameRounding for button

	// Add a tooltip for the keyboard shortcut
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Send message (Enter)");
	}

	ImGui::EndGroup();

	// Help text for chat input
	ImGui::TextColored(theme.textSecondary, "Press Enter to send, Ctrl+Enter for new line");

	// Process chat message
	if (enterPressed || sendClicked)
	{
		processChatInput();
	}

	ImGui::EndChild();     // End ChatPanel
	ImGui::PopStyleVar(2); // ChildRounding, WindowPadding
}

void GameClient::drawControlsPanel(float width, float height)
{
	const UITheme& theme = themeManager.getCurrentTheme();

	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));

	ImGui::BeginChild("ControlsPanel", ImVec2(width, height), true);

	// Panel header with icon
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Default font
	ImGui::TextColored(theme.textAccent, ICON_LC_SETTINGS " CONTROLS");
	ImGui::PopFont();

	// separator with gradient
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 sepMin = ImGui::GetCursorScreenPos();
	ImVec2 sepMax = ImVec2(sepMin.x + width - 40, sepMin.y + 1);
	drawList->AddRectFilledMultiColor(sepMin,
	        sepMax,
	        ImGui::GetColorU32(theme.gradientStart), // Left
	        ImGui::GetColorU32(theme.gradientEnd),   // Right
	        ImGui::GetColorU32(theme.gradientEnd),   // Right
	        ImGui::GetColorU32(theme.gradientStart)  // Left
	);
	ImGui::Dummy(ImVec2(0, 10)); // Space after separator

	// Theme selection
	ImGui::TextColored(theme.textAccent, ICON_LC_PALETTE " THEME");
	ImGui::SameLine();

	// Theme selection combo box
	if (ImGui::BeginCombo("##ThemeCombo", themeManager.getCurrentTheme().name.c_str()))
	{
		for (const auto& themeName: themeManager.getThemeNames())
		{
			bool isSelected = (themeName == themeManager.getCurrentTheme().name);
			if (ImGui::Selectable(themeName.c_str(), isSelected))
			{
				themeManager.setTheme(themeName);
			}
			if (isSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	// Draw the controls UI with styling
	drawNetworkOptionsUI(width, height);

	ImGui::EndChild();     // End ControlsPanel
	ImGui::PopStyleVar(2); // ChildRounding, WindowPadding
}

void GameClient::drawNetworkOptionsUI(float width, float height)
{
	const UITheme& theme = themeManager.getCurrentTheme();

	// Network settings
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 15));

	// Section: Actions
	{
		ImGui::TextColored(theme.textAccent, ICON_LC_ACTIVITY " Actions");
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 sepMin = ImGui::GetCursorScreenPos();
		ImVec2 sepMax = ImVec2(sepMin.x + width - 40, sepMin.y + 1);
		drawList->AddRectFilledMultiColor(sepMin,
		        sepMax,
		        ImGui::GetColorU32(theme.gradientStart), // Left
		        ImGui::GetColorU32(theme.gradientEnd),   // Right
		        ImGui::GetColorU32(theme.gradientEnd),   // Right
		        ImGui::GetColorU32(theme.gradientStart)  // Left
		);
		ImGui::Dummy(ImVec2(0, 10)); // Space after separator
		ImGui::Spacing();

		// button styling
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

		// Chat Clear button
		if (ImGui::Button(ICON_LC_X " Clear Chat", ImVec2(-1, 35)))
		{
			clearChatMessages();
		}

		// Sign out button
		if (ImGui::Button(ICON_LC_LOG_OUT " Sign Out", ImVec2(-1, 35)))
		{
			signOut();
		}

		// Exit button
		if (ImGui::Button(ICON_LC_CIRCLE_X " Exit Application", ImVec2(-1, 35)))
		{
			shouldExit = true;
		}

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

	// Disable rounding for full window
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::Begin(GAME_NAME, nullptr, window_flags);
	ImGui::PopStyleVar();

	// Handle game controls when connected and not focusing UI inputs
	if (connectionState == ConnectionState::Connected && !ImGui::GetIO().WantTextInput)
	{
		ImGuiIO& io = ImGui::GetIO();

		// Get elapsed time since last frame for smooth movement
		time_t currentTime = time(0);
		float deltaTime = (currentTime - lastUpdateTime) / 1000.0f; // Convert to seconds
		lastUpdateTime = currentTime;

		// Use deltaTime for smooth movement regardless of frame rate
		// Cap to reasonable values to prevent extreme movement on lag spikes
		if (deltaTime > 0.1f)
			deltaTime = 0.1f;
		const float BASE_SPEED = 5.0f; // Units per second
		const float MOVE_SPEED = BASE_SPEED * deltaTime;

		// WASD movement on XZ plane - using ImGui's key states
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

		// Global keyboard shortcuts
		if (ImGui::IsKeyPressed(ImGuiKey_F1))
		{
			showDebug = !showDebug;
			logger.setLogLevel(LogLevel::DEBUG);
			addChatMessage("System", std::string("Debug mode ") + (showDebug ? "enabled" : "disabled"));
		}

		if (ImGui::IsKeyPressed(ImGuiKey_F3))
		{
			showStats = !showStats;
			addChatMessage("System", std::string("Network stats ") + (showStats ? "enabled" : "disabled"));
		}

		// ESC to exit
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			shouldExit = true;
		}

		// Shortcut keys for common commands
		if (ImGui::IsKeyPressed(ImGuiKey_P) && (io.KeyCtrl || io.KeyShift))
		{
			// Ctrl+P or Shift+P for player list
			processChatCommand("/players");
		}

		if (ImGui::IsKeyPressed(ImGuiKey_L) && io.KeyCtrl)
		{
			// Ctrl+L to clear chat
			processChatCommand("/clear");
		}

		if (ImGui::IsKeyPressed(ImGuiKey_R) && io.KeyCtrl)
		{
			// Ctrl+R to attempt reconnection
			processChatCommand("/reconnect");
		}

		// Start chat with "/"
		if (ImGui::IsKeyPressed(ImGuiKey_Slash))
		{
			// Focus chat input and add / character
			chatInputBuffer[0] = '/';
			chatInputBuffer[1] = '\0';
			ImGui::SetKeyboardFocusHere();
		}
	}

	// ESC in login screen should exit
	if (connectionState == ConnectionState::LoginScreen && ImGui::IsKeyPressed(ImGuiKey_Escape))
	{
		shouldExit = true;
	}

	if (showDebug && threadManager)
	{
		ImGui::Begin("Thread Statistics", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::Text("%s", getThreadStats().c_str());
		ImGui::End();
	}

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
