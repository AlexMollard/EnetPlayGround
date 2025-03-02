#include "GameClient.h"

// Constructor
GameClient::GameClient(const std::string& playerName, const std::string& password, bool debugMode)
      : myPlayerName(playerName), myPassword(password), logger(debugMode), lastNetworkActivity(getCurrentTimeMs())
{
	// Set up console
	consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (consoleHandle == INVALID_HANDLE_VALUE)
	{
		logger.logError("Failed to get console handle");
	}

	// Try to load stored credentials if none provided
	if (myPlayerName.empty() || myPassword.empty())
	{
		loadCredentials();
	}

	// Initialize update timing
	lastUpdateTime = getCurrentTimeMs();
	lastPositionUpdateTime = lastUpdateTime;
	lastConnectionCheckTime = lastUpdateTime;
}

// Destructor
GameClient::~GameClient()
{
	disconnect();
	logger.log("Client shutdown complete");
}

// Initialize the client
bool GameClient::initialize()
{
	logger.log("Initializing client...");

	// Initialize ENet
	if (enet_initialize() != 0)
	{
		logger.logError("Failed to initialize ENet");
		return false;
	}

	// Create client host
	client = enet_host_create(nullptr, // Create a client host (no bind address)
	        1,                         // Allow 1 outgoing connection
	        2,                         // Use 2 channels (reliable and unreliable)
	        0,                         // Unlimited incoming bandwidth
	        0                          // Unlimited outgoing bandwidth
	);

	if (client == nullptr)
	{
		logger.logError("Failed to create ENet client host");
		enet_deinitialize();
		return false;
	}

	logger.log("Client initialized successfully");
	return true;
}

// Connect to the server
bool GameClient::connectToServer(const char* address = DEFAULT_SERVER, uint16_t port = DEFAULT_PORT)
{
	serverAddress = address;
	serverPort = port;

	// Create ENet address
	ENetAddress enetAddress;
	enet_address_set_host(&enetAddress, address);
	enetAddress.port = port;

	logger.log("Connecting to server at " + std::string(address) + ":" + std::to_string(port) + "...");

	// Connect to the server
	server = enet_host_connect(client, &enetAddress, 2, 0);

	if (server == nullptr)
	{
		logger.logError("Failed to initiate connection to server");
		return false;
	}

	// Wait for connection establishment
	ENetEvent event;
	if (enet_host_service(client, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
	{
		isConnected = true;
		lastNetworkActivity = getCurrentTimeMs();
		logger.log("Connected to server at " + std::string(address) + ":" + std::to_string(port), true);

		// Authenticate with server
		if (!authenticate())
		{
			logger.logError("Authentication failed, disconnecting");
			disconnect();
			return false;
		}

		return true;
	}

	enet_peer_reset(server);
	logger.logError("Connection to server failed (timeout)");
	return false;
}

// Attempt to reconnect to server
bool GameClient::reconnectToServer()
{
	if (isConnected)
	{
		disconnect(false);
	}

	logger.log("Attempting to reconnect to server...", true);
	reconnecting = true;

	for (int attempt = 1; attempt <= RECONNECT_ATTEMPTS; attempt++)
	{
		logger.log("Reconnection attempt " + std::to_string(attempt) + " of " + std::to_string(RECONNECT_ATTEMPTS));

		if (connectToServer(serverAddress.c_str(), serverPort))
		{
			logger.log("Reconnection successful!", true);
			reconnecting = false;
			return true;
		}

		// Wait before next attempt
		std::this_thread::sleep_for(std::chrono::seconds(2));
	}

	logger.logError("Failed to reconnect after " + std::to_string(RECONNECT_ATTEMPTS) + " attempts");
	reconnecting = false;
	return false;
}

// Authenticate with server
bool GameClient::authenticate()
{
	if (!isConnected)
	{
		logger.logError("Cannot authenticate: not connected to server");
		return false;
	}

	// Make sure we have credentials
	if (myPlayerName.empty() || myPassword.empty())
	{
		getUserCredentials();
	}

	logger.log("Authenticating as user: " + myPlayerName);

	// Send authentication message
	std::string authMsg = "AUTH:" + myPlayerName + "," + myPassword;
	sendPacket(authMsg, true);

	// Wait for authentication response
	ENetEvent event;
	bool authResult = false;

	// Wait for up to 5 seconds for authentication response
	uint32_t startTime = getCurrentTimeMs();
	const uint32_t AUTH_TIMEOUT_MS = 5000;

	while (getCurrentTimeMs() - startTime < AUTH_TIMEOUT_MS)
	{
		while (enet_host_service(client, &event, 100) > 0)
		{
			switch (event.type)
			{
				case ENET_EVENT_TYPE_RECEIVE:
				{
					handlePacket(event.packet);

					std::string message(reinterpret_cast<const char*>(event.packet->data), event.packet->dataLength);

					logger.logNetworkEvent("Received during auth: " + message);

					// Check for authentication response or welcome message
					if (message.substr(0, 13) == "AUTH_RESPONSE:")
					{
						size_t commaPos = message.find(',', 13);
						if (commaPos != std::string::npos)
						{
							std::string result = message.substr(13, commaPos - 13);

							if (result == "success")
							{
								myPlayerId = std::stoi(message.substr(commaPos + 1));
								logger.log("Authentication successful! Player ID: " + std::to_string(myPlayerId), true);
								isAuthenticated = true;
								authResult = true;
							}
							else
							{
								logger.logError("Authentication failed: " + message.substr(commaPos + 1));
								isAuthenticated = false;
								authResult = false;
							}
						}
					}
					else if (message.substr(0, 8) == "WELCOME:")
					{
						// Legacy protocol support
						myPlayerId = std::stoi(message.substr(8));
						logger.log("Received legacy welcome. Player ID: " + std::to_string(myPlayerId), true);
						isAuthenticated = true;
						authResult = true;
					}

					enet_packet_destroy(event.packet);

					if (isAuthenticated || (!authResult && message.substr(0, 13) == "AUTH_RESPONSE:"))
					{
						return authResult;
					}
					break;
				}

				case ENET_EVENT_TYPE_DISCONNECT:
				{
					logger.logError("Server disconnected during authentication");
					isConnected = false;
					return false;
				}

				default:
					break;
			}
		}
	}

	logger.logError("Authentication timed out");
	return false;
}

// Disconnect from the server
void GameClient::disconnect(bool showMessage)
{
	if (isConnected && server != nullptr)
	{
		if (showMessage)
		{
			logger.log("Disconnecting from server...", true);
		}

		// If we're initiating the disconnect, send a proper disconnect packet
		enet_peer_disconnect(server, 0);

		// Wait for disconnect acknowledgment with timeout
		ENetEvent event;
		bool disconnected = false;
		uint32_t disconnectStartTime = getCurrentTimeMs();

		// Wait up to 3 seconds for clean disconnection
		while (getCurrentTimeMs() - disconnectStartTime < 3000 && !disconnected)
		{
			while (enet_host_service(client, &event, 100) > 0)
			{
				if (event.type == ENET_EVENT_TYPE_DISCONNECT)
				{
					logger.log("Disconnection acknowledged by server");
					disconnected = true;
					break;
				}
				else if (event.type == ENET_EVENT_TYPE_RECEIVE)
				{
					enet_packet_destroy(event.packet);
				}
			}

			if (disconnected)
				break;
		}

		// Force disconnect if not acknowledged
		if (!disconnected)
		{
			logger.log("Forcing disconnection after timeout");
			enet_peer_reset(server);
		}

		server = nullptr;
	}

	// Always set these flags regardless of clean disconnection
	isConnected = false;
	isAuthenticated = false;
	waitingForPingResponse = false;

	// Clean up ENet resources if needed
	if (client != nullptr)
	{
		enet_host_destroy(client);
		client = nullptr;
		enet_deinitialize();
	}
}

// Load saved credentials
void GameClient::loadCredentials()
{
	logger.log("Loading saved credentials...");

	std::ifstream file(CREDENTIALS_FILE, std::ios::binary);
	if (!file.is_open())
	{
		logger.log("No saved credentials found");
		return;
	}

	try
	{
		// Read username
		size_t nameLength = 0;
		file.read(reinterpret_cast<char*>(&nameLength), sizeof(nameLength));

		std::vector<char> nameBuffer(nameLength + 1, 0);
		file.read(nameBuffer.data(), nameLength);
		myPlayerName = std::string(nameBuffer.data(), nameLength);

		// Read password
		size_t passwordLength = 0;
		file.read(reinterpret_cast<char*>(&passwordLength), sizeof(passwordLength));

		std::vector<char> passwordBuffer(passwordLength + 1, 0);
		file.read(passwordBuffer.data(), passwordLength);
		myPassword = std::string(passwordBuffer.data(), passwordLength);

		logger.log("Loaded credentials for user: " + myPlayerName);
	}
	catch (const std::exception& e)
	{
		logger.logError("Failed to load credentials: " + std::string(e.what()));
		myPlayerName.clear();
		myPassword.clear();
	}

	file.close();
}

// Save credentials to file
void GameClient::saveCredentials()
{
	logger.log("Saving credentials...");

	std::ofstream file(CREDENTIALS_FILE, std::ios::binary);
	if (!file.is_open())
	{
		logger.logError("Failed to save credentials: couldn't open file");
		return;
	}

	try
	{
		// Write username
		size_t nameLength = myPlayerName.length();
		file.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
		file.write(myPlayerName.c_str(), nameLength);

		// Write password
		size_t passwordLength = myPassword.length();
		file.write(reinterpret_cast<const char*>(&passwordLength), sizeof(passwordLength));
		file.write(myPassword.c_str(), passwordLength);

		logger.log("Credentials saved successfully");
	}
	catch (const std::exception& e)
	{
		logger.logError("Failed to save credentials: " + std::string(e.what()));
	}

	file.close();
}

// Get credentials from user
void GameClient::getUserCredentials()
{
	logger.log("Please enter your credentials:", true);

	std::cout << "\nUsername: ";
	std::getline(std::cin, myPlayerName);

	std::cout << "Password: ";
	std::getline(std::cin, myPassword);

	// Save for next time
	saveCredentials();
}

// Send a packet to the server
void GameClient::sendPacket(const std::string& message, bool reliable)
{
	if (!isConnected || server == nullptr)
	{
		logger.logError("Cannot send packet: not connected to server");
		return;
	}

	ENetPacket* packet = enet_packet_create(message.c_str(), message.length(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

	if (enet_peer_send(server, 0, packet) == 0)
	{
		// Update stats
		packetsSent++;
		bytesSent += message.length();
		lastNetworkActivity = getCurrentTimeMs();

		// Log packet (but don't spam logs with position updates)
		if (message.substr(0, 9) != "POSITION:")
		{
			logger.logNetworkEvent("Sent: " + message);
		}
	}
	else
	{
		logger.logError("Failed to send packet: " + message);
		enet_packet_destroy(packet);
	}
}

// Send a position update to the server
void GameClient::sendPositionUpdate()
{
	if (!isConnected || !isAuthenticated)
		return;

	uint32_t currentTime = getCurrentTimeMs();

	// Throttle update rate - don't send updates too frequently
	if (currentTime - lastPositionUpdateTime < positionUpdateRateMs)
		return;

	// Only send if position has changed significantly
	float moveDistance = std::sqrt(pow(myPosition.x - lastSentPosition.x, 2) + pow(myPosition.y - lastSentPosition.y, 2) + pow(myPosition.z - lastSentPosition.z, 2));

	if (moveDistance < movementThreshold)
		return;

	// Prepare position update message
	std::string positionMsg;

	if (useCompressedUpdates)
	{
		// Delta compression: Send only the coordinates that changed significantly
		positionMsg = "MOVE_DELTA:";
		bool addedCoordinate = false;

		if (std::abs(myPosition.x - lastSentPosition.x) >= movementThreshold)
		{
			positionMsg += "x" + std::to_string(myPosition.x);
			addedCoordinate = true;
		}

		if (std::abs(myPosition.y - lastSentPosition.y) >= movementThreshold)
		{
			if (addedCoordinate)
				positionMsg += ",";
			positionMsg += "y" + std::to_string(myPosition.y);
			addedCoordinate = true;
		}

		if (std::abs(myPosition.z - lastSentPosition.z) >= movementThreshold)
		{
			if (addedCoordinate)
				positionMsg += ",";
			positionMsg += "z" + std::to_string(myPosition.z);
		}
	}
	else
	{
		// Full position update (fallback method)
		positionMsg = "POSITION:" + std::to_string(myPosition.x) + "," + std::to_string(myPosition.y) + "," + std::to_string(myPosition.z);
	}

	// Use unreliable packets for position updates
	sendPacket(positionMsg, false);
	lastSentPosition = myPosition;
	lastPositionUpdateTime = currentTime;
}

// Send ping to server to check connection
void GameClient::sendPing()
{
	if (!isConnected)
		return;

	uint32_t currentTime = getCurrentTimeMs();

	// Send ping at regular intervals
	if (currentTime - lastPingTime >= PING_INTERVAL_MS)
	{
		lastPingTime = currentTime;
		lastPingSentTime = currentTime;
		waitingForPingResponse = true;
		std::string pingMsg = "PING:" + std::to_string(currentTime);
		sendPacket(pingMsg);
		logger.log("Sent ping at timestamp " + std::to_string(currentTime));
	}

	// Check for connection timeout (no ping response)
	if (waitingForPingResponse && currentTime - lastPingSentTime > serverResponseTimeout)
	{
		logger.logError("Ping response timeout - Server may have disconnected");
		handleServerDisconnection();
	}
}

void GameClient::checkConnectionHealth()
{
	if (!isConnected || reconnecting)
		return;

	uint32_t currentTime = getCurrentTimeMs();

	// Check if we've received any responses from server recently
	if (currentTime - lastNetworkActivity > serverResponseTimeout)
	{
		logger.logError("No server activity for " + std::to_string(serverResponseTimeout / 1000) + " seconds");
		handleServerDisconnection();
	}
}

void GameClient::handleServerDisconnection()
{
	isConnected = false;
	isAuthenticated = false;

	logger.logError("Detected server disconnection");
	addChatMessage("System", "Lost connection to server");

	// Try to reconnect if not exiting
	if (!shouldExit && !reconnecting)
	{
		addChatMessage("System", "Attempting to reconnect...");
		reconnectToServer();
	}
}

// Send a chat message
void GameClient::sendChatMessage(const std::string& message)
{
	if (!isConnected || !isAuthenticated || message.empty())
		return;

	std::string chatMsg = "CHAT:" + message;
	sendPacket(chatMsg);

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
			logger.setDebugMode(showDebug);
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
			reconnectToServer();
			return;
		}

		// Send all other commands to the server
		if (isConnected && isAuthenticated)
		{
			std::string commandMsg = "COMMAND:" + command.substr(1); // Remove the leading '/'
			sendPacket(commandMsg, true);
			logger.log("Sent command to server: " + commandMsg);
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
	// Update connection monitoring
	lastServerResponseTime = getCurrentTimeMs();
	lastNetworkActivity = lastServerResponseTime;

	// If we received any packet, we know the server is still responding
	if (waitingForPingResponse)
	{
		waitingForPingResponse = false;
	}

	// Update stats
	packetsReceived++;
	bytesReceived += packet->dataLength;
	lastNetworkActivity = getCurrentTimeMs();

	std::string message(reinterpret_cast<const char*>(packet->data), packet->dataLength);

	// Don't log position updates to avoid spam
	if (message.substr(0, 12) != "WORLD_STATE:" && message.substr(0, 5) != "PONG:")
	{
		logger.logNetworkEvent("Received: " + message);
	}

	// Handle different message types
	if (message.substr(0, 13) == "AUTH_RESPONSE:")
	{
		// Already handled in authenticate method
		return;
	}
	else if (message.substr(0, 8) == "WELCOME:")
	{
		// Legacy protocol - handle welcome message
		myPlayerId = std::stoi(message.substr(8));
		logger.log("Received player ID: " + std::to_string(myPlayerId));
		isAuthenticated = true;
	}
	else if (message.substr(0, 12) == "WORLD_STATE:")
	{
		// Handle world state update
		parseWorldState(message.substr(12));
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
		// Calculate ping time
		try
		{
			uint32_t sentTime = std::stoul(message.substr(5));
			pingMs = getCurrentTimeMs() - sentTime;
			waitingForPingResponse = false; // Mark that we got a response
		}
		catch (const std::exception& e)
		{
			logger.logError("Failed to parse PONG message: " + std::string(e.what()));
		}
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
			logger.logError("Failed to parse TELEPORT message: " + std::string(e.what()));
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
	std::lock_guard<std::mutex> lock(playersMutex);

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
			logger.logError("Error parsing player data: " + std::string(e.what()) + ", Data: " + playerData);
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
	std::lock_guard<std::mutex> lock(chatMutex);

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
	std::lock_guard<std::mutex> lock(chatMutex);
	chatMessages.clear();
	addChatMessage("System", "Chat history cleared");
}

void GameClient::handleImGuiInput()
{
	// Get ImGui IO to check key states
	ImGuiIO& io = ImGui::GetIO();

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
		logger.setDebugMode(showDebug);
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

	// Only process movement keys when not in chat mode
	if (!chatFocused && !io.WantTextInput)
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

	// Handle command history navigation in chat mode
	// This is handled separately in the chat input section of drawUI()
}

// This method is updated in the implementation above

// Process a single network update cycle
void GameClient::updateNetwork()
{
	if (!isConnected || client == nullptr)
	{
		return;
	}

	// Process any pending events
	ENetEvent event;
	while (enet_host_service(client, &event, 0) > 0)
	{
		switch (event.type)
		{
			case ENET_EVENT_TYPE_RECEIVE:
				handlePacket(event.packet);
				enet_packet_destroy(event.packet);
				break;

			case ENET_EVENT_TYPE_DISCONNECT:
				logger.logError("Disconnected from server");
				isConnected = false;

				// Try to reconnect if not exiting
				if (!shouldExit && !reconnecting)
				{
					reconnectToServer();
				}
				break;

			default:
				break;
		}
	}

	uint32_t currentTime = getCurrentTimeMs();

	// Update timing flags
	bool shouldUpdatePosition = (currentTime - lastPositionUpdateTime >= MOVEMENT_UPDATE_RATE_MS);
	bool shouldCheckConnection = (currentTime - lastConnectionCheckTime >= connectionCheckInterval);

	// Send position updates if it's time
	if (shouldUpdatePosition && isAuthenticated)
	{
		sendPositionUpdate();
		lastPositionUpdateTime = currentTime;
	}

	// Send ping and check connection if it's time
	if (shouldCheckConnection)
	{
		sendPing();
		checkConnectionHealth();
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

// Draw the UI with ImGui
void GameClient::drawUI()
{
	// Use the full window with no padding and no window decorations
	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);

	ImGui::Begin("MMO Client", nullptr, window_flags);

	// Set up colors and styles for a more modern look
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.19f, 0.19f, 1.00f));        // Light background color
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.24f, 0.24f, 1.00f)); // Very light background
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.93f, 0.90f, 0.85f, 1.00f));  // Primary color (#EEE5DA)

	// Create a nice header section with better styling
	ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Use default font but could be larger font if available
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));

	// Header section with status
	{
		ImGui::BeginChild("HeaderSection", ImVec2(ImGui::GetContentRegionAvail().x, 60), true, ImGuiWindowFlags_NoScrollbar);

		// Left side - title and version
		ImGui::Text("MMO Client v%s", CLIENT_VERSION);

		// Right side - connection status
		float statusWidth = 120;
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - statusWidth);

		ImGui::PushStyleColor(ImGuiCol_Text, isConnected ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
		ImGui::TextUnformatted(isConnected ? ":D CONNECTED" : "X DISCONNECTED");
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
		ImGui::BeginChild("PlayersPanel", ImVec2(ImGui::GetColumnWidth(), ImGui::GetContentRegionAvail().y - 30), true);

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.90f, 0.85f, 1.00f)); // Primary color (#EEE5DA)
		ImGui::TextUnformatted("PLAYERS ONLINE");
		ImGui::PopStyleColor();

		ImGui::Separator();

		// Network stats display if enabled
		if (showStats)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.78f, 0.74f, 1.00f)); // Muted primary color
			ImGui::TextWrapped("Ping: %ums | Packets: %u/%u | Data: %u/%u bytes", pingMs, packetsSent, packetsReceived, bytesSent, bytesReceived);
			ImGui::PopStyleColor();
			ImGui::Separator();
		}

		// Display other players with better formatting
		{
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

		// Chat input with send button - improved version with focus handling
		if (chatFocused && !ImGui::IsAnyItemActive())
		{
			ImGui::SetKeyboardFocusHere();
		}

		// Create flags for input text
		ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;

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

		float availableWidth = ImGui::GetContentRegionAvail().x - 60 - ImGui::GetStyle().ItemSpacing.x;

		// Begin a group to manage the layout
		ImGui::BeginGroup();

		// Apply your style if focused
		if (chatFocused)
		{
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.24f, 0.24f, 1.00f));
		}

		// Use the calculated width for the input text
		ImGui::SetNextItemWidth(availableWidth);
		bool enterPressed = ImGui::InputText("##ChatInput", chatInputBuffer, IM_ARRAYSIZE(chatInputBuffer), inputFlags);

		if (chatFocused)
		{
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		bool sendClicked = ImGui::Button("Send", ImVec2(60, 0));

		// End the group
		ImGui::EndGroup();

		if (enterPressed || sendClicked)
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

	// Right panel - Help & Controls
	{
		ImGui::BeginChild("HelpPanel", ImVec2(ImGui::GetColumnWidth(), ImGui::GetContentRegionAvail().y - 30), true);
		drawNetworkOptionsUI();

		if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Columns(2, "ControlsColumns", false);
			ImGui::TextColored(ImVec4(0.93f, 0.90f, 0.85f, 1.00f), "WASD"); // Primary color (#EEE5DA)
			ImGui::NextColumn();
			ImGui::TextWrapped("Move horizontally");
			ImGui::NextColumn();

			ImGui::TextColored(ImVec4(0.93f, 0.90f, 0.85f, 1.00f), "QE");
			ImGui::NextColumn();
			ImGui::TextWrapped("Move vertically");
			ImGui::NextColumn();

			ImGui::TextColored(ImVec4(0.93f, 0.90f, 0.85f, 1.00f), "T");
			ImGui::NextColumn();
			ImGui::TextWrapped("Chat mode");
			ImGui::NextColumn();

			ImGui::TextColored(ImVec4(0.93f, 0.90f, 0.85f, 1.00f), "ESC");
			ImGui::NextColumn();
			ImGui::TextWrapped("Exit");
			ImGui::NextColumn();

			ImGui::TextColored(ImVec4(0.93f, 0.90f, 0.85f, 1.00f), "F1");
			ImGui::NextColumn();
			ImGui::TextWrapped("Toggle debug");
			ImGui::NextColumn();

			ImGui::TextColored(ImVec4(0.93f, 0.90f, 0.85f, 1.00f), "H");
			ImGui::NextColumn();
			ImGui::TextWrapped("Toggle help");
			ImGui::NextColumn();

			ImGui::TextColored(ImVec4(0.93f, 0.90f, 0.85f, 1.00f), "F3");
			ImGui::NextColumn();
			ImGui::TextWrapped("Toggle network stats");
			ImGui::NextColumn();

			ImGui::Columns(1);
		}

		if (ImGui::CollapsingHeader("Client Commands"))
		{
			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/help");
			ImGui::SameLine();
			ImGui::TextWrapped("Toggle help screen");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/clear");
			ImGui::SameLine();
			ImGui::TextWrapped("Clear chat history");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/debug");
			ImGui::SameLine();
			ImGui::TextWrapped("Toggle debug mode");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/stats");
			ImGui::SameLine();
			ImGui::TextWrapped("Toggle network stats");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/reconnect");
			ImGui::SameLine();
			ImGui::TextWrapped("Reconnect to server");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/quit");
			ImGui::SameLine();
			ImGui::TextWrapped("Exit client");
		}

		if (ImGui::CollapsingHeader("Server Commands"))
		{
			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/pos");
			ImGui::SameLine();
			ImGui::TextWrapped("Show current position");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/tp x y z");
			ImGui::SameLine();
			ImGui::TextWrapped("Teleport to coordinates");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/players");
			ImGui::SameLine();
			ImGui::TextWrapped("List online players");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/me action");
			ImGui::SameLine();
			ImGui::TextWrapped("Send an action message");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/w username message");
			ImGui::SameLine();
			ImGui::TextWrapped("Send private message");
		}

		if (ImGui::CollapsingHeader("Admin Commands"))
		{
			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/kick username");
			ImGui::SameLine();
			ImGui::TextWrapped("Kick a player");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/ban username");
			ImGui::SameLine();
			ImGui::TextWrapped("Ban a player");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/broadcast message");
			ImGui::SameLine();
			ImGui::TextWrapped("Send message to all");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/setadmin username");
			ImGui::SameLine();
			ImGui::TextWrapped("Grant admin status");

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "/tpplayer username");
			ImGui::SameLine();
			ImGui::TextWrapped("Teleport to player");
		}

		ImGui::EndChild();
	}
	ImGui::Columns(1);

	// Bottom status bar
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.20f, 0.19f, 0.19f, 1.00f)); // Light background
	ImGui::BeginChild("StatusBar", ImVec2(ImGui::GetContentRegionAvail().x, 25), true, ImGuiWindowFlags_NoScrollbar);

	// Adjust vertical position to center text
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5);
	ImGui::Text("Ping: %ums | Packets: %u/%u | Data: %u/%u bytes", pingMs, packetsSent, packetsReceived, bytesSent, bytesReceived);
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 150);
	ImGui::Text("Players Online: %zu", otherPlayers.size() + 1); // +1 for self

	ImGui::EndChild();
	ImGui::PopStyleColor(); // ChildBg

	ImGui::PopStyleColor(3); // Header colors
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
