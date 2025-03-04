#include "Server.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#	include <conio.h>
#endif

#include "Utils.h"

// Constructor
GameServer::GameServer()
{
	// Set up logger
	logger.info("Initializing server...");

	// Load configuration
	loadConfig();

	// Initialize command handlers
	initializeCommandHandlers();

	// Load saved player data
	loadAuthData();

	// Initialize plugin system
	initializePluginSystem();
}

// Destructor
GameServer::~GameServer()
{
	shutdown();
}

// Initialize server
bool GameServer::initialize()
{
	logger.info("Initializing ENet...");

	if (enet_initialize() != 0)
	{
		logger.error("Failed to initialize ENet");
		return false;
	}

	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = config.port;

	logger.info("Creating server host on port " + std::to_string(config.port));

	server = enet_host_create(&address,
	        config.maxPlayers,
	        4, // Number of channels
	        0, // Unlimited incoming bandwidth
	        0  // Unlimited outgoing bandwidth
	);

	if (server == nullptr)
	{
		logger.error("Failed to create ENet server host");
		enet_deinitialize();
		return false;
	}

	logger.info("Server initialized successfully on port " + std::to_string(config.port));
	return true;
}

void GameServer::initializePluginSystem()
{
	// Initialize plugin manager
	pluginManager = std::make_unique<PluginManager>(this);

	// Add plugin command handlers
	initializePluginCommandHandlers();

	// Load plugins from plugins directory
	if (!std::filesystem::exists("./plugins"))
	{
		std::filesystem::create_directories("./plugins");
	}

	loadPluginsFromDirectory("./plugins");

	logger.info("Plugin system initialized");
}

void GameServer::initializePluginCommandHandlers()
{
	// Plugin commands
	commandHandlers["plugins"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			sendSystemMessage(player, "You don't have permission to use this command");
			return;
		}

		auto plugins = getLoadedPlugins();
		if (plugins.empty())
		{
			sendSystemMessage(player, "No plugins loaded");
			return;
		}

		sendSystemMessage(player, "Loaded plugins (" + std::to_string(plugins.size()) + "):");
		for (const auto& name: plugins)
		{
			IPlugin* plugin = pluginManager->getPlugin(name);
			if (plugin)
			{
				sendSystemMessage(player, "- " + name + " v" + plugin->getVersion() + " by " + plugin->getAuthor());
			}
			else
			{
				sendSystemMessage(player, "- " + name);
			}
		}
	};

	commandHandlers["loadplugin"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			sendSystemMessage(player, "You don't have permission to use this command");
			return;
		}

		if (args.size() < 2)
		{
			sendSystemMessage(player, "Usage: /loadplugin <path>");
			return;
		}

		std::string path = args[1];
		if (loadPlugin(path))
		{
			sendSystemMessage(player, "Plugin loaded successfully");
		}
		else
		{
			sendSystemMessage(player, "Failed to load plugin");
		}
	};

	commandHandlers["unloadplugin"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			sendSystemMessage(player, "You don't have permission to use this command");
			return;
		}

		if (args.size() < 2)
		{
			sendSystemMessage(player, "Usage: /unloadplugin <name>");
			return;
		}

		std::string name = args[1];
		if (unloadPlugin(name))
		{
			sendSystemMessage(player, "Plugin unloaded successfully");
		}
		else
		{
			sendSystemMessage(player, "Failed to unload plugin");
		}
	};

	commandHandlers["reloadplugin"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			sendSystemMessage(player, "You don't have permission to use this command");
			return;
		}

		if (args.size() < 2)
		{
			sendSystemMessage(player, "Usage: /reloadplugin <name>");
			return;
		}

		std::string name = args[1];
		if (reloadPlugin(name))
		{
			sendSystemMessage(player, "Plugin reloaded successfully");
		}
		else
		{
			sendSystemMessage(player, "Failed to reload plugin");
		}
	};

	commandHandlers["reloadallplugins"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			sendSystemMessage(player, "You don't have permission to use this command");
			return;
		}

		auto plugins = getLoadedPlugins();
		if (plugins.empty())
		{
			sendSystemMessage(player, "No plugins loaded");
			return;
		}

		int successCount = 0;
		for (const auto& name: plugins)
		{
			if (reloadPlugin(name))
			{
				successCount++;
			}
		}

		sendSystemMessage(player, "Reloaded " + std::to_string(successCount) + " of " + std::to_string(plugins.size()) + " plugins");
	};
}

// Plugin system methods
bool GameServer::loadPlugin(const std::string& path)
{
	return pluginManager->loadPlugin(path);
}

bool GameServer::unloadPlugin(const std::string& name)
{
	return pluginManager->unloadPlugin(name);
}

bool GameServer::reloadPlugin(const std::string& name)
{
	return pluginManager->reloadPlugin(name);
}

bool GameServer::loadPluginsFromDirectory(const std::string& directory)
{
	return pluginManager->loadPluginsFromDirectory(directory);
}

std::vector<std::string> GameServer::getLoadedPlugins() const
{
	return pluginManager->getLoadedPlugins();
}

// Start server
void GameServer::start()
{
	if (isRunning)
	{
		logger.error("Server is already running");
		return;
	}

	if (server == nullptr && !initialize())
	{
		logger.error("Failed to initialize server");
		return;
	}

	isRunning = true;
	logger.info("Server starting...");

	// Start network thread
	networkThread = std::thread(&GameServer::networkThreadFunc, this);

	// Start update thread
	updateThread = std::thread(&GameServer::updateThreadFunc, this);

	// Start save thread
	saveThread = std::thread(&GameServer::saveThreadFunc, this);

	logger.info("Server started successfully");

	// Broadcast system message
	broadcastSystemMessage("Server started. Welcome to the game!");
}

// Shutdown server
void GameServer::shutdown()
{
	if (!isRunning)
	{
		return;
	}

	logger.info("Shutting down server...");

	// Signal threads to stop
	isRunning = false;

	// Unload plugins explicitly before shutting down threads
	if (pluginManager)
	{
		auto plugins = getLoadedPlugins();
		for (const auto& name: plugins)
		{
			unloadPlugin(name);
		}
	}

	// Wait for threads to finish
	if (networkThread.joinable())
		networkThread.join();
	if (updateThread.joinable())
		updateThread.join();
	if (saveThread.joinable())
		saveThread.join();

	// Save player data
	saveAuthData();

	// Clean up ENet
	if (server != nullptr)
	{
		enet_host_destroy(server);
		server = nullptr;
		enet_deinitialize();
	}

	logger.info("Server shutdown complete");
}

// Run server (blocks until shutdown)
void GameServer::run()
{
	start();

	logger.info("Server started successfully. Type 'help' for available commands.");

	// Main thread handles console commands
	std::string command;

	while (isRunning)
	{
		// Show prompt
		std::cout << "> " << std::flush;

#ifdef _WIN32
		// Windows-specific character-by-character input handling
		std::string input;
		Logger::setConsoleInputLine(input); // Initialize with empty string

		bool inputComplete = false;
		while (isRunning && !inputComplete)
		{
			if (_kbhit()) // Check if key was pressed
			{
				char c = _getch(); // Get the character

				// Process Enter key
				if (c == '\r')
				{
					std::cout << std::endl;
					inputComplete = true;
				}
				// Process backspace
				else if (c == '\b')
				{
					if (!input.empty())
					{
						input.pop_back();
						std::cout << "\b \b" << std::flush; // Erase last character
						Logger::setConsoleInputLine(input);
					}
				}
				// Process Ctrl+C
				else if (c == 3)
				{
					isRunning = false;
					break;
				}
				// Process regular printable characters
				else if (c >= 32 && c <= 126)
				{
					input.push_back(c);
					std::cout << c << std::flush;
					Logger::setConsoleInputLine(input);
				}
			}
			else
			{
				// Sleep briefly to reduce CPU usage
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}

		command = input;
#else
		// Non-Windows platforms - simpler but not as robust
		std::getline(std::cin, command);
#endif

		// Clear the console input protection
		Logger::clearConsoleInputLine();

		// Process command
		if (command == "quit" || command == "exit")
		{
			break;
		}
		else if (command == "status")
		{
			printServerStatus();
		}
		else if (command == "players")
		{
			printPlayerList();
		}
		else if (command == "help")
		{
			printConsoleHelp();
		}
		else if (command.substr(0, 9) == "broadcast ")
		{
			std::string message = command.substr(9);
			broadcastSystemMessage(message);
			logger.info("Broadcast message sent: " + message);
		}
		else if (command.substr(0, 5) == "kick ")
		{
			std::string playerName = command.substr(5);
			kickPlayer(playerName);
		}
		else if (command.substr(0, 4) == "ban ")
		{
			std::string playerName = command.substr(4);
			banPlayer(playerName);
		}
		else if (command == "save")
		{
			saveAuthData();
			logger.info("Player data saved manually");
		}
		else if (command.substr(0, 10) == "set_admin ")
		{
			std::string playerName = command.substr(10);
			setPlayerAdmin(playerName, true);
		}
		else if (command.substr(0, 12) == "remove_admin ")
		{
			std::string playerName = command.substr(12);
			setPlayerAdmin(playerName, false);
		}
		else if (command == "reload")
		{
			loadConfig();
			logger.info("Configuration reloaded");
		}
		else if (command.substr(0, 9) == "loglevel ")
		{
			try
			{
				int level = std::stoi(command.substr(9));
				logger.setLogLevel((LogLevel) level);
			}
			catch (const std::exception&)
			{
				logger.error("Invalid log level: " + command.substr(9));
			}
		}
		else if (command.substr(0, 7) == "plugins")
		{
			for (auto& plugin: pluginManager->getLoadedPlugins())
			{
				logger.info(plugin);
			}
		}
		else if (!command.empty())
		{
			logger.info("Unknown command: " + command);
			logger.info("Type 'help' for available commands");
		}
	}

	shutdown();
}

// Network thread function
void GameServer::networkThreadFunc()
{
	logger.info("Network thread started");

	ENetEvent event;

	while (isRunning)
	{
		while (enet_host_service(server, &event, 10) > 0)
		{
			switch (event.type)
			{
				case ENET_EVENT_TYPE_CONNECT:
				{
					handleClientConnect(event);
					break;
				}

				case ENET_EVENT_TYPE_RECEIVE:
				{
					handleClientMessage(event);

					// Clean up packet
					enet_packet_destroy(event.packet);
					break;
				}

				case ENET_EVENT_TYPE_DISCONNECT:
				{
					handleClientDisconnect(event);
					break;
				}

				default:
					break;
			}
		}

		// Small sleep to reduce CPU usage
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	logger.info("Network thread stopped");
}

// Update thread function
void GameServer::updateThreadFunc()
{
	logger.info("Update thread started");

	while (isRunning)
	{
		// Get current time
		uint32_t currentTime = Utils::getCurrentTimeMs();

		// Check for plugin updates periodically
		if (currentTime - lastPluginCheckTime > pluginCheckIntervalMs)
		{
			pluginManager->checkForPluginUpdates();
			lastPluginCheckTime = currentTime;
		}

		// Synchronize player stats
		syncPlayerStats();

		// Dispatch server tick to plugins
		pluginManager->dispatchServerTick();

		// Broadcast world state
		broadcastWorldState();

		// Check for timeouts
		checkTimeouts();

		// Sleep for broadcast interval
		std::this_thread::sleep_for(std::chrono::milliseconds(config.broadcastRateMs));
	}

	logger.info("Update thread stopped");
}

// Save thread function
void GameServer::saveThreadFunc()
{
	logger.info("Save thread started");

	while (isRunning)
	{
		// Save player data periodically
		saveAuthData();
		logger.debug("Player data auto-saved");

		// Sleep for save interval
		std::this_thread::sleep_for(std::chrono::milliseconds(config.saveIntervalMs));
	}

	logger.info("Save thread stopped");
}

// Handle client connection
void GameServer::handleClientConnect(const ENetEvent& event)
{
	stats.totalConnections++;

	std::string ipAddress = Utils::peerAddressToString(event.peer->address);
	logger.info("New client connected from " + ipAddress);

	// Create temporary player entry
	uint32_t tempId = 0; // Use 0 for unauthenticated players
	std::string defaultName = "Guest" + std::to_string(Utils::getCurrentTimeMs() % 10000);

	std::scoped_lock<std::mutex> lock(playersMutex);

	Player newPlayer;
	newPlayer.id = tempId;
	newPlayer.name = defaultName;
	newPlayer.position = config.spawnPosition;
	newPlayer.lastValidPosition = config.spawnPosition;
	newPlayer.peer = event.peer;
	newPlayer.lastUpdateTime = Utils::getCurrentTimeMs();
	newPlayer.lastPingTime = Utils::getCurrentTimeMs();
	newPlayer.connectionStartTime = Utils::getCurrentTimeMs();
	newPlayer.lastPositionUpdateTime = Utils::getCurrentTimeMs();
	newPlayer.failedAuthAttempts = 0;
	newPlayer.totalBytesReceived = 0;
	newPlayer.totalBytesSent = 0;
	newPlayer.pingMs = 0;
	newPlayer.isAuthenticated = false;
	newPlayer.isAdmin = false;
	newPlayer.ipAddress = ipAddress;

	// Store player pointer in peer data
	event.peer->data = reinterpret_cast<void*>(new uint32_t(tempId));

	// Add to players map (using peer pointer as key for unauthenticated players)
	players[reinterpret_cast<uintptr_t>(event.peer)] = newPlayer;

	// Send plugin event
	pluginManager->dispatchPlayerConnect(newPlayer);

	// Send welcome message
	std::string welcomeMsg = "WELCOME:" + std::to_string(tempId);
	sendPacket(event.peer, welcomeMsg, true);

	logger.info("Temporary player created: " + defaultName);

	// Update concurrent player count
	if (players.size() > stats.maxConcurrentPlayers)
	{
		stats.maxConcurrentPlayers = players.size();
	}
}

// Handle client message
void GameServer::handleClientMessage(const ENetEvent& event)
{
	// Update stats
	stats.totalPacketsReceived++;
	stats.totalBytesReceived += event.packet->dataLength;

	// Get player ID from peer data
	uint32_t* ptrPlayerId = reinterpret_cast<uint32_t*>(event.peer->data);
	if (ptrPlayerId == nullptr)
	{
		logger.error("Received message from peer with no ID data");
		return;
	}

	uint32_t playerId = *ptrPlayerId;

	// Find player
	Player* player = nullptr;
	{
		std::scoped_lock<std::mutex> lock(playersMutex);

		if (playerId == 0)
		{
			// Unauthenticated player, use peer pointer as key
			auto it = players.find(reinterpret_cast<uintptr_t>(event.peer));
			if (it != players.end())
			{
				player = &it->second;
			}
		}
		else
		{
			auto it = players.find(playerId);
			if (it != players.end())
			{
				player = &it->second;
			}
		}
	}

	if (player == nullptr)
	{
		logger.error("Received message from unknown player. ID: " + std::to_string(playerId));
		return;
	}

	// Update player's last activity time
	player->lastUpdateTime = Utils::getCurrentTimeMs();
	player->totalBytesReceived += event.packet->dataLength;

	// Parse message
	std::string message(reinterpret_cast<char*>(event.packet->data), event.packet->dataLength);
	logger.logNetworkEvent("Message from " + player->name + ": " + message);

	// Debug log (skip position updates to avoid spam)
	//if (message.substr(0, 9) != "POSITION:" && message.substr(0, 5) != "PING:") {
	//    logger.debug("Message from " + player->name + ": " + message);
	//}

	// Send of plugin event, This will hopefully fully replace all the code below in the future
	pluginManager->dispatchPlayerMessage(*player, message);

	// Handle different message types
	if (message.substr(0, 5) == "AUTH:")
	{
		handleAuthMessage(message.substr(5), event.peer);
	}
	else if (message.substr(0, 11) == "MOVE_DELTA:" && player->isAuthenticated)
	{
		handleDeltaPositionUpdate(*player, message.substr(11));
	}
	else if (message.substr(0, 9) == "POSITION:" && player->isAuthenticated)
	{
		handleSendPosition(*player);
	}
	else if (message.substr(0, 5) == "CHAT:" && player->isAuthenticated)
	{
		handleChatMessage(*player, message.substr(5));
	}
	else if (message.substr(0, 5) == "PING:")
	{
		handlePingMessage(*player, message.substr(5));
	}
	else if (message.substr(0, 8) == "COMMAND:" && player->isAuthenticated)
	{
		handleCommandMessage(*player, message.substr(8));
	}
	else if (!player->isAuthenticated && message.substr(0, 8) == "COMMAND:")
	{
		// Special handling for login command from unauthenticated player
		std::vector<std::string> parts = Utils::splitString(message, ' ');
		if (parts.empty())
			return;

		if (parts[0].substr(0, 13) == "COMMAND:login")
		{
			if (parts.size() >= 3)
			{
				std::string username = parts[1];
				std::string password = parts[2];
				// Pass the peer pointer instead of player reference
				handleAuthMessage(username + "," + password, player->peer);
			}
			else
			{
				sendSystemMessage(*player, "Usage: /login username password");
			}
		}
		else if (parts[0].substr(0, 16) == "COMMAND:register")
		{
			if (parts.size() >= 3)
			{
				std::string username = parts[1];
				std::string password = parts[2];
				handleRegistration(*player, username, password);
			}
			else
			{
				sendSystemMessage(*player, "Usage: /register username password");
			}
		}
		else
		{
			sendSystemMessage(*player, "You must log in first. Use /login username password");
		}
	}
}

// Handle client disconnect
void GameServer::handleClientDisconnect(const ENetEvent& event)
{
	// Get player ID from peer data with proper locking
	uint32_t playerId = 0;
	{
		std::scoped_lock<std::mutex> peerDataLock(peerDataMutex);
		uint32_t* ptrPlayerId = reinterpret_cast<uint32_t*>(event.peer->data);
		if (ptrPlayerId != nullptr)
		{
			playerId = *ptrPlayerId;
		}
	}

	// Clean up stats
	{
		std::scoped_lock<std::mutex> statsLock(peerStatsMutex);
		peerStats.erase(reinterpret_cast<uintptr_t>(event.peer));
	}

	// Handle cleanup based on authentication status
	std::string playerName;
	{
		std::scoped_lock<std::mutex> lock(playersMutex);

		if (playerId == 0)
		{
			// Unauthenticated player, use peer pointer as key
			auto it = players.find(reinterpret_cast<uintptr_t>(event.peer));
			if (it != players.end())
			{
				pluginManager->dispatchPlayerDisconnect(it->second);
				playerName = it->second.name;
				players.erase(it);
			}
		}
		else
		{
			// Authenticated player
			auto it = players.find(playerId);
			if (it != players.end())
			{
				pluginManager->dispatchPlayerDisconnect(it->second);
				playerName = it->second.name;
				Position lastPos = it->second.position;

				// Pass the data we already have
				savePlayerData(playerName, lastPos);

				// Remove from spatial grid
				spatialGrid.removeEntity(playerId, it->second.position);

				// Remove from players map
				players.erase(it);
			}
		}
	}

	// Clean up peer data ONCE, at the end
	{
		std::scoped_lock<std::mutex> peerDataLock(peerDataMutex);
		uint32_t* ptrPlayerId = reinterpret_cast<uint32_t*>(event.peer->data);
		delete ptrPlayerId;
		event.peer->data = nullptr;
	}

	logger.info("Player disconnected: " + playerName + " (ID: " + std::to_string(playerId) + ")");

	// Broadcast player disconnect if it was an authenticated player
	if (playerId != 0)
	{
		broadcastSystemMessage(playerName + " has left the game");
	}
}

// Handle authentication message
void GameServer::handleAuthMessage(const std::string& authDataStr, ENetPeer* peer)
{
	// We accept a peer pointer instead of a player reference
	if (peer == nullptr)
	{
		logger.error("Auth attempt with null peer pointer");
		return;
	}

	// Find player by peer pointer before we do anything else
	uint32_t playerId = 0;
	Player* playerPtr = nullptr;
	uintptr_t peerKey = reinterpret_cast<uintptr_t>(peer);

	{
		std::scoped_lock<std::mutex> lock(playersMutex);
		auto it = players.find(peerKey);
		if (it == players.end())
		{
			logger.error("Auth attempt from unknown peer");
			return;
		}
		playerPtr = &it->second;
	}

	// Now use a local copy of the player instead of a reference
	Player player = *playerPtr;

	auto parts = Utils::splitString(authDataStr, ',');
	if (parts.size() < 2)
	{
		sendSystemMessage(player, "Invalid authentication format");
		logger.error("Invalid auth format from " + player.ipAddress);
		return;
	}

	std::string username = parts[0];
	std::string password = parts[1];

	// Check if too many failed attempts
	if (player.failedAuthAttempts >= MAX_PASSWORD_ATTEMPTS)
	{
		sendAuthResponse(peer, false, "Too many failed attempts. Please reconnect.");
		logger.error("Too many auth attempts from " + player.ipAddress);

		// Disconnect the player after a delay
		std::thread(
		        [this, peerCopy = peer]()
		        {
			        std::this_thread::sleep_for(std::chrono::seconds(1));
			        enet_peer_disconnect(peerCopy, 0);
		        })
		        .detach();

		return;
	}

	// Reset variables for authentication check
	bool success = false;
	std::string response;
	AuthData authData;

	// Authenticate player
	{
		std::scoped_lock<std::mutex> lock(authMutex);

		auto it = authenticatedPlayers.find(username);
		if (it != authenticatedPlayers.end())
		{
			// Verify password
			std::string passHash = Utils::hashPassword(password);
			if (it->second.passwordHash == passHash)
			{
				// Authentication successful
				playerId = it->second.playerId;
				authData = it->second; // Copy the auth data
				success = true;
				response = "Authentication successful";

				// Update player data
				it->second.lastLoginTime = std::time(nullptr);
				it->second.lastIpAddress = player.ipAddress;
				it->second.loginCount++;

				logger.info("Player authenticated: " + username + " (ID: " + std::to_string(playerId) + ")");
			}
			else
			{
				// Wrong password
				success = false;
				response = "Invalid password";

				// Update the failed attempts in the stored player
				std::scoped_lock<std::mutex> playerLock(playersMutex);
				auto playerIt = players.find(peerKey);
				if (playerIt != players.end())
				{
					playerIt->second.failedAuthAttempts++;
				}

				stats.authFailures++;
				logger.error("Authentication failed for " + username + ": Invalid password");
			}
		}
		else
		{
			// User not found
			success = false;
			response = "User not found. Use /register to create an account.";

			// Update the failed attempts in the stored player
			std::scoped_lock<std::mutex> playerLock(playersMutex);
			auto playerIt = players.find(peerKey);
			if (playerIt != players.end())
			{
				playerIt->second.failedAuthAttempts++;
			}

			logger.error("Authentication failed: User not found: " + username);
		}
	}

	// Failed authentication - send response and return
	if (!success)
	{
		sendAuthResponse(peer, false, response);
		return;
	}

	// Check if player ID is already in use (multiple connections)
	bool alreadyLoggedIn = false;
	{
		std::scoped_lock<std::mutex> lock(playersMutex);
		alreadyLoggedIn = players.find(playerId) != players.end();
	}

	if (alreadyLoggedIn)
	{
		sendAuthResponse(peer, false, "Account already logged in");
		logger.error("Authentication failed: Account already logged in: " + username);
		return;
	}

	// Update the player object with authenticated info
	{
		std::scoped_lock<std::mutex> pLock(playersMutex);
		std::scoped_lock<std::mutex> peerLock(peerDataMutex);

		// Create a new player entry
		Player authenticatedPlayer;
		authenticatedPlayer.id = playerId;
		authenticatedPlayer.name = username;
		authenticatedPlayer.position = authData.lastPosition;
		authenticatedPlayer.lastValidPosition = authData.lastPosition;
		authenticatedPlayer.peer = peer;
		authenticatedPlayer.lastUpdateTime = Utils::getCurrentTimeMs();
		authenticatedPlayer.lastPingTime = Utils::getCurrentTimeMs();
		authenticatedPlayer.connectionStartTime = player.connectionStartTime;
		authenticatedPlayer.failedAuthAttempts = 0;
		authenticatedPlayer.totalBytesReceived = player.totalBytesReceived;
		authenticatedPlayer.totalBytesSent = player.totalBytesSent;
		authenticatedPlayer.pingMs = player.pingMs;
		authenticatedPlayer.isAuthenticated = true;
		authenticatedPlayer.isAdmin = authData.isAdmin;
		authenticatedPlayer.ipAddress = player.ipAddress;

		// Add new player entry before removing old one
		players[playerId] = authenticatedPlayer;

		// Now it's safe to remove the old player entry
		players.erase(peerKey);

		// Update peer data
		uint32_t* ptrPlayerId = reinterpret_cast<uint32_t*>(peer->data);
		if (ptrPlayerId != nullptr)
		{
			*ptrPlayerId = playerId;
		}
		else
		{
			peer->data = reinterpret_cast<void*>(new uint32_t(playerId));
		}

		// Add to spatial grid
		spatialGrid.addEntity(playerId, authenticatedPlayer.position);

		logger.info("Player " + username + " (ID: " + std::to_string(playerId) + ") logged in from " + player.ipAddress);
	}

	// Send authentication response
	sendAuthResponse(peer, true, std::to_string(playerId));

	// Send welcome message
	sendSystemMessage(playerId, "Welcome back, " + username + "!");

	// Broadcast join message
	broadcastSystemMessage(username + " has joined the game");

	// Plugin loggedin event
	pluginManager->dispatchPlayerLogin(players[playerId]);
}

// Handle player registration
void GameServer::handleRegistration(Player& player, const std::string& username, const std::string& password)
{
	// Validate username and password
	if (username.length() < 3 || username.length() > 20)
	{
		sendSystemMessage(player, "Username must be between 3 and 20 characters");
		return;
	}

	if (password.length() < 6)
	{
		sendSystemMessage(player, "Password must be at least 6 characters");
		return;
	}

	// Check for invalid characters
	if (username.find(',') != std::string::npos || username.find(' ') != std::string::npos || username.find('\t') != std::string::npos)
	{
		sendSystemMessage(player, "Username contains invalid characters");
		return;
	}

	bool success = false;
	std::string response;
	uint32_t newPlayerId = 0;

	// Register new player
	{
		std::scoped_lock<std::mutex> lock(authMutex);

		// Check if username already exists
		if (authenticatedPlayers.find(username) != authenticatedPlayers.end())
		{
			success = false;
			response = "Username already exists";
			logger.error("Registration failed: Username already exists: " + username);
		}
		else
		{
			// Create new player
			newPlayerId = nextPlayerId++;

			AuthData newPlayer;
			newPlayer.username = username;
			newPlayer.passwordHash = Utils::hashPassword(password);
			newPlayer.playerId = newPlayerId;
			newPlayer.lastLoginTime = std::time(nullptr);
			newPlayer.registrationTime = std::time(nullptr);
			newPlayer.lastPosition = config.spawnPosition;
			newPlayer.lastIpAddress = player.ipAddress;
			newPlayer.loginCount = 1;
			newPlayer.isAdmin = false;

			authenticatedPlayers[username] = newPlayer;

			success = true;
			response = "Registration successful";
			logger.info("New player registered: " + username + " (ID: " + std::to_string(newPlayerId) + ")");
		}
	}

	if (success)
	{
		// Save auth data immediately after registration
		saveAuthData();

		// Update the player object with registered info
		{
			std::scoped_lock<std::mutex> pLock(playersMutex);

			// Create a new player entry
			Player registeredPlayer;
			registeredPlayer.id = newPlayerId;
			registeredPlayer.name = username;
			registeredPlayer.position = config.spawnPosition;
			registeredPlayer.lastValidPosition = config.spawnPosition;
			registeredPlayer.peer = player.peer;
			registeredPlayer.lastUpdateTime = Utils::getCurrentTimeMs();
			registeredPlayer.lastPingTime = Utils::getCurrentTimeMs();
			registeredPlayer.connectionStartTime = player.connectionStartTime;
			registeredPlayer.failedAuthAttempts = 0;
			registeredPlayer.totalBytesReceived = player.totalBytesReceived;
			registeredPlayer.totalBytesSent = player.totalBytesSent;
			registeredPlayer.pingMs = player.pingMs;
			registeredPlayer.isAuthenticated = true;
			registeredPlayer.isAdmin = false;
			registeredPlayer.ipAddress = player.ipAddress;

			// Remove old player entry
			uint32_t oldKey = player.id == 0 ? reinterpret_cast<uintptr_t>(player.peer) : player.id;
			players.erase(oldKey);

			// Add new player entry
			players[newPlayerId] = registeredPlayer;

			// Update peer data
			uint32_t* ptrPlayerId = reinterpret_cast<uint32_t*>(player.peer->data);
			if (ptrPlayerId != nullptr)
			{
				*ptrPlayerId = newPlayerId;
			}
			else
			{
				player.peer->data = reinterpret_cast<void*>(new uint32_t(newPlayerId));
			}

			// Add to spatial grid
			spatialGrid.addEntity(newPlayerId, registeredPlayer.position);
		}

		// Send registration response
		sendAuthResponse(player.peer, true, std::to_string(newPlayerId));

		// Send welcome message
		sendSystemMessage(newPlayerId, "Welcome to the game, " + username + "!");

		// Broadcast join message
		broadcastSystemMessage(username + " has joined the game for the first time!");
	}
	else
	{
		sendSystemMessage(player, "Registration failed: " + response);
	}
}

void GameServer::syncPlayerStats()
{
	std::scoped_lock<std::mutex> statsLock(peerStatsMutex);
	std::scoped_lock<std::mutex> playersLock(playersMutex);

	// For each player, copy stats from the lightweight objects
	for (auto& playerPair: players)
	{
		Player& player = playerPair.second;
		if (player.peer != nullptr)
		{
			uintptr_t peerKey = reinterpret_cast<uintptr_t>(player.peer);
			auto statsIt = peerStats.find(peerKey);
			if (statsIt != peerStats.end())
			{
				player.totalBytesSent = statsIt->second.totalBytesSent.load();
				player.totalBytesReceived = statsIt->second.totalBytesReceived.load();
			}
		}
	}

	// Clean up stats for disconnected peers (optional)
	std::vector<uintptr_t> keysToRemove;
	for (auto& statsPair: peerStats)
	{
		bool found = false;
		for (auto& playerPair: players)
		{
			if (playerPair.second.peer != nullptr && reinterpret_cast<uintptr_t>(playerPair.second.peer) == statsPair.first)
			{
				found = true;
				break;
			}
		}
		if (!found)
		{
			keysToRemove.push_back(statsPair.first);
		}
	}

	for (uintptr_t key: keysToRemove)
	{
		peerStats.erase(key);
	}
}

void GameServer::handleDeltaPositionUpdate(Player& player, const std::string& deltaData)
{
	try
	{
		// Get current time to track when this update was received
		uint32_t currentTime = Utils::getCurrentTimeMs();

		// Start with the current position
		Position newPosition = player.position;

		// Parse the delta updates (format: "x12.34,y56.78,z90.12")
		auto parts = Utils::splitString(deltaData, ',');
		for (const auto& part: parts)
		{
			if (part.empty())
				continue;

			char axis = part[0];
			float value = std::stof(part.substr(1));

			// Update the corresponding coordinate
			switch (axis)
			{
				case 'x':
					newPosition.x = value;
					break;
				case 'y':
					newPosition.y = value;
					break;
				case 'z':
					newPosition.z = value;
					break;
				default:
					logger.error("Invalid axis in delta position update: " + std::string(1, axis));
					continue;
			}
		}

		// Validate movement if enabled
		if (config.enableMovementValidation)
		{
			Position oldPosition = player.position;
			float distance = oldPosition.distanceTo(newPosition);

			// Calculate elapsed time since last position update (in seconds)
			float elapsedTime = (currentTime - player.lastPositionUpdateTime) / 1000.0f;

			// Set a minimum elapsed time to avoid division by zero
			// and to handle the first position update
			if (elapsedTime < 0.01f)
				elapsedTime = 0.01f;

			// Calculate the player's current speed (units per second)
			float currentSpeed = distance / elapsedTime;

			// Check if speed is within allowed movement speed (with a small margin for error)
			// Add a 20% tolerance to account for network jitter
			float speedLimit = config.maxMovementSpeed * 1.2f;

			if (currentSpeed > speedLimit && distance > 0.5f)
			{ // Only check if moved significantly
				logger.debug("Player " + player.name + " moved too fast: " + std::to_string(currentSpeed) + " units/sec (limit: " + std::to_string(speedLimit) + "), distance: " + std::to_string(distance) + " in " + std::to_string(elapsedTime) + " seconds");

				// Reject movement and teleport back to last valid position
				sendTeleport(player, player.lastValidPosition);
				return;
			}

			// Movement is valid, update last valid position
			player.lastValidPosition = newPosition;
		}

		// Update player position and timestamp
		Position oldPosition = player.position;
		player.position = newPosition;
		player.lastPositionUpdateTime = currentTime;
		pluginManager->dispatchPlayerMove(player, oldPosition, player.position);

		// Update in spatial grid
		spatialGrid.updateEntity(player.id, oldPosition, newPosition);
	}
	catch (const std::exception& e)
	{
		logger.error("Error parsing delta position update from " + player.name + ": " + e.what());
	}
}

// Handle sending position to client
void GameServer::handleSendPosition(Player& player)
{
	try
	{
		// Get current time
		uint32_t currentTime = Utils::getCurrentTimeMs();

		// Create position string in the format "x12.34,y56.78,z90.12"
		std::string positionString = "x" + std::to_string(player.position.x) + "," + "y" + std::to_string(player.position.y) + "," + "z" + std::to_string(player.position.z);

		// Create a position update packet using the existing sendPacket method
		std::string positionPacket = "POSITION:" + positionString;

		// Send the position data to the client
		sendPacket(player.peer, positionPacket, false); // Using unreliable packets for position updates

		// Log the position update (optional)
		logger.debug("Sent position update to " + player.name + ": " + positionString);

		// Update timestamp of when we last sent position
		player.lastPositionUpdateTime = currentTime; // Using the existing timestamp field

		// Not sure if I should send it to ever player just yet
		// std::set<uint32_t> nearbyPlayers = spatialGrid.getNearbyEntities(player.position, config.interestRadius);
		// for (uint32_t nearbyId : nearbyPlayers) {
		//     // Skip self
		//     if (nearbyId == player.id) continue;
		//
		//     auto it = players.find(nearbyId);
		//     if (it != players.end()) {
		//         sendPacket(it->second.peer, positionPacket, false);
		//     }
		// }
	}
	catch (const std::exception& e)
	{
		logger.error("Error sending position update to " + player.name + ": " + e.what());
	}
}

// Handle chat message
void GameServer::handleChatMessage(Player& player, const std::string& message)
{
	// Ignore empty messages
	if (message.empty())
		return;

	// Check if it's a command
	if (message[0] == '/')
	{
		handleCommandMessage(player, message.substr(1));
		return;
	}

	logger.debug("Chat from " + player.name + ": " + message);

	// Store in chat history
	{
		std::scoped_lock<std::mutex> lock(chatMutex);

		ChatMessage chatMsg;
		chatMsg.sender = player.name;
		chatMsg.content = message;
		chatMsg.timestamp = Utils::getCurrentTimeMs();
		chatMsg.isGlobal = true;
		chatMsg.isSystem = false;
		chatMsg.range = 0;

		chatHistory.push_back(chatMsg);

		// Limit chat history size
		while (chatHistory.size() > MAX_CHAT_HISTORY)
		{
			chatHistory.pop_front();
		}
	}

	// Broadcast to all players
	broadcastChatMessage(player.name, message);

	// Update stats
	stats.chatMessagesSent++;
}

// Handle ping message
void GameServer::handlePingMessage(Player& player, const std::string& pingData)
{
	try
	{
		uint32_t clientTime = std::stoul(pingData);
		player.lastPingTime = Utils::getCurrentTimeMs();

		// Send pong response
		std::string pongMsg = "PONG:" + pingData;
		sendPacket(player.peer, pongMsg, false);
	}
	catch (const std::exception& e)
	{
		logger.error("Error parsing ping from " + player.name + ": " + e.what());
	}
}

// Handle command message
void GameServer::handleCommandMessage(Player& player, const std::string& commandStr)
{
	logger.debug("Processing command: '" + commandStr + "'");

	// Split command and args
	std::vector<std::string> parts = Utils::splitString(commandStr, ' ');
	if (parts.empty())
		return;

	std::string command = parts[0];
	std::transform(command.begin(), command.end(), command.begin(), [](unsigned char c) { return std::tolower(c); });

	bool commandHandled = false;
	if (pluginManager->dispatchPlayerCommand(player, command, parts))
	{
		commandHandled = true;
	}

	// Check if command exists
	auto it = commandHandlers.find(command);
	if (it != commandHandlers.end())
	{
		// Execute command
		it->second(player, parts);
	}
	else if (!commandHandled)
	{
		sendSystemMessage(player, "Unknown command: " + command);
	}
}

// Send auth response
void GameServer::sendAuthResponse(ENetPeer* peer, bool success, const std::string& message)
{
	std::string response = "AUTH_RESPONSE:" + std::string(success ? "success" : "failure") + "," + message;
	sendPacket(peer, response, true);
}

// Send packet to player
void GameServer::sendPacket(ENetPeer* peer, const std::string& message, bool reliable)
{
	if (peer == nullptr)
		return;

	// Create and send packet
	ENetPacket* packet = enet_packet_create(message.c_str(), message.length(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

	if (enet_peer_send(peer, 0, packet) < 0)
	{
		logger.error("Failed to send packet: " + message);
		enet_packet_destroy(packet);
		return;
	}

	// Update global stats
	stats.totalPacketsSent++;
	stats.totalBytesSent += message.length();

	// Update per-peer stats without accessing player objects
	{
		std::scoped_lock<std::mutex> lock(peerStatsMutex); // Much lighter lock
		peerStats[reinterpret_cast<uintptr_t>(peer)].totalBytesSent += message.length();
	}

	// No player object access here at all
}

// Send system message to player
void GameServer::sendSystemMessage(Player& player, const std::string& message)
{
	logger.trace("Sending system message to" + player.name + ": " + message);
	std::string sysMsg = "SYSTEM:" + message;
	sendPacket(player.peer, sysMsg, true);
}

// Send system message to player by ID
void GameServer::sendSystemMessage(uint32_t playerId, const std::string& message)
{
	std::scoped_lock<std::mutex> lock(playersMutex);

	auto it = players.find(playerId);
	if (it != players.end())
	{
		sendSystemMessage(it->second, message);
	}
}

// Send teleport command to player
void GameServer::sendTeleport(Player& player, const Position& position)
{
	std::string teleportMsg = "TELEPORT:" + std::to_string(position.x) + "," + std::to_string(position.y) + "," + std::to_string(position.z);
	sendPacket(player.peer, teleportMsg, true);
}

// Broadcast world state to all players
void GameServer::broadcastWorldState()
{
	std::scoped_lock<std::mutex> lock(playersMutex);

	for (auto& pair: players)
	{
		Player& player = pair.second;

		// Skip unauthenticated players
		if (!player.isAuthenticated)
			continue;

		// Prepare world state packet for this player
		std::string worldState = "WORLD_STATE:";

		// Get nearby entities
		std::set<uint32_t> visibleEntities;
		if (config.interestRadius > 0)
		{
			visibleEntities = spatialGrid.getNearbyEntities(player.position, config.interestRadius);
		}
		else
		{
			// No interest management, see all players
			for (const auto& otherPair: players)
			{
				if (otherPair.second.isAuthenticated)
				{
					visibleEntities.insert(otherPair.first);
				}
			}
		}

		// Add all players within interest radius
		for (uint32_t entityId: visibleEntities)
		{
			// Skip self
			if (entityId == player.id)
				continue;

			auto it = players.find(entityId);
			if (it != players.end() && it->second.isAuthenticated)
			{
				const Player& otherPlayer = it->second;

				worldState += std::to_string(otherPlayer.id) + "," + otherPlayer.name + "," + std::to_string(otherPlayer.position.x) + "," + std::to_string(otherPlayer.position.y) + "," + std::to_string(otherPlayer.position.z) + ";";
			}
		}

		// Update player's visible set for next time
		player.visiblePlayers = visibleEntities;

		// Send world state (use unreliable packet for frequent updates)
		sendPacket(player.peer, worldState, false);
	}
}

// Check for timed out players
void GameServer::checkTimeouts()
{
	std::scoped_lock<std::mutex> lock(playersMutex);
	uint32_t currentTime = Utils::getCurrentTimeMs();

	std::vector<uint32_t> timeoutPlayers;
	for (const auto& pair: players)
	{
		// Skip unauthenticated players
		if (!pair.second.isAuthenticated)
			continue;

		if (currentTime - pair.second.lastUpdateTime > config.timeoutMs)
		{
			timeoutPlayers.push_back(pair.first);
		}
	}

	// Disconnect timed-out players
	for (uint32_t id: timeoutPlayers)
	{
		auto it = players.find(id);
		if (it != players.end())
		{
			logger.info("Player " + it->second.name + " (ID: " + std::to_string(id) + ") timed out");

			auto it = players.find(id);
			if (it == players.end())
				return;

			std::string username = it->second.name;
			Position lastPos = it->second.position;

			// Save player data
			savePlayerData(username, lastPos);

			// Broadcast timeout message
			broadcastSystemMessage(it->second.name + " timed out");

			// Disconnect the player
			enet_peer_disconnect(it->second.peer, 0);
		}
	}
}

// Broadcast system message to all players
void GameServer::broadcastSystemMessage(const std::string& message)
{
	//std::scoped_lock<std::mutex> lock(playersMutex);

	logger.info("Broadcast: " + message);

	for (auto& pair: players)
	{
		if (pair.second.isAuthenticated)
		{
			sendSystemMessage(pair.second, message);
		}
	}

	// Add to chat history
	{
		std::scoped_lock<std::mutex> chatLock(chatMutex);

		ChatMessage chatMsg;
		chatMsg.sender = "Server";
		chatMsg.content = message;
		chatMsg.timestamp = Utils::getCurrentTimeMs();
		chatMsg.isGlobal = true;
		chatMsg.isSystem = true;
		chatMsg.range = 0;

		chatHistory.push_back(chatMsg);

		// Limit chat history size
		while (chatHistory.size() > MAX_CHAT_HISTORY)
		{
			chatHistory.pop_front();
		}
	}
}

std::vector<std::string> GameServer::getOnlinePlayerNames()
{
	std::scoped_lock<std::mutex> lock(playersMutex);

	std::vector<std::string> names;
	for (const auto& pair: players)
	{
		if (pair.second.isAuthenticated)
		{
			names.push_back(pair.second.name);
		}
	}

	return names;
}

// Broadcast chat message to all players
void GameServer::broadcastChatMessage(const std::string& sender, const std::string& message)
{
	std::scoped_lock<std::mutex> lock(playersMutex);

	std::string chatMsg = "CHAT:" + sender + ":" + message;

	pluginManager->dispatchChatMessage(sender, message);

	for (auto& pair: players)
	{
		if (pair.second.isAuthenticated)
		{
			sendPacket(pair.second.peer, chatMsg, true);
		}
	}
}

// Updated function signature
void GameServer::savePlayerData(const std::string& username, const Position& lastPos)
{
	// Find in authenticated players
	std::scoped_lock<std::mutex> aLock(authMutex);
	auto authIt = authenticatedPlayers.find(username);
	if (authIt != authenticatedPlayers.end())
	{
		// Save position
		authIt->second.lastPosition = lastPos;
		logger.debug("Saved position data for player " + username);
	}
}

// Load authentication data from file
void GameServer::loadAuthData()
{
	std::scoped_lock<std::mutex> lock(authMutex);

	logger.info("Loading player authentication data...");

	std::ifstream file(AUTH_DB_FILE, std::ios::binary);
	if (!file.is_open())
	{
		logger.info("No existing auth file found, starting fresh");
		return;
	}

	try
	{
		// Read number of entries
		size_t numEntries;
		file.read(reinterpret_cast<char*>(&numEntries), sizeof(numEntries));

		logger.info("Loading " + std::to_string(numEntries) + " player accounts...");

		uint32_t maxId = 0;
		authenticatedPlayers.clear();

		// Read each entry
		for (size_t i = 0; i < numEntries; i++)
		{
			AuthData auth;

			// Read username
			size_t usernameLength;
			if (!file.read(reinterpret_cast<char*>(&usernameLength), sizeof(usernameLength)))
			{
				throw std::runtime_error("Failed to read username length for entry " + std::to_string(i));
			}

			std::vector<char> usernameBuffer(usernameLength + 1, 0);
			if (!file.read(usernameBuffer.data(), usernameLength))
			{
				throw std::runtime_error("Failed to read username data for entry " + std::to_string(i));
			}
			auth.username = std::string(usernameBuffer.data(), usernameLength);

			// Read password hash
			size_t passwordHashLength;
			if (!file.read(reinterpret_cast<char*>(&passwordHashLength), sizeof(passwordHashLength)))
			{
				throw std::runtime_error("Failed to read password hash length for " + auth.username);
			}

			std::vector<char> passwordHashBuffer(passwordHashLength + 1, 0);
			if (!file.read(passwordHashBuffer.data(), passwordHashLength))
			{
				throw std::runtime_error("Failed to read password hash data for " + auth.username);
			}
			auth.passwordHash = std::string(passwordHashBuffer.data(), passwordHashLength);

			// Read player ID
			if (!file.read(reinterpret_cast<char*>(&auth.playerId), sizeof(auth.playerId)))
			{
				throw std::runtime_error("Failed to read player ID for " + auth.username);
			}

			// Read last login time
			if (!file.read(reinterpret_cast<char*>(&auth.lastLoginTime), sizeof(auth.lastLoginTime)))
			{
				throw std::runtime_error("Failed to read last login time for " + auth.username);
			}

			// Read registration time (with backwards compatibility)
			if (file.peek() != EOF)
			{
				if (!file.read(reinterpret_cast<char*>(&auth.registrationTime), sizeof(auth.registrationTime)))
				{
					auth.registrationTime = auth.lastLoginTime; // Default if not present
				}
			}
			else
			{
				auth.registrationTime = auth.lastLoginTime;
			}

			// Read last position
			if (file.peek() != EOF)
			{
				if (!file.read(reinterpret_cast<char*>(&auth.lastPosition), sizeof(auth.lastPosition)))
				{
					auth.lastPosition = config.spawnPosition; // Default if not present
				}
			}
			else
			{
				auth.lastPosition = config.spawnPosition;
			}

			// Read last IP address (with backwards compatibility)
			if (file.peek() != EOF)
			{
				size_t ipLength;
				if (file.read(reinterpret_cast<char*>(&ipLength), sizeof(ipLength)))
				{
					std::vector<char> ipBuffer(ipLength + 1, 0);
					if (file.read(ipBuffer.data(), ipLength))
					{
						auth.lastIpAddress = std::string(ipBuffer.data(), ipLength);
					}
					else
					{
						auth.lastIpAddress = "";
					}
				}
				else
				{
					auth.lastIpAddress = "";
				}
			}
			else
			{
				auth.lastIpAddress = "";
			}

			// Read login count (with backwards compatibility)
			if (file.peek() != EOF)
			{
				if (!file.read(reinterpret_cast<char*>(&auth.loginCount), sizeof(auth.loginCount)))
				{
					auth.loginCount = 1; // Default if not present
				}
			}
			else
			{
				auth.loginCount = 1;
			}

			// Read admin status (with backwards compatibility)
			if (file.peek() != EOF)
			{
				if (!file.read(reinterpret_cast<char*>(&auth.isAdmin), sizeof(auth.isAdmin)))
				{
					auth.isAdmin = false; // Default if not present
				}
			}
			else
			{
				auth.isAdmin = false;
			}

			// Track highest player ID to update nextPlayerId
			if (auth.playerId > maxId)
			{
				maxId = auth.playerId;
			}

			// Add to authenticated players map
			authenticatedPlayers[auth.username] = auth;

			logger.debug("Loaded account: " + auth.username + " (ID: " + std::to_string(auth.playerId) + ")");
		}

		// Update nextPlayerId to be one more than the highest ID found
		if (maxId >= nextPlayerId)
		{
			nextPlayerId = maxId + 1;
			logger.info("Updated nextPlayerId to " + std::to_string(nextPlayerId));
		}

		logger.info("Successfully loaded " + std::to_string(authenticatedPlayers.size()) + " player accounts from " + AUTH_DB_FILE);
	}
	catch (const std::exception& e)
	{
		logger.error("Error loading auth data: " + std::string(e.what()));
		logger.error("Some player accounts may not have been loaded correctly");
	}

	file.close();
}

// Save authentication data to file
void GameServer::saveAuthData()
{
	std::scoped_lock<std::mutex> lock(authMutex);

	logger.debug("Saving player authentication data...");

	// Create backup of existing file
	if (std::filesystem::exists(AUTH_DB_FILE))
	{
		try
		{
			std::filesystem::copy_file(AUTH_DB_FILE, std::string(AUTH_DB_FILE) + std::string(".bak"), std::filesystem::copy_options::overwrite_existing);
		}
		catch (const std::exception& e)
		{
			logger.error("Failed to create backup of auth file: " + std::string(e.what()));
		}
	}

	std::ofstream file(AUTH_DB_FILE, std::ios::binary);
	if (!file.is_open())
	{
		logger.error("Error: Could not open auth file for writing");
		return;
	}

	try
	{
		// Write number of entries
		size_t numEntries = authenticatedPlayers.size();
		file.write(reinterpret_cast<const char*>(&numEntries), sizeof(numEntries));

		// Write each entry
		for (const auto& pair: authenticatedPlayers)
		{
			const AuthData& auth = pair.second;

			// Write username
			size_t usernameLength = auth.username.length();
			file.write(reinterpret_cast<const char*>(&usernameLength), sizeof(usernameLength));
			file.write(auth.username.c_str(), usernameLength);

			// Write password hash
			size_t passwordHashLength = auth.passwordHash.length();
			file.write(reinterpret_cast<const char*>(&passwordHashLength), sizeof(passwordHashLength));
			file.write(auth.passwordHash.c_str(), passwordHashLength);

			// Write player ID
			file.write(reinterpret_cast<const char*>(&auth.playerId), sizeof(auth.playerId));

			// Write last login time
			file.write(reinterpret_cast<const char*>(&auth.lastLoginTime), sizeof(auth.lastLoginTime));

			// Write registration time
			file.write(reinterpret_cast<const char*>(&auth.registrationTime), sizeof(auth.registrationTime));

			// Write last position
			file.write(reinterpret_cast<const char*>(&auth.lastPosition), sizeof(auth.lastPosition));

			// Write last IP address
			size_t ipLength = auth.lastIpAddress.length();
			file.write(reinterpret_cast<const char*>(&ipLength), sizeof(ipLength));
			file.write(auth.lastIpAddress.c_str(), ipLength);

			// Write login count
			file.write(reinterpret_cast<const char*>(&auth.loginCount), sizeof(auth.loginCount));

			// Write admin status
			file.write(reinterpret_cast<const char*>(&auth.isAdmin), sizeof(auth.isAdmin));
		}

		file.close();
		logger.debug("Saved authentication data for " + std::to_string(numEntries) + " players");
	}
	catch (const std::exception& e)
	{
		logger.error("Error saving auth data: " + std::string(e.what()));
	}
}

// Load server configuration
void GameServer::loadConfig()
{
	logger.info("Loading server configuration...");

	std::ifstream file(CONFIG_FILE);
	if (!file.is_open())
	{
		logger.info("No config file found, using defaults");
		createDefaultConfig();
		return;
	}

	std::string line;
	while (std::getline(file, line))
	{
		// Skip comments and empty lines
		if (line.empty() || line[0] == '#')
			continue;

		auto parts = Utils::splitString(line, '=');
		if (parts.size() != 2)
			continue;

		std::string key = parts[0];
		std::string value = parts[1];

		// Trim whitespace
		key.erase(0, key.find_first_not_of(" \t"));
		key.erase(key.find_last_not_of(" \t") + 1);
		value.erase(0, value.find_first_not_of(" \t"));
		value.erase(value.find_last_not_of(" \t") + 1);

		try
		{
			if (key == "port")
			{
				config.port = static_cast<uint16_t>(std::stoi(value));
			}
			else if (key == "max_players")
			{
				config.maxPlayers = std::stoul(value);
			}
			else if (key == "broadcast_rate_ms")
			{
				config.broadcastRateMs = std::stoul(value);
			}
			else if (key == "timeout_ms")
			{
				config.timeoutMs = std::stoul(value);
			}
			else if (key == "save_interval_ms")
			{
				config.saveIntervalMs = std::stoul(value);
			}
			else if (key == "enable_movement_validation")
			{
				config.enableMovementValidation = (value == "true" || value == "1");
			}
			else if (key == "max_movement_speed")
			{
				config.maxMovementSpeed = std::stof(value);
			}
			else if (key == "interest_radius")
			{
				config.interestRadius = std::stof(value);
			}
			else if (key == "admin_password")
			{
				config.adminPassword = value;
			}
			else if (key == "log_to_console")
			{
				config.logToConsole = (value == "true" || value == "1");
			}
			else if (key == "log_to_file")
			{
				config.logToFile = (value == "true" || value == "1");
			}
			else if (key == "log_level")
			{
				config.logLevel = std::stoi(value);
			}
			else if (key == "enable_chat")
			{
				config.enableChat = (value == "true" || value == "1");
			}
			else if (key == "spawn_position_x")
			{
				config.spawnPosition.x = std::stof(value);
			}
			else if (key == "spawn_position_y")
			{
				config.spawnPosition.y = std::stof(value);
			}
			else if (key == "spawn_position_z")
			{
				config.spawnPosition.z = std::stof(value);
			}
		}
		catch (const std::exception& e)
		{
			logger.error("Error parsing config value for " + key + ": " + e.what());
		}
	}

	file.close();

	// Update logger settings
	logger.setLogToConsole(config.logToConsole);
	logger.setLogToFile(config.logToFile);
	logger.setLogLevel((LogLevel) config.logLevel);

	logger.info("Configuration loaded successfully");
}

// Create default configuration file
void GameServer::createDefaultConfig()
{
	logger.info("Creating default configuration file...");

	std::ofstream file(CONFIG_FILE);
	if (!file.is_open())
	{
		logger.error("Failed to create default config file");
		return;
	}

	file << "# Server Configuration\n";
	file << "port=" << DEFAULT_PORT << "\n";
	file << "max_players=" << MAX_PLAYERS << "\n";
	file << "broadcast_rate_ms=" << BROADCAST_RATE_MS << "\n";
	file << "timeout_ms=" << PLAYER_TIMEOUT_MS << "\n";
	file << "save_interval_ms=" << SAVE_INTERVAL_MS << "\n";
	file << "enable_movement_validation=" << (MOVEMENT_VALIDATION ? "true" : "false") << "\n";
	file << "max_movement_speed=" << MAX_MOVEMENT_SPEED << "\n";
	file << "interest_radius=" << INTEREST_RADIUS << "\n";
	file << "admin_password=" << ADMIN_PASSWORD << "\n";
	file << "log_to_console=true\n";
	file << "log_to_file=true\n";
	file << "log_level=1\n";
	file << "enable_chat=true\n";
	file << "spawn_position_x=" << DEFAULT_SPAWN_X << "\n";
	file << "spawn_position_y=" << DEFAULT_SPAWN_Y << "\n";
	file << "spawn_position_z=" << DEFAULT_SPAWN_Z << "\n";

	file.close();

	logger.info("Default configuration file created");
}

// Initialize command handlers
void GameServer::initializeCommandHandlers()
{
	// Help command
	commandHandlers["help"] = [this](Player& player, const std::vector<std::string>& args)
	{
		std::string helpMessage = "Available commands:\n"
		                          "/help - Show this help\n"
		                          "/pos - Show current position\n"
		                          "/tp x y z - Teleport to coordinates\n"
		                          "/players - List online players\n"
		                          "/me action - Send an action message\n"
		                          "/w username message - Send private message";

		if (player.isAdmin)
		{
			helpMessage += "\n\nAdmin commands:\n"
			               "/kick username - Kick a player\n"
			               "/ban username - Ban a player\n"
			               "/broadcast message - Broadcast message to all\n"
			               "/setadmin username - Grant admin status\n"
			               "/tpplayer username - Teleport to player";
		}

		sendSystemMessage(player, helpMessage);
	};

	// Position command
	commandHandlers["pos"] = [this](Player& player, const std::vector<std::string>& args)
	{
		std::stringstream ss;
		ss << "Current position: X=" << player.position.x << " Y=" << player.position.y << " Z=" << player.position.z;
		sendSystemMessage(player, ss.str());
	};

	// Teleport command
	commandHandlers["tp"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (args.size() < 4)
		{
			sendSystemMessage(player, "Usage: /tp x y z");
			return;
		}

		try
		{
			float x = std::stof(args[1]);
			float y = std::stof(args[2]);
			float z = std::stof(args[3]);

			Position oldPos = player.position;
			Position newPos = { x, y, z };

			// Update player position
			player.position = newPos;
			player.lastValidPosition = newPos;

			// Update in spatial grid
			spatialGrid.updateEntity(player.id, oldPos, newPos);

			// Send teleport confirmation
			sendTeleport(player, newPos);
			sendSystemMessage(player, "Teleported to X=" + args[1] + " Y=" + args[2] + " Z=" + args[3]);
		}
		catch (const std::exception& e)
		{
			sendSystemMessage(player, "Invalid coordinates: " + std::string(e.what()));
		}
	};

	// Players list command
	commandHandlers["players"] = [this](Player& player, const std::vector<std::string>& args)
	{
		std::scoped_lock<std::mutex> lock(playersMutex);

		std::stringstream ss;
		ss << "Online players (" << players.size() << "):";
		sendSystemMessage(player, ss.str());

		for (const auto& pair: players)
		{
			if (pair.second.isAuthenticated)
			{
				std::string adminTag = pair.second.isAdmin ? " [ADMIN]" : "";
				sendSystemMessage(player, "- " + pair.second.name + adminTag);
			}
		}
	};

	// Me (action) command
	commandHandlers["me"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (args.size() < 2)
		{
			sendSystemMessage(player, "Usage: /me action");
			return;
		}

		// Combine args into action text
		std::string action;
		for (size_t i = 1; i < args.size(); ++i)
		{
			if (i > 1)
				action += " ";
			action += args[i];
		}

		// Broadcast action message
		broadcastChatMessage("ACTION", player.name + " " + action);
	};

	// Whisper command
	commandHandlers["w"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (args.size() < 3)
		{
			sendSystemMessage(player, "Usage: /w username message");
			return;
		}

		std::string targetName = args[1];

		// Combine args into message text
		std::string message;
		for (size_t i = 2; i < args.size(); ++i)
		{
			if (i > 2)
				message += " ";
			message += args[i];
		}

		// Find target player
		Player* targetPlayer = nullptr;
		{
			std::scoped_lock<std::mutex> lock(playersMutex);
			for (auto& pair: players)
			{
				if (pair.second.isAuthenticated && pair.second.name == targetName)
				{
					targetPlayer = &pair.second;
					break;
				}
			}
		}

		if (targetPlayer != nullptr)
		{
			// Send to target
			std::string targetMsg = "CHAT:*WHISPER* " + player.name + ":" + message;
			sendPacket(targetPlayer->peer, targetMsg, true);

			// Send confirmation to sender
			sendSystemMessage(player, "Whisper to " + targetName + ": " + message);
		}
		else
		{
			sendSystemMessage(player, "Player not found: " + targetName);
		}
	};

	// Admin commands

	// Kick command
	commandHandlers["kick"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			sendSystemMessage(player, "You don't have permission to use this command");
			return;
		}

		if (args.size() < 2)
		{
			sendSystemMessage(player, "Usage: /kick username");
			return;
		}

		std::string targetName = args[1];
		kickPlayer(targetName, player.name);
		sendSystemMessage(player, "Attempted to kick player: " + targetName);
	};

	// Ban command
	commandHandlers["ban"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			sendSystemMessage(player, "You don't have permission to use this command");
			return;
		}

		if (args.size() < 2)
		{
			sendSystemMessage(player, "Usage: /ban username");
			return;
		}

		std::string targetName = args[1];
		banPlayer(targetName, player.name);
		sendSystemMessage(player, "Attempted to ban player: " + targetName);
	};

	// Broadcast command
	commandHandlers["broadcast"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			sendSystemMessage(player, "You don't have permission to use this command");
			return;
		}

		if (args.size() < 2)
		{
			sendSystemMessage(player, "Usage: /broadcast message");
			return;
		}

		// Combine args into message text
		std::string message;
		for (size_t i = 1; i < args.size(); ++i)
		{
			if (i > 1)
				message += " ";
			message += args[i];
		}

		broadcastSystemMessage("[Broadcast] " + message);
		logger.info("Admin broadcast from " + player.name + ": " + message);
	};

	// Set admin command
	commandHandlers["setadmin"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			sendSystemMessage(player, "You don't have permission to use this command");
			return;
		}

		if (args.size() < 2)
		{
			sendSystemMessage(player, "Usage: /setadmin username");
			return;
		}

		std::string targetName = args[1];
		if (setPlayerAdmin(targetName, true))
		{
			sendSystemMessage(player, "Admin status granted to " + targetName);
		}
		else
		{
			sendSystemMessage(player, "Failed to set admin status for " + targetName);
		}
	};

	// Teleport to player command
	commandHandlers["tpplayer"] = [this](Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			sendSystemMessage(player, "You don't have permission to use this command");
			return;
		}

		if (args.size() < 2)
		{
			sendSystemMessage(player, "Usage: /tpplayer username");
			return;
		}

		std::string targetName = args[1];

		// Find target player
		Player* targetPlayer = nullptr;
		{
			std::scoped_lock<std::mutex> lock(playersMutex);
			for (auto& pair: players)
			{
				if (pair.second.isAuthenticated && pair.second.name == targetName)
				{
					targetPlayer = &pair.second;
					break;
				}
			}
		}

		if (targetPlayer != nullptr)
		{
			Position oldPos = player.position;
			Position newPos = targetPlayer->position;

			// Update player position
			player.position = newPos;
			player.lastValidPosition = newPos;

			// Update in spatial grid
			spatialGrid.updateEntity(player.id, oldPos, newPos);

			// Send teleport confirmation
			sendTeleport(player, newPos);
			sendSystemMessage(player, "Teleported to player: " + targetName);
		}
		else
		{
			sendSystemMessage(player, "Player not found: " + targetName);
		}
	};
}

// Kick a player by name
bool GameServer::kickPlayer(const std::string& playerName, const std::string& adminName)
{
	std::scoped_lock<std::mutex> lock(playersMutex);

	for (auto& pair: players)
	{
		if (pair.second.isAuthenticated && pair.second.name == playerName)
		{
			// Send kick message
			sendSystemMessage(pair.second, "You have been kicked by " + adminName);

			// Log the kick
			logger.info("Player " + playerName + " kicked by " + adminName);

			// Broadcast kick message
			broadcastSystemMessage(playerName + " has been kicked by " + adminName);

			// Disconnect the player
			enet_peer_disconnect(pair.second.peer, 0);
			return true;
		}
	}

	return false;
}

// Ban a player by name
bool GameServer::banPlayer(const std::string& playerName, const std::string& adminName)
{
	// TODO: Implement a proper ban system with IP tracking and ban list

	// For now, just kick the player
	return kickPlayer(playerName, adminName);
}

// Set a player's admin status
bool GameServer::setPlayerAdmin(const std::string& playerName, bool isAdmin)
{
	// Update in auth data
	{
		std::scoped_lock<std::mutex> lock(authMutex);

		auto it = authenticatedPlayers.find(playerName);
		if (it != authenticatedPlayers.end())
		{
			it->second.isAdmin = isAdmin;

			// Save changes
			saveAuthData();

			logger.info("Admin status " + std::string(isAdmin ? "granted to" : "revoked from") + " player " + playerName);
		}
		else
		{
			logger.error("Failed to find player in auth data: " + playerName);
			return false;
		}
	}

	// Update in active players if online
	{
		std::scoped_lock<std::mutex> lock(playersMutex);

		for (auto& pair: players)
		{
			if (pair.second.isAuthenticated && pair.second.name == playerName)
			{
				pair.second.isAdmin = isAdmin;

				// Notify the player
				sendSystemMessage(pair.second, "Your admin status has been " + std::string(isAdmin ? "granted" : "revoked"));

				break;
			}
		}
	}

	return true;
}

// Print server status to console
void GameServer::printServerStatus()
{
	uint32_t authenticatedCount = 0;
	{
		std::scoped_lock<std::mutex> lock(playersMutex);
		for (const auto& pair: players)
		{
			if (pair.second.isAuthenticated)
			{
				authenticatedCount++;
			}
		}
	}

	logger.info("===== Server Status =====");
	logger.info("Version: " + std::string(VERSION));
	logger.info("Uptime: " + Utils::formatUptime(stats.getUptimeSeconds()));
	logger.info("Port: " + std::to_string(config.port));
	logger.info("Players: " + std::to_string(authenticatedCount) + " online, " + std::to_string(authenticatedPlayers.size()) + " registered");
	logger.info("Max concurrent players: " + std::to_string(stats.maxConcurrentPlayers));
	logger.info("Total connections: " + std::to_string(stats.totalConnections));
	logger.info("Failed auth attempts: " + std::to_string(stats.authFailures));
	logger.info("Network stats:");
	logger.info("  Packets: " + std::to_string(stats.totalPacketsSent) + " sent, " + std::to_string(stats.totalPacketsReceived) + " received");
	logger.info("  Data: " + Utils::formatBytes(stats.totalBytesSent) + " sent, " + Utils::formatBytes(stats.totalBytesReceived) + " received");
	logger.info("=========================");
}

// Print player list to console
void GameServer::printPlayerList()
{
	std::scoped_lock<std::mutex> lock(playersMutex);

	logger.info("===== Online Players =====");

	if (players.empty() || std::none_of(players.begin(), players.end(), [](const auto& pair) { return pair.second.isAuthenticated; }))
	{
		logger.info("No players online.");
	}
	else
	{
		for (const auto& pair: players)
		{
			if (pair.second.isAuthenticated)
			{
				std::string playerInfo = "- " + pair.second.name + " (ID: " + std::to_string(pair.second.id) + ")" + (pair.second.isAdmin ? " [ADMIN]" : "") + " @ X=" + std::to_string(pair.second.position.x) + " Y=" + std::to_string(pair.second.position.y) + " Z=" + std::to_string(pair.second.position.z) + " | IP: " + pair.second.ipAddress;
				logger.info(playerInfo);
			}
		}
	}

	logger.info("=========================");
}

// Print console help
void GameServer::printConsoleHelp()
{
	logger.info("===== Console Commands =====");
	logger.info("help - Show this help");
	logger.info("status - Show server status");
	logger.info("players - List online players");
	logger.info("broadcast <message> - Send message to all players");
	logger.info("kick <username> - Kick a player");
	logger.info("ban <username> - Ban a player");
	logger.info("save - Save player data manually");
	logger.info("set_admin <username> - Grant admin status");
	logger.info("remove_admin <username> - Remove admin status");
	logger.info("reload - Reload configuration");
	logger.info("plugins - List loaded plugins");
	logger.info("loadplugin <path> - Load a plugin");
	logger.info("unloadplugin <name> - Unload a plugin");
	logger.info("reloadplugin <name> - Reload a plugin");
	logger.info("reloadallplugins - Reload all plugins");
	logger.info("loglevel <0-6> - Set log level (0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=fatal, 6=off)");
	logger.info("quit/exit - Shutdown server");
	logger.info("===========================");
}

ServerStats::ServerStats()
{
	startTime = getCurrentTimeMs();
}

uint32_t ServerStats::getCurrentTimeMs()
{
	return (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint32_t ServerStats::getUptimeSeconds() const
{
	return (getCurrentTimeMs() - startTime) / 1000;
}
