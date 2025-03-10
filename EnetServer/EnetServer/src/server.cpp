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
      : dbManager(logger), threadManager()
{
	// Set up logger
	logger.info("Initializing server...");

	// Load configuration
	loadConfig();

	// Initialize database if enabled
	if (config.useDatabase)
	{
		initializeDatabase();
	}

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

	// Close database connection if used
	if (config.useDatabase)
	{
		dbManager.disconnect();
	}
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

bool GameServer::initializeDatabase()
{
	// Check environment variables first
	std::string envHost = Utils::getEnvVar("GAME_DB_HOST");
	std::string envUser = Utils::getEnvVar("GAME_DB_USER");
	std::string envPass = Utils::getEnvVar("GAME_DB_PASSWORD");
	std::string envName = Utils::getEnvVar("GAME_DB_NAME");
	std::string envPort = Utils::getEnvVar("GAME_DB_PORT");

	// Override config with environment variables if present
	std::string dbHost = !envHost.empty() ? envHost : config.dbHost;
	std::string dbUser = !envUser.empty() ? envUser : config.dbUser;
	std::string dbPassword = !envPass.empty() ? envPass : config.dbPassword;
	std::string dbName = !envName.empty() ? envName : config.dbName;
	int dbPort = !envPort.empty() ? std::stoi(envPort) : config.dbPort;

	// Set connection details
	dbManager.setConnectionInfo(dbHost, dbUser, dbPassword, dbName, dbPort);

	// Connect to database
	if (!dbManager.connect())
	{
		logger.error("Failed to connect to database. Falling back to file storage.");
		config.useDatabase = false;
		return false;
	}

	logger.info("Database connection established successfully");
	return true;
}

void GameServer::initializePluginCommandHandlers()
{
	// Plugin commands
	commandHandlers["plugins"] = [this](const Player& player, const std::vector<std::string>& args)
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

	commandHandlers["loadplugin"] = [this](const Player& player, const std::vector<std::string>& args)
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

	commandHandlers["unloadplugin"] = [this](const Player& player, const std::vector<std::string>& args)
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

	commandHandlers["reloadplugin"] = [this](const Player& player, const std::vector<std::string>& args)
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

	commandHandlers["reloadallplugins"] = [this](const Player& player, const std::vector<std::string>& args)
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
	stopRequested = false;
	logger.info("Server starting...");

	// Schedule all recurring tasks
	scheduleRecurringTask([this]() { networkTaskFunc(); }, 1, networkTaskFuture, "Network");
	scheduleRecurringTask([this]() { updateTaskFunc(); }, config.broadcastRateMs, updateTaskFuture, "Update");
	scheduleRecurringTask([this]() { saveTaskFunc(); }, config.saveIntervalMs, saveTaskFuture, "Save");

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
	stopRequested = true;

	// Unload plugins explicitly before shutting down threads
	if (pluginManager)
	{
		auto plugins = getLoadedPlugins();
		for (const auto& name: plugins)
		{
			unloadPlugin(name);
		}
	}

	// Save player data
	saveAuthData();

	// Wait for ongoing tasks to complete
	try
	{
		// Wait for a reasonable timeout on each task
		const auto timeout = std::chrono::seconds(5);

		if (networkTaskFuture.valid())
		{
			auto status = networkTaskFuture.wait_for(timeout);
			if (status != std::future_status::ready)
			{
				logger.warning("Network task didn't finish in time, continuing shutdown...");
			}
		}

		if (updateTaskFuture.valid())
		{
			auto status = updateTaskFuture.wait_for(timeout);
			if (status != std::future_status::ready)
			{
				logger.warning("Update task didn't finish in time, continuing shutdown...");
			}
		}

		if (saveTaskFuture.valid())
		{
			auto status = saveTaskFuture.wait_for(timeout);
			if (status != std::future_status::ready)
			{
				logger.warning("Save task didn't finish in time, continuing shutdown...");
			}
		}

		// Wait for all remaining tasks to complete
		threadManager.waitForTasks();
	}
	catch (const std::exception& e)
	{
		logger.error("Exception during task shutdown: " + std::string(e.what()));
	}

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

		// Process commands via thread manager
		if (command == "quit" || command == "exit")
		{
			break;
		}
		else if (!command.empty())
		{
			// Use the thread manager to handle the command processing
			// This is a low-priority task that doesn't block the main thread
			threadManager.scheduleTask(
			        [this, command]()
			        {
				        if (command == "status")
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
				        else if (command.substr(0, 9) == "broadcast")
				        {
					        std::string message = command.substr(10);
					        broadcastSystemMessage(message);
				        }
				        else if (command.substr(0, 4) == "kick")
				        {
					        std::string playerName = command.substr(5);
					        kickPlayer(playerName);
				        }
				        else if (command.substr(0, 3) == "ban")
				        {
					        std::string playerName = command.substr(4);
					        banPlayer(playerName);
				        }
				        else if (command == "save")
				        {
					        saveAuthData();
					        logger.info("Player data saved manually");
				        }
				        else if (command.substr(0, 8) == "setadmin")
				        {
					        std::string playerName = command.substr(9);
					        setPlayerAdmin(playerName, true);
				        }
				        else if (command.substr(0, 11) == "removeadmin")
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
			        });
		}
	}

	shutdown();
}

// Network thread function
void GameServer::networkTaskFunc()
{
	ENetEvent event;

	// Process network events
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
}

// Update thread function
void GameServer::updateTaskFunc()
{
	// Get current time
	uint32_t currentTime = Utils::getCurrentTimeMs();

	// Check for plugin updates periodically
	if (currentTime - lastPluginCheckTime > pluginCheckIntervalMs)
	{
		threadManager.scheduleReadTask({ GameResources::PluginsId },
		        [this]()
		        {
			        pluginManager->checkForPluginUpdates();
			        lastPluginCheckTime = Utils::getCurrentTimeMs();
		        });
	}

	// Synchronize player stats (needs access to Players and PeerStats)
	threadManager.scheduleResourceTask({ GameResources::PlayersId, GameResources::PeerStatsId },
	        [this]()
	        {
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
	        });

	// Dispatch server tick to plugins
	threadManager.scheduleReadTask({ GameResources::PluginsId }, [this]() { pluginManager->dispatchServerTick(); });

	// Broadcast world state (needs access to Players and SpatialGrid)
	threadManager.scheduleReadTask({ GameResources::PlayersId, GameResources::SpatialGridId }, [this]() { broadcastWorldState(); });

	// Check for timeouts
	threadManager.scheduleResourceTask({ GameResources::PlayersId }, [this]() { checkTimeouts(); });
}

// Save thread function
void GameServer::saveTaskFunc()
{
	// Save player data
	saveAuthData();
	logger.debug("Player data auto-saved");
}

void GameServer::scheduleRecurringTask(std::function<void()> task, uint32_t intervalMs, std::future<void>& futureHandle, const std::string& taskName)
{
	// Create a task that will repeatedly run until the server is shut down
	auto recurringTask = [this, task, intervalMs, taskName]()
	{
		logger.info(taskName + " task started");

		while (!stopRequested)
		{
			// Execute the task
			task();

			// Sleep for the interval
			auto endTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);
			while (std::chrono::steady_clock::now() < endTime && !stopRequested)
			{
				// Check if shutdown was requested every 100ms
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}

		logger.info(taskName + " task stopped");
	};

	// Schedule the recurring task and save its future
	futureHandle = threadManager.scheduleTaskWithResult(recurringTask);
}

// Handle client connection
void GameServer::handleClientConnect(const ENetEvent& event)
{
	// Use resource task with write access to Players and the SpatialGrid
	threadManager.scheduleResourceTask({ GameResources::PlayersId, GameResources::PeerDataId, GameResources::SpatialGridId },
	        [this, event]()
	        {
		        stats.totalConnections++;

		        std::string ipAddress = Utils::peerAddressToString(event.peer->address);
		        logger.info("New client connected from " + ipAddress);

		        // Create temporary player entry
		        uint32_t tempId = 0; // Use 0 for unauthenticated players
		        std::string defaultName = "Guest" + std::to_string(Utils::getCurrentTimeMs() % 10000);

		        Player newPlayer;
		        newPlayer.id = tempId;
		        newPlayer.name = defaultName;
		        newPlayer.position = config.spawnPosition;
		        newPlayer.lastValidPosition = config.spawnPosition;
		        newPlayer.peer = event.peer;
		        newPlayer.lastUpdateTime = Utils::getCurrentTimeMs();
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

		        logger.info("Temporary player created: " + defaultName);

		        // Update concurrent player count
		        if (players.size() > stats.maxConcurrentPlayers)
		        {
			        stats.maxConcurrentPlayers = players.size();
		        }
	        });
}

// Handle client message
void GameServer::handleClientMessage(const ENetEvent& event)
{
	// First, update stats and get the basic information that doesn't require locking
	stats.totalPacketsReceived++;
	stats.totalBytesReceived += event.packet->dataLength;

	// Parse the packet using our new packet system
	auto packet = packetManager.receivePacket(event.packet);
	if (!packet)
	{
		// Invalid packet, log with more details for debugging
		logger.error("Received invalid packet - size: " + std::to_string(event.packet->dataLength));

		// If it's large enough to potentially have a header, try to extract more info
		if (event.packet->dataLength >= sizeof(GameProtocol::PacketHeader))
		{
			const auto* header = reinterpret_cast<const GameProtocol::PacketHeader*>(event.packet->data);
			logger.error("  Magic: 0x" + std::to_string(header->magic) + ", Version: " + std::to_string(header->version) + ", Type: " + std::to_string(static_cast<int>(header->type)));
		}
		return;
	}

	// Now handle the message with the appropriate resource access
	threadManager.scheduleResourceTask(
	        {
	                GameResources::PlayersId,   // Need access to players map
	                GameResources::PeerDataId,  // Need access to peer->data
	                GameResources::PeerStatsId, // For updating statistics
	                GameResources::PluginsId    // For plugin event dispatch
	        },
	        [this, event, packetPtr = std::move(packet)]() mutable
	        {
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

		        if (player == nullptr)
		        {
			        logger.error("Received message from unknown player. ID: " + std::to_string(playerId));
			        return;
		        }

		        // Update player's last activity time
		        player->lastUpdateTime = Utils::getCurrentTimeMs();
		        player->totalBytesReceived += event.packet->dataLength;

		        // Log the message type
		        logger.logNetworkEvent("Message from " + player->name + ": Packet Type " + GameProtocol::getPacketTypeName(packetPtr->getType()));

		        // Send plugin event (you might need to adapt this for binary packets)
		        // For now, we'll skip this or implement a string-based representation

		        // Handle the packet based on its type
		        handlePacket(*player, std::move(packetPtr));
	        });
}

void GameServer::handlePacket(const Player& player, std::unique_ptr<GameProtocol::Packet> packetPtr)
{
	auto& packet = *packetPtr;

	// Handle different packet types
	switch (packet.getType())
	{
		case GameProtocol::PacketType::AuthRequest:
		{
			// Handle authentication request
			auto& authPacket = static_cast<GameProtocol::AuthRequestPacket&>(packet);

			// Schedule a new task for auth handling
			threadManager.scheduleTask([this, username = authPacket.username, password = authPacket.password, playerPeer = player.peer]() { handleAuthMessage(username + "," + password, playerPeer); });
			break;
		}

		case GameProtocol::PacketType::Registration:
		{
			// Handle registration request
			auto& regPacket = static_cast<GameProtocol::RegistrationPacket&>(packet);

			// Schedule registration handling
			threadManager.scheduleTask([this, playerCopy = player, username = regPacket.username, password = regPacket.password]() { handleRegistration(playerCopy, username, password); });
			break;
		}

		case GameProtocol::PacketType::DeltaPositionUpdate:
		{
			// Handle position update
			if (!player.isAuthenticated)
			{
				logger.error("Unauthenticated player tried to update position");
				return;
			}

			auto& posPacket = static_cast<GameProtocol::DeltaPositionUpdatePacket&>(packet);
			Position newPos = posPacket.position;

			// Schedule position update with the right resources
			threadManager.scheduleResourceTask({ GameResources::PlayersId, GameResources::SpatialGridId },
			        [this, playerId = player.id, newPos]()
			        {
				        // Convert to a delta format for the existing method
				        std::string deltaData = "x" + std::to_string(newPos.x) + ",y" + std::to_string(newPos.y) + ",z" + std::to_string(newPos.z);

				        // Call existing position update logic
				        handleDeltaPositionUpdate(playerId, deltaData);
			        });
			break;
		}

		case GameProtocol::PacketType::ChatMessage:
		{
			// Handle chat message
			if (!player.isAuthenticated)
			{
				logger.error("Unauthenticated player tried to send chat message");
				return;
			}

			auto& chatPacket = static_cast<GameProtocol::ChatMessagePacket&>(packet);

			// Schedule chat handling with the right resources
			threadManager.scheduleResourceTask({ GameResources::ChatId, GameResources::PlayersId }, [this, playerCopy = player, message = chatPacket.message]() { handleChatMessage(playerCopy, message); });
			break;
		}

		case GameProtocol::PacketType::Command:
		{
			// Handle command
			auto& cmdPacket = static_cast<GameProtocol::CommandPacket&>(packet);

			if (!player.isAuthenticated && cmdPacket.command != "login" && cmdPacket.command != "register")
			{
				sendSystemMessage(player, "You must log in first. Use /login username password");
				return;
			}

			// Special handling for login/register commands from unauthenticated players
			if (!player.isAuthenticated)
			{
				if (cmdPacket.command == "login")
				{
					if (cmdPacket.arguments.size() >= 2)
					{
						std::string username = cmdPacket.arguments[0];
						std::string password = cmdPacket.arguments[1];

						// Schedule auth handling
						threadManager.scheduleTask([this, username, password, playerPeer = player.peer]() { handleAuthMessage(username + "," + password, playerPeer); });
					}
					else
					{
						sendSystemMessage(player, "Usage: /login username password");
					}
				}
				else if (cmdPacket.command == "register")
				{
					if (cmdPacket.arguments.size() >= 2)
					{
						std::string username = cmdPacket.arguments[0];
						std::string password = cmdPacket.arguments[1];

						// Schedule registration handling
						threadManager.scheduleTask([this, playerCopy = player, username, password]() { handleRegistration(playerCopy, username, password); });
					}
					else
					{
						sendSystemMessage(player, "Usage: /register username password");
					}
				}
				return;
			}

			// For authenticated players, handle the command normally
			threadManager.scheduleResourceTask({ GameResources::PlayersId, GameResources::AuthId, GameResources::ChatId },
			        [this, playerCopy = player, cmd = cmdPacket.command, args = cmdPacket.arguments]()
			        {
				        // Call existing command handler
				        handleCommandMessage(playerCopy, cmd + " " + Utils::joinStrings(args, " "));
			        });
			break;
		}

		case GameProtocol::PacketType::PositionUpdate:
		{
			// Handle position update
			if (!player.isAuthenticated)
			{
				logger.error("Unauthenticated player tried to update position");
				return;
			}
			auto& posPacket = static_cast<GameProtocol::PositionUpdatePacket&>(packet);
			// Schedule position update with the right resources
			threadManager.scheduleResourceTask({ GameResources::PlayersId, GameResources::SpatialGridId }, [this, playerId = player.id, newPos = posPacket.position]() { handlePositionUpdate(playerId, newPos); });
			break;
		}
		

		default:
			logger.error("Received unknown packet type: " + GameProtocol::getPacketTypeName(packet.getType()));
			break;
	}
}

void GameServer::handlePositionUpdate(uint32_t playerId, const Position& newPos)
{
	// Find player
	auto it = players.find(playerId);
	if (it == players.end())
	{
		logger.error("Received position update for unknown player ID: " + std::to_string(playerId));
		return;
	}
	Player& player = it->second;
	// Update player position
	Position lastPos = player.position;
	player.position = newPos;
	// Update spatial grid
	spatialGrid.updateEntity(playerId, lastPos, newPos);
}

// Handle client disconnect
void GameServer::handleClientDisconnect(const ENetEvent& event)
{
	// Use a resource task that needs access to all relevant resources
	threadManager.scheduleResourceTask(
	        {
	                GameResources::PeerDataId,    // For peer->data access
	                GameResources::PeerStatsId,   // For peerStats map
	                GameResources::PlayersId,     // For players map
	                GameResources::SpatialGridId, // For spatial grid updates
	                GameResources::PluginsId      // For plugin dispatch
	        },
	        [this, event]()
	        {
		        // Get player ID from peer data
		        uint32_t playerId = 0;
		        uint32_t* ptrPlayerId = reinterpret_cast<uint32_t*>(event.peer->data);
		        if (ptrPlayerId != nullptr)
		        {
			        playerId = *ptrPlayerId;
		        }

		        // Clean up stats
		        peerStats.erase(reinterpret_cast<uintptr_t>(event.peer));

		        // Handle cleanup based on authentication status
		        std::string playerName;

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

				        // Since savePlayerData accesses Auth resource, schedule it separately
				        // to avoid resource deadlocks
				        threadManager.scheduleResourceTask({ GameResources::AuthId, GameResources::DatabaseId }, [this, playerName, lastPos]() { savePlayerData(playerName, lastPos); });

				        // Remove from spatial grid
				        spatialGrid.removeEntity(playerId, it->second.position);

				        // Remove from players map
				        players.erase(it);
			        }
		        }

		        // Clean up peer data
		        delete ptrPlayerId;
		        event.peer->data = nullptr;

		        logger.info("Player disconnected: " + playerName + " (ID: " + std::to_string(playerId) + ")");

		        // Broadcast player disconnect if it was an authenticated player
		        if (playerId != 0)
		        {
			        // Schedule separately since broadcast also needs access to resources
			        threadManager.scheduleTask([this, playerName]() { broadcastSystemMessage(playerName + " has left the game"); });
		        }
	        });
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

	// Find player and authenticate - needs access to Players and Auth resources
	threadManager.scheduleResourceTask(
	        {
	                GameResources::PlayersId,    // For accessing players map
	                GameResources::AuthId,       // For accessing authentication data
	                GameResources::PeerDataId,   // For peer->data access
	                GameResources::SpatialGridId // For spatial grid updates
	        },
	        [this, authDataStr, peer]()
	        {
		        // Find player by peer pointer
		        uint32_t playerId = 0;
		        Player* playerPtr = nullptr;
		        uintptr_t peerKey = reinterpret_cast<uintptr_t>(peer);

		        auto it = players.find(peerKey);
		        if (it == players.end())
		        {
			        logger.error("Auth attempt from unknown peer");
			        return;
		        }

		        // Use a local copy of the player
		        Player player = it->second;

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

			        // Schedule peer disconnect after delay
			        threadManager.scheduleTask(
			                [this, peerCopy = peer]()
			                {
				                std::this_thread::sleep_for(std::chrono::seconds(1));
				                enet_peer_disconnect(peerCopy, 0);
			                });

			        return;
		        }

		        // Reset variables for authentication check
		        bool success = false;
		        std::string response;
		        AuthData authData;

		        // Authenticate player - this is already within the resource task so no mutex needed
		        auto authIt = authenticatedPlayers.find(username);
		        if (authIt != authenticatedPlayers.end())
		        {
			        // Verify password
			        std::string passHash = Utils::hashPassword(password);
			        if (authIt->second.passwordHash == passHash)
			        {
				        // Authentication successful
				        playerId = authIt->second.playerId;
				        authData = authIt->second; // Copy the auth data
				        success = true;
				        response = "Authentication successful";

				        // Update player data
				        authIt->second.lastLoginTime = std::time(nullptr);
				        authIt->second.lastIpAddress = player.ipAddress;
				        authIt->second.loginCount++;

				        logger.info("Player authenticated: " + username + " (ID: " + std::to_string(playerId) + ")");
			        }
			        else
			        {
				        // Wrong password
				        success = false;
				        response = "Invalid password";

				        // Update the failed attempts in the stored player
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
			        auto playerIt = players.find(peerKey);
			        if (playerIt != players.end())
			        {
				        playerIt->second.failedAuthAttempts++;
			        }

			        logger.error("Authentication failed: User not found: " + username);
		        }

		        // Failed authentication - send response and return
		        if (!success)
		        {
			        sendAuthResponse(peer, false, response);
			        return;
		        }

		        // Check if player ID is already in use (multiple connections)
		        bool alreadyLoggedIn = players.find(playerId) != players.end();

		        if (alreadyLoggedIn)
		        {
			        sendAuthResponse(peer, false, "Account already logged in");
			        logger.error("Authentication failed: Account already logged in: " + username);
			        return;
		        }

		        // Create a new player entry
		        Player authenticatedPlayer;
		        authenticatedPlayer.id = playerId;
		        authenticatedPlayer.name = username;
		        authenticatedPlayer.position = authData.lastPosition;
		        authenticatedPlayer.lastValidPosition = authData.lastPosition;
		        authenticatedPlayer.peer = peer;
		        authenticatedPlayer.lastUpdateTime = Utils::getCurrentTimeMs();
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

		        // Need to capture these for use outside this task
		        uint32_t finalPlayerId = playerId;
		        std::string finalUsername = username;

		        // Exit the resource critical section before doing communications
		        // We'll capture the values we need and schedule new tasks

		        // Send authentication response
		        sendAuthResponse(peer, true, std::to_string(finalPlayerId));

		        // Broadcast join message - scheduling as a separate task
		        threadManager.scheduleTask([this, finalUsername]() { broadcastSystemMessage(finalUsername + " has joined the game"); });

		        // Plugin loggedin event - scheduling as a separate task
		        // that needs the final player object
		        threadManager.scheduleResourceTask({ GameResources::PlayersId, GameResources::PluginsId },
		                [this, finalPlayerId]()
		                {
			                auto it = players.find(finalPlayerId);
			                if (it != players.end())
			                {
				                pluginManager->dispatchPlayerLogin(it->second);
			                }
		                });
	        });
}

// Handle player registration
void GameServer::handleRegistration(const Player& player, const std::string& username, const std::string& password)
{
	// Validate credentials outside resource locks
	if (username.length() < 3 || username.length() > 20)
	{
		auto packet = PacketManager::createSystemMessage("Username must be between 3 and 20 characters");
		sendPacket(player.peer, *packet, true);
		return;
	}

	if (password.length() < 6)
	{
		auto packet = PacketManager::createSystemMessage("Password must be at least 6 characters");
		sendPacket(player.peer, *packet, true);
		return;
	}

	// Check for invalid characters
	if (username.find(',') != std::string::npos || username.find(' ') != std::string::npos || username.find('\t') != std::string::npos)
	{
		auto packet = PacketManager::createSystemMessage("Username contains invalid characters");
		sendPacket(player.peer, *packet, true);
		return;
	}

	// Now access resources for registration
	threadManager.scheduleResourceTask(
	        {
	                GameResources::AuthId,       // Need access to authenticated players map
	                GameResources::PlayersId,    // Need access to players map
	                GameResources::PeerDataId,   // Need access to peer->data
	                GameResources::SpatialGridId // Need access to spatial grid
	        },
	        [this, player, username, password]()
	        {
		        bool success = false;
		        std::string response;
		        uint32_t newPlayerId = 0;

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

		        if (success)
		        {
			        uint32_t oldKey = player.id == 0 ? reinterpret_cast<uintptr_t>(player.peer) : player.id;

			        // Save auth data immediately - schedule separately to avoid deadlocks
			        threadManager.scheduleResourceTask({ GameResources::AuthId, GameResources::DatabaseId }, [this]() { saveAuthData(); });

			        // Create a new player entry
			        Player registeredPlayer;
			        registeredPlayer.id = newPlayerId;
			        registeredPlayer.name = username;
			        registeredPlayer.position = config.spawnPosition;
			        registeredPlayer.lastValidPosition = config.spawnPosition;
			        registeredPlayer.peer = player.peer;
			        registeredPlayer.lastUpdateTime = Utils::getCurrentTimeMs();
			        registeredPlayer.connectionStartTime = player.connectionStartTime;
			        registeredPlayer.failedAuthAttempts = 0;
			        registeredPlayer.totalBytesReceived = player.totalBytesReceived;
			        registeredPlayer.totalBytesSent = player.totalBytesSent;
			        registeredPlayer.pingMs = player.pingMs;
			        registeredPlayer.isAuthenticated = true;
			        registeredPlayer.isAdmin = false;
			        registeredPlayer.ipAddress = player.ipAddress;

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

			        // Store these for use in subsequent messages
			        ENetPeer* playerPeer = player.peer;

			        // Send registration response with auth response packet
			        auto authResponsePacket = PacketManager::createAuthResponse(true, "Registration successful", newPlayerId);
			        sendPacket(playerPeer, *authResponsePacket, true);

			        // Send welcome message - schedule as a separate task
			        threadManager.scheduleTask(
			                [this, newPlayerId, username]()
			                {
				                // Find the player by ID
				                threadManager.scheduleResourceTask({ GameResources::PlayersId },
				                        [this, newPlayerId, username]()
				                        {
					                        auto it = players.find(newPlayerId);
					                        if (it != players.end())
					                        {
						                        auto welcomePacket = PacketManager::createSystemMessage("Welcome to the game, " + username + "!");
						                        sendPacket(it->second.peer, *welcomePacket, true);
					                        }
				                        });
			                });

			        // Broadcast join message - schedule as a separate task
			        threadManager.scheduleTask(
			                [this, username]()
			                {
				                // Create the message packet
				                auto joinPacket = PacketManager::createSystemMessage(username + " has joined the game for the first time!");

				                // Schedule a resource task to get all players
				                threadManager.scheduleResourceTask({ GameResources::PlayersId },
				                        [this, packet = std::move(joinPacket)]()
				                        {
					                        // Collect all authenticated players' peers
					                        std::vector<ENetPeer*> authenticatedPeers;

					                        for (auto& pair: players)
					                        {
						                        if (pair.second.isAuthenticated && pair.second.peer != nullptr)
						                        {
							                        authenticatedPeers.push_back(pair.second.peer);
						                        }
					                        }

					                        // Release resources before sending packets
					                        threadManager.scheduleTask(
					                                [this, authenticatedPeers, packet = std::move(packet)]()
					                                {
						                                for (ENetPeer* peer: authenticatedPeers)
						                                {
							                                sendPacket(peer, *packet, true);
						                                }
					                                });
				                        });
			                });

			        // Remove old player entry
			        players.erase(oldKey);
		        }
		        else
		        {
			        // Send error message for failed registration
			        auto errorPacket = PacketManager::createSystemMessage("Registration failed: " + response);
			        sendPacket(player.peer, *errorPacket, true);
		        }
	        });
}

void GameServer::syncPlayerStats()
{
	// Use a resource task that requires access to both the players map and peer stats
	threadManager.scheduleResourceTask({ GameResources::PlayersId, GameResources::PeerStatsId },
	        [this]()
	        {
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
	        });
}

void GameServer::handleDeltaPositionUpdate(uint32_t playerId, const std::string& deltaData)
{
	// Use a resource task to safely access and modify the player in the map
	threadManager.scheduleResourceTask({ GameResources::PlayersId, GameResources::SpatialGridId },
	        [this, playerId, deltaData]()
	        {
		        auto playerIt = players.find(playerId);
		        if (playerIt == players.end())
		        {
			        logger.error("Player not found for position update: " + std::to_string(playerId));
			        return;
		        }

		        Player& player = playerIt->second;

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
	        });
}

// Handle sending position to client
void GameServer::handleSendPosition(uint32_t playerId)
{
	// Schedule a resource task that requires Players and SpatialGrid
	threadManager.scheduleResourceTask({ GameResources::PlayersId, GameResources::SpatialGridId },
	        [this, playerId]()
	        {
		        // Find the player in the map
		        auto playerIt = players.find(playerId);
		        if (playerIt == players.end())
		        {
			        logger.error("Player not found for position update: " + std::to_string(playerId));
			        return;
		        }

		        Player& player = playerIt->second;

		        try
		        {
			        // Get current time
			        uint32_t currentTime = Utils::getCurrentTimeMs();

			        // Create a position update packet
			        auto positionPacket = PacketManager::createPositionUpdate(playerId, player.position);

			        // Send the position data to the client (using unreliable transmission for position updates)
			        sendPacket(player.peer, *positionPacket, false);

			        // Log the position update (optional)
			        logger.debug("Sent position update to " + player.name + ": (" + std::to_string(player.position.x) + ", " + std::to_string(player.position.y) + ", " + std::to_string(player.position.z) + ")");

			        // Update timestamp of when we last sent position
			        player.lastPositionUpdateTime = currentTime;

			        // Send the player's position to nearby players
			        std::set<uint32_t> nearbyPlayers = spatialGrid.getNearbyEntities(player.position, config.interestRadius);

			        // Collect peers to send to
			        std::vector<ENetPeer*> nearbyPeers;
			        for (uint32_t nearbyId: nearbyPlayers)
			        {
				        // Skip self
				        if (nearbyId == player.id)
					        continue;

				        auto it = players.find(nearbyId);
				        if (it != players.end() && it->second.peer != nullptr)
				        {
					        nearbyPeers.push_back(it->second.peer);
				        }
			        }

			        // Schedule packet sending as a separate task to avoid holding resources
			        if (!nearbyPeers.empty())
			        {
				        // Need to move the packet to the lambda
				        threadManager.scheduleTask(
				                [this, nearbyPeers, packet = std::move(positionPacket)]()
				                {
					                for (ENetPeer* peer: nearbyPeers)
					                {
						                // Send the position update to each nearby player
						                // We reuse the same packet for all peers
						                sendPacket(peer, *packet, false);
					                }
				                });
			        }
		        }
		        catch (const std::exception& e)
		        {
			        logger.error("Error sending position update to " + player.name + ": " + e.what());
		        }
	        });
}

// Handle chat message
void GameServer::handleChatMessage(const Player& player, const std::string& message)
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

	// Use resource tasks for chat history and broadcasting
	threadManager.scheduleResourceTask({ GameResources::ChatId },
	        [this, playerName = player.name, message]()
	        {
		        // Store in chat history
		        ChatMessage chatMsg;
		        chatMsg.sender = playerName;
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

		        // Update stats - atomic operation, no resource needed
		        stats.chatMessagesSent++;
	        });

	// Broadcast to all players - schedule separately to avoid holding Chat resource
	threadManager.scheduleTask(
	        [this, playerName = player.name, message]()
	        {
		        // Create a properly structured chat message packet
		        auto chatPacket = PacketManager::createChatMessage(playerName, message, true); // true = global message

		        // Schedule a resource task to get all players
		        threadManager.scheduleResourceTask({ GameResources::PlayersId },
		                [this, packet = std::move(chatPacket)]()
		                {
			                // Collect all authenticated players' peers
			                std::vector<ENetPeer*> authenticatedPeers;

			                for (auto& pair: players)
			                {
				                if (pair.second.isAuthenticated && pair.second.peer != nullptr)
				                {
					                authenticatedPeers.push_back(pair.second.peer);
				                }
			                }

			                // Release resources before sending packets
			                threadManager.scheduleTask(
			                        [this, authenticatedPeers, packet = std::move(packet)]()
			                        {
				                        for (ENetPeer* peer: authenticatedPeers)
				                        {
					                        sendPacket(peer, *packet, true); // Use reliable transmission for chat
				                        }
			                        });
		                });
	        });
}

// Handle ping message
void GameServer::handlePingMessage(const Player& player, const std::string& pingData)
{
	try
	{
		uint32_t clientTime = std::stoul(pingData);

		// Create and send a Heartbeat packet with the clientTime
		// TODO: we should have a dedicated PingResponse/Pong packet
		// For now, we'll use a Heartbeat packet which should be defined in your PacketTypes.h

		// Create a heartbeat packet (using the existing packet type)
		auto heartbeatPacket = std::make_unique<GameProtocol::HeartbeatPacket>(clientTime);

		// Send the response
		sendPacket(player.peer, *heartbeatPacket, false); // Unreliable is fine for pings
	}
	catch (const std::exception& e)
	{
		logger.error("Error parsing ping from " + player.name + ": " + e.what());
	}
}

// Handle command message
void GameServer::handleCommandMessage(const Player& player, const std::string& commandStr)
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
		// Create and send a system message packet for the unknown command
		auto packet = PacketManager::createSystemMessage("Unknown command: " + command);
		sendPacket(player.peer, *packet, true);
	}
}

// Send packet to player
void GameServer::sendPacket(ENetPeer* peer, const GameProtocol::Packet& packet, bool reliable)
{
	if (!peer)
		return;

	// Use the packet manager to send the packet
	packetManager.sendPacket(peer,
	        packet,
	        reliable,
	        [this, peer](size_t dataSize)
	        {
		        // Update global stats - atomic operation
		        stats.totalPacketsSent++;
		        stats.totalBytesSent += dataSize;

		        // Update per-peer stats - needs resource access
		        threadManager.scheduleResourceTask({ GameResources::PeerStatsId }, [this, peer, dataSize]() { peerStats[reinterpret_cast<uintptr_t>(peer)].totalBytesSent += dataSize; });
	        });
}

// Updated system message method
void GameServer::sendSystemMessage(const Player& player, const std::string& message)
{
	logger.trace("Sending system message to " + player.name + ": " + message);

	// Create and send system message packet
	auto packet = PacketManager::createSystemMessage(message);
	sendPacket(player.peer, *packet, true); // System messages should be reliable
}

// Updated auth response method
void GameServer::sendAuthResponse(ENetPeer* peer, bool success, const std::string& message, uint32_t playerId)
{
	// Create and send auth response packet
	auto packet = PacketManager::createAuthResponse(success, message, playerId);
	sendPacket(peer, *packet, true); // Auth responses should be reliable
}

// Updated teleport method
void GameServer::sendTeleport(const Player& player, const Position& position)
{
	// Create and send teleport packet
	auto packet = PacketManager::createTeleport(position);
	sendPacket(player.peer, *packet, true); // Teleports should be reliable
}

// Updated broadcast chat message method
void GameServer::broadcastChatMessage(const std::string& sender, const std::string& message)
{
	// Create chat message packet
	auto packet = PacketManager::createChatMessage(sender, message);

	// Schedule a resource task that requires Players resource
	threadManager.scheduleResourceTask({ GameResources::PlayersId },
	        [this, packet = std::move(packet)]()
	        {
		        // Collect all authenticated players' peers
		        std::vector<ENetPeer*> authenticatedPeers;

		        for (auto& pair: players)
		        {
			        if (pair.second.isAuthenticated && pair.second.peer != nullptr)
			        {
				        authenticatedPeers.push_back(pair.second.peer);
			        }
		        }

		        // Release resources before sending packets
		        // Schedule a separate task for sending packets to avoid holding resources
		        threadManager.scheduleTask(
		                [this, authenticatedPeers, packet = std::move(packet)]()
		                {
			                for (ENetPeer* peer: authenticatedPeers)
			                {
				                sendPacket(peer, *packet, true);
			                }
		                });
	        });
}

// Broadcast world state to all players
void GameServer::broadcastWorldState()
{
	// Use resource task that requires Players and SpatialGrid
	threadManager.scheduleResourceTask({ GameResources::PlayersId, GameResources::SpatialGridId },
	        [this]()
	        {
		        for (auto& pair: players)
		        {
			        Player& player = pair.second;

			        // Skip unauthenticated players
			        if (!player.isAuthenticated)
				        continue;

			        // Create world state packet for this player
			        auto worldStatePacket = PacketManager::createWorldState();

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

					        // Add player to world state packet
					        GameProtocol::WorldStatePacket::PlayerInfo info;
					        info.id = otherPlayer.id;
					        info.name = otherPlayer.name;
					        info.position = otherPlayer.position;
					        worldStatePacket->players.push_back(info);
				        }
			        }

			        // Update player's visible set for next time
			        player.visiblePlayers = visibleEntities;

			        // Get a local reference to the peer for sending
			        ENetPeer* playerPeer = player.peer;

			        // Send world state (use unreliable packet for frequent updates)
			        // Do this in a separate task to avoid holding resources during network operations
			        threadManager.scheduleTask([this, playerPeer, packet = std::move(worldStatePacket)]() { sendPacket(playerPeer, *packet, false); });
		        }
	        });
}

// Check for timed out players
void GameServer::checkTimeouts()
{
	threadManager.scheduleResourceTask({ GameResources::PlayersId },
	        [this]()
	        {
		        uint32_t currentTime = Utils::getCurrentTimeMs();
		        std::vector<uint32_t> timeoutPlayers;

		        // First pass: identify timed out players
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

		        // Second pass: handle each timed-out player
		        for (uint32_t id: timeoutPlayers)
		        {
			        auto it = players.find(id);
			        if (it != players.end())
			        {
				        logger.info("Player " + it->second.name + " (ID: " + std::to_string(id) + ") timed out");

				        std::string username = it->second.name;
				        Position lastPos = it->second.position;
				        ENetPeer* playerPeer = it->second.peer;

				        // Schedule saving player data as a separate task
				        threadManager.scheduleResourceTask({ GameResources::AuthId, GameResources::DatabaseId }, [this, username, lastPos]() { savePlayerData(username, lastPos); });

				        // Schedule broadcasting timeout message as a separate task
				        threadManager.scheduleTask([this, username]() { broadcastSystemMessage(username + " timed out"); });

				        // Disconnect the player - do this in a separate task
				        threadManager.scheduleTask([playerPeer]() { enet_peer_disconnect(playerPeer, 0); });

				        // Remove from players map
				        players.erase(it);
			        }
		        }
	        });
}

// Broadcast system message to all players
void GameServer::broadcastSystemMessage(const std::string& message)
{
	// Schedule as a read task since we're not modifying player data
	threadManager.scheduleReadTask({ GameResources::PlayersId },
	        [this, message]()
	        {
		        logger.info("Broadcast: " + message);

		        for (auto& pair: players)
		        {
			        if (pair.second.isAuthenticated)
			        {
				        sendSystemMessage(pair.second, message);
			        }
		        }

		        // Add to chat history (needs write access to Chat)
		        threadManager.scheduleResourceTask({ GameResources::ChatId },
		                [this, message]()
		                {
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
		                });
	        });
}

std::vector<std::string> GameServer::getOnlinePlayerNames()
{
	// Schedule a read task and wait for the result
	return threadManager
	        .scheduleReadTaskWithResult({ GameResources::PlayersId },
	                [this]() -> std::vector<std::string>
	                {
		                std::vector<std::string> names;
		                for (const auto& pair: players)
		                {
			                if (pair.second.isAuthenticated)
			                {
				                names.push_back(pair.second.name);
			                }
		                }
		                return names;
	                })
	        .get(); // Wait for the result
}

// Updated function signature
void GameServer::savePlayerData(const std::string& username, const Position& lastPos)
{
	// Schedule a resource task that requires Auth and Database resources
	threadManager.scheduleResourceTask({ GameResources::AuthId, GameResources::DatabaseId },
	        [this, username, lastPos]()
	        {
		        // Update in-memory structure
		        bool foundInMemory = false;
		        uint32_t playerId = 0;

		        auto authIt = authenticatedPlayers.find(username);
		        if (authIt != authenticatedPlayers.end())
		        {
			        // Save position in memory
			        authIt->second.lastPosition = lastPos;
			        playerId = authIt->second.playerId;
			        foundInMemory = true;
			        logger.debug("Updated in-memory position data for player " + username);
		        }

		        if (!foundInMemory)
		        {
			        logger.error("Failed to find player in memory: " + username);
			        return;
		        }

		        // Save to database if enabled
		        if (config.useDatabase && foundInMemory)
		        {
			        if (dbManager.savePlayerPosition(playerId, lastPos))
			        {
				        logger.debug("Saved position data for player " + username + " to database");
			        }
			        else
			        {
				        logger.error("Failed to save position data to database for player " + username);
			        }
		        }
	        });
}

// Load authentication data from file
void GameServer::loadAuthData()
{
	// Use a resource task with exclusive access to Auth resources
	threadManager.scheduleResourceTask({ GameResources::AuthId, GameResources::DatabaseId },
	        [this]()
	        {
		        logger.info("Loading player authentication data...");

		        // Try to load from database if enabled
		        if (config.useDatabase)
		        {
			        if (dbManager.loadAuthData(authenticatedPlayers, nextPlayerId))
			        {
				        return;
			        }
			        else
			        {
				        logger.error("Failed to load player data from database. Falling back to file.");
				        // Fall through to file-based loading if database fails
			        }
		        }

		        // Original file-based loading code (as fallback)
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
		        }
		        catch (const std::exception& e)
		        {
			        logger.error("Error loading auth data: " + std::string(e.what()));
			        logger.error("Some player accounts may not have been loaded correctly");
		        }

		        file.close();
	        });
}

// Save authentication data to file
void GameServer::saveAuthData()
{
	// Schedule a resource task that requires Auth and Database resources
	threadManager.scheduleResourceTask({ GameResources::AuthId, GameResources::DatabaseId },
	        [this]()
	        {
		        logger.debug("Saving player authentication data...");

		        // Save to database if enabled
		        if (config.useDatabase)
		        {
			        if (dbManager.saveAuthData(authenticatedPlayers))
			        {
				        logger.debug("Saved authentication data for " + std::to_string(authenticatedPlayers.size()) + " players to database");
				        return;
			        }
			        else
			        {
				        logger.error("Failed to save player data to database. Falling back to file.");
				        // Fall through to file-based saving if database fails
			        }
		        }

		        // Original file-based saving code (as fallback)
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
			        logger.debug("Saved authentication data for " + std::to_string(numEntries) + " players to file");
		        }
		        catch (const std::exception& e)
		        {
			        logger.error("Error saving auth data to file: " + std::string(e.what()));
		        }
	        });
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

			// database configuration options
			else if (key == "use_database")
			{
				config.useDatabase = (value == "true" || value == "1");
			}
			else if (key == "db_host")
			{
				config.dbHost = value;
			}
			else if (key == "db_user")
			{
				config.dbUser = value;
			}
			else if (key == "db_password")
			{
				config.dbPassword = value;
			}
			else if (key == "db_name")
			{
				config.dbName = value;
			}
			else if (key == "db_port")
			{
				config.dbPort = std::stoi(value);
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

	// Database configuration
	file << "\n# Database Configuration\n";
	file << "use_database=" << (USE_DATABASE ? "true" : "false") << "\n";
	file << "db_host=" << DB_HOST << "\n";
	file << "db_user=" << DB_USER << "\n";
	file << "db_password=" << DB_PASSWORD << "\n";
	file << "db_name=" << DB_NAME << "\n";
	file << "db_port=" << DB_PORT << "\n";

	file.close();

	logger.info("Default configuration file created");
}

// Initialize command handlers
void GameServer::initializeCommandHandlers()
{
	// Help command - Read-only, doesn't modify any state
	commandHandlers["help"] = [this](const Player& player, const std::vector<std::string>& args)
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

		// Create and send system message packet
		auto packet = PacketManager::createSystemMessage(helpMessage);
		sendPacket(player.peer, *packet, true);
	};

	// Position command - Read-only
	commandHandlers["pos"] = [this](const Player& player, const std::vector<std::string>& args)
	{
		std::stringstream ss;
		ss << "Current position: X=" << player.position.x << " Y=" << player.position.y << " Z=" << player.position.z;

		// Create and send system message packet
		auto packet = PacketManager::createSystemMessage(ss.str());
		sendPacket(player.peer, *packet, true);
	};

	// Teleport command - Requires write access to player and spatial grid
	commandHandlers["tp"] = [this](const Player& player, const std::vector<std::string>& args)
	{
		if (args.size() < 4)
		{
			auto packet = PacketManager::createSystemMessage("Usage: /tp x y z");
			sendPacket(player.peer, *packet, true);
			return;
		}

		try
		{
			float x = std::stof(args[1]);
			float y = std::stof(args[2]);
			float z = std::stof(args[3]);

			// Use a resource task to update the player's position safely
			threadManager.scheduleResourceTask({ GameResources::PlayersId, GameResources::SpatialGridId },
			        [this, playerId = player.id, x, y, z, args]()
			        {
				        // Find the player in the map
				        auto playerIt = players.find(playerId);
				        if (playerIt == players.end())
				        {
					        return; // Player no longer exists
				        }

				        Player& player = playerIt->second;
				        Position oldPos = player.position;
				        Position newPos = { x, y, z };

				        // Update player position
				        player.position = newPos;
				        player.lastValidPosition = newPos;

				        // Update in spatial grid
				        spatialGrid.updateEntity(playerId, oldPos, newPos);

				        // Send teleport packet
				        auto teleportPacket = PacketManager::createTeleport(newPos);
				        sendPacket(player.peer, *teleportPacket, true);

				        // Send confirmation message
				        auto msgPacket = PacketManager::createSystemMessage("Teleported to X=" + args[1] + " Y=" + args[2] + " Z=" + args[3]);
				        sendPacket(player.peer, *msgPacket, true);
			        });
		}
		catch (const std::exception& e)
		{
			auto packet = PacketManager::createSystemMessage("Invalid coordinates: " + std::string(e.what()));
			sendPacket(player.peer, *packet, true);
		}
	};

	// Players list command - Needs read-only access to players
	commandHandlers["players"] = [this](const Player& player, const std::vector<std::string>& args)
	{
		// Use a read task to safely access the players map
		threadManager.scheduleReadTask({ GameResources::PlayersId },
		        [this, playerId = player.id]()
		        {
			        // First player might not exist anymore
			        auto requesterIt = players.find(playerId);
			        if (requesterIt == players.end())
			        {
				        return; // Player no longer exists
			        }

			        // Count authenticated players
			        int authenticatedCount = 0;
			        for (const auto& pair: players)
			        {
				        if (pair.second.isAuthenticated)
				        {
					        authenticatedCount++;
				        }
			        }

			        // Send the header message
			        std::stringstream ss;
			        ss << "Online players (" << authenticatedCount << "):";
			        auto headerPacket = PacketManager::createSystemMessage(ss.str());
			        sendPacket(requesterIt->second.peer, *headerPacket, true);

			        // Send each player in the list
			        for (const auto& pair: players)
			        {
				        if (pair.second.isAuthenticated)
				        {
					        std::string adminTag = pair.second.isAdmin ? " [ADMIN]" : "";
					        auto playerPacket = PacketManager::createSystemMessage("- " + pair.second.name + adminTag);
					        sendPacket(requesterIt->second.peer, *playerPacket, true);
				        }
			        }
		        });
	};

	// Me (action) command - Doesn't modify state, just broadcasts
	commandHandlers["me"] = [this](const Player& player, const std::vector<std::string>& args)
	{
		if (args.size() < 2)
		{
			auto packet = PacketManager::createSystemMessage("Usage: /me action");
			sendPacket(player.peer, *packet, true);
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

		// Broadcast action message - Schedule as a task
		threadManager.scheduleTask(
		        [this, playerName = player.name, action]()
		        {
			        // Create a chat message with a special "ACTION" sender to indicate this is an action
			        auto packet = PacketManager::createChatMessage("ACTION", playerName + " " + action);

			        // Schedule a resource task that requires Players resource
			        threadManager.scheduleResourceTask({ GameResources::PlayersId },
			                [this, packet = std::move(packet)]()
			                {
				                // Collect all authenticated players' peers
				                std::vector<ENetPeer*> authenticatedPeers;

				                for (auto& pair: players)
				                {
					                if (pair.second.isAuthenticated && pair.second.peer != nullptr)
					                {
						                authenticatedPeers.push_back(pair.second.peer);
					                }
				                }

				                // Release resources before sending packets
				                // Schedule a separate task for sending packets to avoid holding resources
				                threadManager.scheduleTask(
				                        [this, authenticatedPeers, packet = std::move(packet)]()
				                        {
					                        for (ENetPeer* peer: authenticatedPeers)
					                        {
						                        sendPacket(peer, *packet, true);
					                        }
				                        });
			                });
		        });
	};

	// Whisper command - Needs read access to find target player
	commandHandlers["w"] = [this](const Player& player, const std::vector<std::string>& args)
	{
		if (args.size() < 3)
		{
			auto packet = PacketManager::createSystemMessage("Usage: /w username message");
			sendPacket(player.peer, *packet, true);
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

		// Use a read task to find the target player
		threadManager.scheduleReadTask({ GameResources::PlayersId },
		        [this, senderPlayerId = player.id, targetName, message]()
		        {
			        // Find sender (who might not exist anymore)
			        auto senderIt = players.find(senderPlayerId);
			        if (senderIt == players.end())
			        {
				        return; // Sender no longer exists
			        }

			        // Find target player
			        Player* targetPlayer = nullptr;
			        for (auto& pair: players)
			        {
				        if (pair.second.isAuthenticated && pair.second.name == targetName)
				        {
					        targetPlayer = &pair.second;
					        break;
				        }
			        }

			        if (targetPlayer != nullptr)
			        {
				        // Send to target - create a chat message with special WHISPER format
				        auto whisperPacket = PacketManager::createChatMessage("*WHISPER* " + senderIt->second.name, message, false); // false for not global
				        sendPacket(targetPlayer->peer, *whisperPacket, true);

				        // Send confirmation to sender
				        auto confirmPacket = PacketManager::createSystemMessage("Whisper to " + targetName + ": " + message);
				        sendPacket(senderIt->second.peer, *confirmPacket, true);
			        }
			        else
			        {
				        auto errorPacket = PacketManager::createSystemMessage("Player not found: " + targetName);
				        sendPacket(senderIt->second.peer, *errorPacket, true);
			        }
		        });
	};

	// Admin commands

	// Kick command - Needs resource access to find and kick player
	commandHandlers["kick"] = [this](const Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			auto packet = PacketManager::createSystemMessage("You don't have permission to use this command");
			sendPacket(player.peer, *packet, true);
			return;
		}

		if (args.size() < 2)
		{
			auto packet = PacketManager::createSystemMessage("Usage: /kick username");
			sendPacket(player.peer, *packet, true);
			return;
		}

		std::string targetName = args[1];

		// Schedule the kick operation
		threadManager.scheduleTask(
		        [this, targetName, adminName = player.name]()
		        {
			        kickPlayer(targetName, adminName);
			        // Note: kickPlayer has been refactored to use resource tasks internally
		        });

		auto packet = PacketManager::createSystemMessage("Attempting to kick player: " + targetName);
		sendPacket(player.peer, *packet, true);
	};

	// Ban command - Similar to kick
	commandHandlers["ban"] = [this](const Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			auto packet = PacketManager::createSystemMessage("You don't have permission to use this command");
			sendPacket(player.peer, *packet, true);
			return;
		}

		if (args.size() < 2)
		{
			auto packet = PacketManager::createSystemMessage("Usage: /ban username");
			sendPacket(player.peer, *packet, true);
			return;
		}

		std::string targetName = args[1];

		// Schedule the ban operation
		threadManager.scheduleTask(
		        [this, targetName, adminName = player.name]()
		        {
			        banPlayer(targetName, adminName);
			        // Note: banPlayer has been refactored to use resource tasks internally
		        });

		auto packet = PacketManager::createSystemMessage("Attempting to ban player: " + targetName);
		sendPacket(player.peer, *packet, true);
	};

	// Broadcast command - Just broadcasts a message
	commandHandlers["broadcast"] = [this](const Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			auto packet = PacketManager::createSystemMessage("You don't have permission to use this command");
			sendPacket(player.peer, *packet, true);
			return;
		}

		if (args.size() < 2)
		{
			auto packet = PacketManager::createSystemMessage("Usage: /broadcast message");
			sendPacket(player.peer, *packet, true);
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

		// Schedule the broadcast
		threadManager.scheduleTask(
		        [this, message, playerName = player.name]()
		        {
			        // Create system message for broadcast
			        auto broadcastPacket = PacketManager::createSystemMessage("[Broadcast] " + message);

			        // Schedule a resource task to get all players
			        threadManager.scheduleResourceTask({ GameResources::PlayersId },
			                [this, packet = std::move(broadcastPacket)]()
			                {
				                // Collect all authenticated players' peers
				                std::vector<ENetPeer*> authenticatedPeers;

				                for (auto& pair: players)
				                {
					                if (pair.second.isAuthenticated && pair.second.peer != nullptr)
					                {
						                authenticatedPeers.push_back(pair.second.peer);
					                }
				                }

				                // Release resources before sending packets
				                threadManager.scheduleTask(
				                        [this, authenticatedPeers, packet = std::move(packet)]()
				                        {
					                        for (ENetPeer* peer: authenticatedPeers)
					                        {
						                        sendPacket(peer, *packet, true);
					                        }
				                        });
			                });

			        logger.info("Admin broadcast from " + playerName + ": " + message);
		        });
	};

	// Set admin command - Needs access to auth data
	commandHandlers["setadmin"] = [this](const Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			auto packet = PacketManager::createSystemMessage("You don't have permission to use this command");
			sendPacket(player.peer, *packet, true);
			return;
		}

		if (args.size() < 2)
		{
			auto packet = PacketManager::createSystemMessage("Usage: /setadmin username");
			sendPacket(player.peer, *packet, true);
			return;
		}

		std::string targetName = args[1];

		// Schedule the operation and get the result
		bool success = threadManager
		                       .scheduleResourceTaskWithResult<bool>({ GameResources::AuthId, GameResources::PlayersId },
		                               [this, targetName]() -> bool
		                               {
			                               return setPlayerAdmin(targetName, true);
			                               // Note: setPlayerAdmin has been refactored to work directly with resources
		                               })
		                       .get(); // Wait for the result

		if (success)
		{
			auto packet = PacketManager::createSystemMessage("Admin status granted to " + targetName);
			sendPacket(player.peer, *packet, true);
		}
		else
		{
			auto packet = PacketManager::createSystemMessage("Failed to set admin status for " + targetName);
			sendPacket(player.peer, *packet, true);
		}
	};

	// Teleport to player command - Needs access to players and spatial grid
	commandHandlers["tpplayer"] = [this](const Player& player, const std::vector<std::string>& args)
	{
		if (!player.isAdmin)
		{
			auto packet = PacketManager::createSystemMessage("You don't have permission to use this command");
			sendPacket(player.peer, *packet, true);
			return;
		}

		if (args.size() < 2)
		{
			auto packet = PacketManager::createSystemMessage("Usage: /tpplayer username");
			sendPacket(player.peer, *packet, true);
			return;
		}

		std::string targetName = args[1];

		// Use a resource task to safely teleport
		threadManager.scheduleResourceTask({ GameResources::PlayersId, GameResources::SpatialGridId },
		        [this, adminId = player.id, targetName]()
		        {
			        // Find the admin player
			        auto adminIt = players.find(adminId);
			        if (adminIt == players.end())
			        {
				        return; // Admin no longer exists
			        }

			        // Find target player
			        Player* targetPlayer = nullptr;
			        for (auto& pair: players)
			        {
				        if (pair.second.isAuthenticated && pair.second.name == targetName)
				        {
					        targetPlayer = &pair.second;
					        break;
				        }
			        }

			        if (targetPlayer != nullptr)
			        {
				        Position oldPos = adminIt->second.position;
				        Position newPos = targetPlayer->position;

				        // Update admin's position
				        adminIt->second.position = newPos;
				        adminIt->second.lastValidPosition = newPos;

				        // Update in spatial grid
				        spatialGrid.updateEntity(adminId, oldPos, newPos);

				        // Send teleport packet
				        auto teleportPacket = PacketManager::createTeleport(newPos);
				        sendPacket(adminIt->second.peer, *teleportPacket, true);

				        // Send confirmation message
				        auto msgPacket = PacketManager::createSystemMessage("Teleported to player: " + targetName);
				        sendPacket(adminIt->second.peer, *msgPacket, true);
			        }
			        else
			        {
				        auto packet = PacketManager::createSystemMessage("Player not found: " + targetName);
				        sendPacket(adminIt->second.peer, *packet, true);
			        }
		        });
	};
}

// Kick a player by name
bool GameServer::kickPlayer(const std::string& playerName, const std::string& adminName)
{
	// We need to be more explicit with the types to avoid template deduction issues
	// Use a regular resource task and track the result manually
	std::promise<bool> resultPromise;
	std::future<bool> resultFuture = resultPromise.get_future();

	// Schedule the task without using scheduleResourceTaskWithResult
	threadManager.scheduleResourceTask({ GameResources::PlayersId },
	        [this, playerName, adminName, &resultPromise]()
	        {
		        bool playerFound = false;

		        for (auto& pair: players)
		        {
			        if (pair.second.isAuthenticated && pair.second.name == playerName)
			        {
				        // Cache player data needed for follow-up tasks
				        Player playerCopy = pair.second;
				        ENetPeer* playerPeer = pair.second.peer;

				        // Log the kick
				        logger.info("Player " + playerName + " kicked by " + adminName);

				        // Schedule disconnecting the player
				        enet_peer_disconnect(playerPeer, 0);

				        playerFound = true;
				        break;
			        }
		        }

		        // Set the result in the promise
		        resultPromise.set_value(playerFound);
	        });

	// Wait for and return the result
	return resultFuture.get();
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
	// Use scheduleReadTask instead of scheduleReadTaskWithResult since we don't need the return value
	threadManager.scheduleReadTask({ GameResources::PlayersId, GameResources::AuthId },
	        [this]()
	        {
		        // Count authenticated players
		        uint32_t authenticatedCount = 0;
		        for (const auto& pair: players)
		        {
			        if (pair.second.isAuthenticated)
			        {
				        authenticatedCount++;
			        }
		        }

		        // Get the number of registered players
		        uint32_t registeredCount = authenticatedPlayers.size();

		        // Log server status
		        logger.info("===== Server Status =====");
		        logger.info("Version: " + std::string(VERSION));
		        logger.info("Uptime: " + Utils::formatUptime(stats.getUptimeSeconds()));
		        logger.info("Port: " + std::to_string(config.port));
		        logger.info("Players: " + std::to_string(authenticatedCount) + " online, " + std::to_string(registeredCount) + " registered");
		        logger.info("Max concurrent players: " + std::to_string(stats.maxConcurrentPlayers));
		        logger.info("Total connections: " + std::to_string(stats.totalConnections));
		        logger.info("Failed auth attempts: " + std::to_string(stats.authFailures));
		        logger.info("Network stats:");
		        logger.info("  Packets: " + std::to_string(stats.totalPacketsSent) + " sent, " + std::to_string(stats.totalPacketsReceived) + " received");
		        logger.info("  Data: " + Utils::formatBytes(stats.totalBytesSent) + " sent, " + Utils::formatBytes(stats.totalBytesReceived) + " received");
		        logger.info("Thread Pool: " + std::to_string(threadManager.getThreadCount()) + " threads");
		        logger.info("=========================");
	        });
}

// Print player list to console
void GameServer::printPlayerList()
{
	// Schedule a read task that requires Players resource
	threadManager.scheduleReadTask({ GameResources::PlayersId },
	        [this]()
	        {
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
					        std::string playerInfo =
					                "- " + pair.second.name + " (ID: " + std::to_string(pair.second.id) + ")" + (pair.second.isAdmin ? " [ADMIN]" : "") + " @ X=" + std::to_string(pair.second.position.x) + " Y=" + std::to_string(pair.second.position.y) + " Z=" + std::to_string(pair.second.position.z) + " | IP: " + pair.second.ipAddress;
					        logger.info(playerInfo);
				        }
			        }
		        }

		        logger.info("=========================");
	        });
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
	logger.info("setadmin <username> - Grant admin status");
	logger.info("removeadmin <username> - Remove admin status");
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
