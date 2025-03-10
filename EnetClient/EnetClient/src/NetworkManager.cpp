#include "NetworkManager.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

// TokenBucket implementation
bool NetworkManager::TokenBucket::consumeTokens(float amount, uint64_t currentTime)
{
	// Update tokens based on time passed
	if (lastUpdate > 0)
	{
		float secondsPassed = (currentTime - lastUpdate) / 1000.0f;
		tokens = min(tokens + rate * secondsPassed, maxTokens);
	}

	lastUpdate = currentTime;

	// Check if we have enough tokens
	if (tokens >= amount)
	{
		tokens -= amount;
		return true;
	}

	return false;
}

// Reset network diagnostics
void NetworkDiagnostics::reset()
{
	avgPingMs = 0;
	minPingMs = UINT32_MAX;
	maxPingMs = 0;
	pingStdDeviation = 0.0f;
	packetLossPercentage = 0;

	disconnectionCount = 0;
	reconnectionCount = 0;
	reconnectionAttempts = 0;
	timeToReconnectMs = 0;

	pingSent = 0;
	pingReceived = 0;
	pingLost = 0;

	packetCreationErrors = 0;
	packetSendErrors = 0;
	enetServiceErrors = 0;
	lastErrorTime = 0;

	connectionStartTime = 0;
	totalConnectedTimeMs = 0;
	lastConnectionTime = 0;
	longestDowntimeMs = 0;

	zoneTransitionCount = 0;
	avgZoneLoadingTimeMs = 0;
	combatPacketSuccessRate = 100.0f;
	avgCombatActionLatencyMs = 0;
	highDensityAreaCount = 0;
	highDensityPositionUpdatesPerSecond = 0.0f;
	serverResponseConsistency = 1.0f;
}

// Constructor with enhanced initialization
NetworkManager::NetworkManager(std::shared_ptr<ThreadManager> threadManager)
      : bandwidthTokenBucket(0, 0), threadManager(threadManager)
{
	logger.debug("Initializing Enhanced NetworkManager");

	// Create a thread manager if none provided
	if (!threadManager)
	{
		this->threadManager = std::make_shared<ThreadManager>();
	}

	// Default packet type configuration
	auto configureMessageTypes = [this]()
	{
		messageTypeConfigs["AUTH:"] = { MessageCategory::CRITICAL, PRIORITY_CRITICAL, false, 1.0f };
		messageTypeConfigs["PING:"] = { MessageCategory::CRITICAL, PRIORITY_HIGH, false, 1.0f };
		messageTypeConfigs["POSITION:"] = { MessageCategory::POSITION, PRIORITY_NORMAL, true, 0.3f };
		messageTypeConfigs["MOVE_DELTA:"] = { MessageCategory::POSITION, PRIORITY_NORMAL, true, 0.3f };
		messageTypeConfigs["CHAT:"] = { MessageCategory::CHAT, PRIORITY_NORMAL, true, 0.7f };
		messageTypeConfigs["COMMAND:"] = { MessageCategory::GAMEPLAY, PRIORITY_HIGH, false, 0.9f };
		messageTypeConfigs["TELEPORT:"] = { MessageCategory::GAMEPLAY, PRIORITY_CRITICAL, false, 1.0f };
	};

	threadManager->scheduleResourceTask({ GameResources::configResourceId }, configureMessageTypes);

	// Default bandwidth allocation
	auto configureBandwidth = [this]()
	{
		categoryTokenBuckets[MessageCategory::CRITICAL] = TokenBucket(50000, 100000); // 50 KB/s, 100 KB burst
		categoryTokenBuckets[MessageCategory::GAMEPLAY] = TokenBucket(40000, 80000);  // 40 KB/s, 80 KB burst
		categoryTokenBuckets[MessageCategory::POSITION] = TokenBucket(30000, 60000);  // 30 KB/s, 60 KB burst
		categoryTokenBuckets[MessageCategory::CHAT] = TokenBucket(20000, 40000);      // 20 KB/s, 40 KB burst
		categoryTokenBuckets[MessageCategory::TELEMETRY] = TokenBucket(5000, 10000);  // 5 KB/s, 10 KB burst
		categoryTokenBuckets[MessageCategory::MISC] = TokenBucket(5000, 10000);       // 5 KB/s, 10 KB burst

		// Global bandwidth limit (150 KB/s, 300 KB burst)
		bandwidthTokenBucket = TokenBucket(150000, 300000);
	};

	threadManager->scheduleResourceTask({ GameResources::bandwidthResourceId }, configureBandwidth);
}

// Destructor with enhanced cleanup
NetworkManager::~NetworkManager()
{
	disconnect(true);

	// Clean up ENet resources
	threadManager->scheduleResourceTask({ GameResources::networkResourceId },
	        [this]()
	        {
		        if (client != nullptr)
		        {
			        enet_host_destroy(client);
			        client = nullptr;
			        enet_deinitialize();
		        }

		        logger.debug("Enhanced NetworkManager destroyed");
	        });

	// Wait for all tasks to complete before destroying
	threadManager->waitForTasks();
}

bool NetworkManager::initialize()
{
	return threadManager
	        ->scheduleResourceTaskWithResult<bool>({ GameResources::networkResourceId },
	                [this]()
	                {
		                logger.debug("Initializing ENet");

		                // Initialize ENet
		                if (enet_initialize() != 0)
		                {
			                logger.error("Failed to initialize ENet");
			                return false;
		                }

		                // Create client host
		                client = enet_host_create(nullptr, // Use nullptr instead of &address
		                        1,                         // Allow 1 outgoing connection
		                        4,                         // Use 4 channels to match server
		                        0,                         // Unlimited incoming bandwidth
		                        0                          // Unlimited outgoing bandwidth
		                );

		                if (client == nullptr)
		                {
			                logger.error("Failed to create ENet client host");
			                enet_deinitialize();
			                return false;
		                }

		                logger.debug("NetworkManager initialized successfully");
		                return true;
	                })
	        .get();
}

std::string NetworkManager::getConnectionStateString() const
{
	std::lock_guard<std::mutex> guard(connectionStateMutex);

	switch (connectionState)
	{
		case ConnectionState::DISCONNECTED:
			return "DISCONNECTED";
		case ConnectionState::CONNECTING:
			return "CONNECTING";
		case ConnectionState::CONNECTED:
			return "CONNECTED";
		case ConnectionState::RECONNECTING:
			return "RECONNECTING";
		case ConnectionState::DISCONNECTING:
			return "DISCONNECTING";
		default:
			return "UNKNOWN";
	}
}

bool NetworkManager::connectToServer(const char* address, uint16_t port)
{
	// Check if we're already trying to connect
	bool expected = false;
	if (!connectionInProgress.compare_exchange_strong(expected, true))
	{
		logger.warning("Connection attempt in progress - ignoring new request");
		return false;
	}

	// Check if we're already connected
	{
		std::lock_guard<std::mutex> guard(connectionStateMutex);
		if (connectionState == ConnectionState::CONNECTED || connectionState == ConnectionState::CONNECTING)
		{
			logger.warning("Connection attempt ignored - already in state: " + getConnectionStateString());
			connectionInProgress.store(false);
			return isConnected;
		}
	}

	logger.debug("Starting connection to server at " + std::string(address) + ":" + std::to_string(port));

	// All ENet operations need to be serialized with enetMutex
	std::lock_guard<std::mutex> enetGuard(enetMutex);

	// Update connection state
	{
		std::lock_guard<std::mutex> guard(connectionStateMutex);
		connectionState = ConnectionState::CONNECTING;
		serverAddress = address;
		serverPort = port;

		// Reset connection-related stats
		successivePingFailures = 0;
		adaptiveTimeoutMultiplier = 1;
		serverResponseTimeout = initialServerResponseTimeout;
	}

	// Create ENet address
	ENetAddress enetAddress;
	if (enet_address_set_host(&enetAddress, address) != 0)
	{
		logger.error("Failed to resolve host address: " + std::string(address));

		std::lock_guard<std::mutex> guard(connectionStateMutex);
		connectionState = ConnectionState::DISCONNECTED;
		connectionInProgress.store(false);
		return false;
	}
	enetAddress.port = port;

	logger.debug("Connecting to server at " + std::string(address) + ":" + std::to_string(port) + "...");

	// Connect to the server
	server = enet_host_connect(client, &enetAddress, 4, 0);

	if (server == nullptr)
	{
		logger.error("Failed to initiate connection to server");

		std::lock_guard<std::mutex> guard(connectionStateMutex);
		connectionState = ConnectionState::DISCONNECTED;
		connectionInProgress.store(false);
		return false;
	}

	// Record start time for diagnostics
	uint64_t connectStartTime = getCurrentTimeMs();

	// Wait for connection establishment with a timeout
	uint64_t startTime = getCurrentTimeMs();
	uint32_t timeout = 5000; // 5 seconds timeout

	bool connectionEstablished = false;

	while (getCurrentTimeMs() - startTime < timeout && !connectionEstablished)
	{
		ENetEvent event;
		// Use a small timeout to check in increments
		int result = enet_host_service(client, &event, 100); // 100ms chunks

		if (result > 0)
		{
			if (event.type == ENET_EVENT_TYPE_CONNECT)
			{
				// Handle successful connection
				{
					std::lock_guard<std::mutex> guard(connectionStateMutex);
					isConnected = true;
					connectionState = ConnectionState::CONNECTED;
					lastNetworkActivity = getCurrentTimeMs();
					lastHeartbeatSent = getCurrentTimeMs();
				}

				// Update diagnostics
				updateConnectionDiagnostics(connectStartTime);

				logger.info("Connected to server at " + serverAddress + ":" + std::to_string(serverPort));
				connectionEstablished = true;
			}
			else if (event.type == ENET_EVENT_TYPE_RECEIVE)
			{
				// Handle any early packets
				enet_packet_destroy(event.packet);
			}
		}
		else if (result < 0)
		{
			// Handle connection error
			logger.error("Error while connecting: " + std::to_string(result));

			{
				std::lock_guard<std::mutex> guard(connectionStateMutex);
				connectionState = ConnectionState::DISCONNECTED;
				if (server != nullptr)
				{
					enet_peer_reset(server);
					server = nullptr;
				}
			}

			{
				std::lock_guard<std::mutex> guard(diagnosticsMutex);
				diagnostics.enetServiceErrors++;
				diagnostics.lastErrorTime = getCurrentTimeMs();
			}

			connectionInProgress.store(false);
			return false;
		}
	}

	// Check if connection timed out
	if (!connectionEstablished)
	{
		logger.warning("Connection to server failed (timeout)");

		{
			std::lock_guard<std::mutex> guard(connectionStateMutex);
			connectionState = ConnectionState::DISCONNECTED;
			if (server != nullptr)
			{
				enet_peer_reset(server);
				server = nullptr;
			}
		}

		connectionInProgress.store(false);
		return false;
	}

	// Now we can process queued packets, but don't block here
	threadManager->scheduleTask([this]() { processQueuedPackets(); });

	connectionInProgress.store(false);
	return true;
}

void NetworkManager::disconnect(bool tellServer)
{
	// Check if we're already disconnected
	if (connectionState == ConnectionState::DISCONNECTED)
	{
		return;
	}

	// Update connection state
	connectionState = ConnectionState::DISCONNECTING;

	bool wasConnected = false;
	ENetPeer* serverToDisconnect = nullptr;

	wasConnected = isConnected;
	serverToDisconnect = server;

	if (wasConnected && serverToDisconnect != nullptr)
	{
		logger.info("Disconnecting from server...");

		// Calculate and record session time
		{
			if (diagnostics.lastConnectionTime > 0)
			{
				uint64_t sessionTime = getCurrentTimeMs() - diagnostics.lastConnectionTime;
				diagnostics.totalConnectedTimeMs += sessionTime;
			}

			diagnostics.disconnectionCount++;
		}

		// If we're initiating the disconnect, send a proper disconnect packet
		enet_peer_disconnect(serverToDisconnect, 0);

		// Wait for disconnect acknowledgment with timeout
		ENetEvent event;
		bool disconnected = false;
		uint64_t disconnectStartTime = getCurrentTimeMs();

		// Wait up to 3 seconds for clean disconnection
		while (tellServer && getCurrentTimeMs() - disconnectStartTime < 3000 && !disconnected)
		{
			while (enet_host_service(client, &event, 100) > 0)
			{
				if (event.type == ENET_EVENT_TYPE_DISCONNECT)
				{
					logger.debug("Disconnection acknowledged by server");
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
			logger.warning("Forcing disconnection!");
			enet_peer_reset(serverToDisconnect);
		}

		server = nullptr;
		isConnected = false;
		connectionState = ConnectionState::DISCONNECTED;
		waitingForPingResponse = false;
		successivePingFailures = 0;
	}
	else
	{
		isConnected = false;
		connectionState = ConnectionState::DISCONNECTED;
		waitingForPingResponse = false;
		successivePingFailures = 0;
	}

	connectionInProgress.store(false);
}

bool NetworkManager::reconnectToServer()
{
	if (connectionState == ConnectionState::DISCONNECTING)
	{
		logger.warning("Reconnection attempt ignored - disconnecting in progress");
		return false;
	}

	// Check if we're connected
	bool isCurrentlyConnected = false;
	{
		std::lock_guard<std::mutex> guard(connectionStateMutex);
		isCurrentlyConnected = isConnected;
	}

	if (isCurrentlyConnected)
	{
		disconnect(true);
	}

	logger.info("Attempting to reconnect to server...");

	{
		std::lock_guard<std::mutex> guard(connectionStateMutex);
		reconnecting = true;
	}

	{
		std::lock_guard<std::mutex> guard(diagnosticsMutex);
		diagnostics.reconnectionAttempts++;
	}

	// Store current values
	std::string currentAddress;
	uint16_t currentPort;

	{
		std::lock_guard<std::mutex> guard(connectionStateMutex);
		currentAddress = serverAddress;
		currentPort = serverPort;
	}

	uint32_t attemptLimit = 0;
	{
		std::lock_guard<std::mutex> configGuard(connectionStateMutex);
		attemptLimit = reconnectAttempts;
	}

	for (uint32_t attempt = 1; attempt <= attemptLimit; attempt++)
	{
		logger.debug("Reconnection attempt " + std::to_string(attempt) + " of " + std::to_string(attemptLimit));

		// Attempt connection
		bool connected = connectToServer(currentAddress.c_str(), currentPort);

		if (connected)
		{
			logger.info("Reconnection successful!");

			{
				std::lock_guard<std::mutex> guard(connectionStateMutex);
				reconnecting = false;
			}

			return true;
		}

		// Wait before next attempt using exponential backoff
		uint32_t backoffTime = 1000 * min(30, 1 << (attempt - 1)); // Cap at 30 seconds
		logger.debug("Waiting " + std::to_string(backoffTime / 1000) + " seconds before next attempt...");
		std::this_thread::sleep_for(std::chrono::milliseconds(backoffTime));
	}

	logger.error("Failed to reconnect after " + std::to_string(attemptLimit) + " attempts");

	{
		std::lock_guard<std::mutex> guard(connectionStateMutex);
		reconnecting = false;
	}

	// Clean very old packets
	cleanExpiredPackets();

	return false;
}

void NetworkManager::updateConnectionDiagnostics(uint64_t connectStartTime)
{
	std::lock_guard<std::mutex> guard(diagnosticsMutex);

	if (diagnostics.connectionStartTime == 0)
	{
		diagnostics.connectionStartTime = getCurrentTimeMs();
	}
	else if (diagnostics.lastConnectionTime > 0)
	{
		diagnostics.reconnectionCount++;
		uint64_t downtime = connectStartTime - diagnostics.lastConnectionTime;
		diagnostics.timeToReconnectMs += downtime;

		// Track longest downtime
		if (downtime > diagnostics.longestDowntimeMs)
		{
			diagnostics.longestDowntimeMs = downtime;
		}
	}

	diagnostics.lastConnectionTime = getCurrentTimeMs();
}

void NetworkManager::cleanExpiredPackets()
{
	uint64_t currentTime = getCurrentTimeMs();

	threadManager->scheduleResourceTask({ GameResources::queueResourceId },
	        [this, currentTime]()
	        {
		        std::priority_queue<QueuedPacket> tempQueue;

		        while (!outgoingQueue.empty())
		        {
			        QueuedPacket packet = outgoingQueue.top();
			        outgoingQueue.pop();

			        if (currentTime - packet.timestamp <= maxQueuedPacketAge * 2)
			        {
				        tempQueue.push(packet);
			        }
		        }

		        outgoingQueue = std::move(tempQueue);
		        logger.debug("Removed expired packets from queue, " + std::to_string(outgoingQueue.size()) + " remaining");
	        });
}

void NetworkManager::sendPacket(std::shared_ptr<GameProtocol::Packet> packet, bool reliable)
{
	// Check connection state locally
	bool currentlyConnected = false;
	{
		std::lock_guard<std::mutex> guard(connectionStateMutex);
		currentlyConnected = isConnected && server != nullptr && connectionState == ConnectionState::CONNECTED;
	}

	if (!currentlyConnected)
	{
		// Check for packet queueing
		bool shouldQueue = queuePacketsDuringDisconnection;

		// Queue the packet if enabled
		if (shouldQueue)
		{
			uint8_t priority = PRIORITY_NORMAL;

			// Determine priority based on packet type
			GameProtocol::PacketType packetType = packet->getType();

			// Set priority based on packet type
			switch (packetType)
			{
				case GameProtocol::PacketType::AuthRequest:
				case GameProtocol::PacketType::Heartbeat:
				case GameProtocol::PacketType::Disconnect:
					priority = PRIORITY_CRITICAL;
					break;

				case GameProtocol::PacketType::ChatMessage:
				case GameProtocol::PacketType::SystemMessage:
				case GameProtocol::PacketType::Whisper:
					priority = PRIORITY_HIGH;
					break;

				case GameProtocol::PacketType::PositionUpdate:
				case GameProtocol::PacketType::DeltaPositionUpdate:
					priority = PRIORITY_NORMAL;
					break;

				default:
					priority = PRIORITY_NORMAL;
					break;
			}

			// Convert to binary and queue
			std::vector<uint8_t> data = packet->serialize();
			std::string dataStr(reinterpret_cast<const char*>(data.data()), data.size());
			queuePacket(dataStr, reliable, priority);
		}
		return;
	}

	// Get server peer
	ENetPeer* serverPeer = getServerPeer();
	if (!serverPeer)
	{
		logger.error("Cannot send packet - server peer not available");
		return;
	}

	// Use the packet manager to send the packet
	packetManager.sendPacket(serverPeer, *packet, reliable);

	// Update statistics
	GameProtocol::PacketType packetType = packet->getType();
	std::string packetTypeName;

	// Convert packet type to string for logging
	switch (packetType)
	{
		case GameProtocol::PacketType::AuthRequest:
			packetTypeName = "AuthRequest";
			break;
		case GameProtocol::PacketType::AuthResponse:
			packetTypeName = "AuthResponse";
			break;
		case GameProtocol::PacketType::ChatMessage:
			packetTypeName = "ChatMessage";
			break;
		case GameProtocol::PacketType::SystemMessage:
			packetTypeName = "SystemMessage";
			break;
		case GameProtocol::PacketType::Command:
			packetTypeName = "Command";
			break;
		case GameProtocol::PacketType::PositionUpdate:
			packetTypeName = "PositionUpdate";
			break;
		case GameProtocol::PacketType::DeltaPositionUpdate:
			packetTypeName = "DeltaPositionUpdate";
			break;
		case GameProtocol::PacketType::Heartbeat:
			packetTypeName = "Heartbeat";
			break;
		default:
			packetTypeName = "Unknown";
			break;
	}

	// Don't log position updates and heartbeats to avoid spam
	if (packetType != GameProtocol::PacketType::PositionUpdate && packetType != GameProtocol::PacketType::DeltaPositionUpdate && packetType != GameProtocol::PacketType::Heartbeat)
	{
		logger.logNetworkEvent("Sent packet of type: " + packetTypeName);
	}
}

void NetworkManager::queuePacket(const std::string& data, bool reliable, uint8_t priority)
{
	threadManager->scheduleResourceTask({ GameResources::queueResourceId },
	        [this, data, reliable, priority]()
	        {
		        // Check if queue has room
		        if (outgoingQueue.size() < maxQueueSize)
		        {
			        // Create queued packet with current time
			        QueuedPacket packet(data, reliable, priority);
			        packet.timestamp = getCurrentTimeMs();
			        outgoingQueue.push(packet);

			        logger.debug("Queued packet (priority=" + std::to_string(priority) + ")");
		        }
		        else
		        {
			        // Queue is full, drop the packet
			        logger.warning("Packet queue full, dropping packet");
		        }
	        });
}

void NetworkManager::sendPacketWithPriority(std::shared_ptr<GameProtocol::Packet> packet, bool reliable, uint8_t priority)
{
	// Check if we're connected for the fast path
	bool currentlyConnected = threadManager->scheduleReadTaskWithResult({ GameResources::networkResourceId }, [this]() -> bool { return isConnected && connectionState == ConnectionState::CONNECTED; }).get();

	if (currentlyConnected)
	{
		// Send directly if connected
		sendPacket(packet, reliable);
		return;
	}

	// If we reach here, we're disconnected but may want to queue
	bool shouldQueue = threadManager->scheduleReadTaskWithResult({ GameResources::configResourceId }, [this]() -> bool { return queuePacketsDuringDisconnection; }).get();

	if (shouldQueue)
	{
		// Serialize the packet to binary data for queueing
		std::vector<uint8_t> serializedData = packet->serialize();
		std::string dataStr(reinterpret_cast<const char*>(serializedData.data()), serializedData.size());

		// Get packet type for logging
		GameProtocol::PacketType packetType = packet->getType();
		std::string packetTypeName = GameProtocol::getPacketTypeName(packetType);

		threadManager->scheduleResourceTask({ GameResources::queueResourceId },
		        [this, dataStr, reliable, priority, packetTypeName]()
		        {
			        // Check if queue has room
			        if (outgoingQueue.size() < maxQueueSize)
			        {
				        // Create queued packet with current time
				        QueuedPacket packet(dataStr, reliable, priority);
				        packet.timestamp = getCurrentTimeMs();
				        outgoingQueue.push(packet);

				        logger.debug("Queued packet during disconnection (priority=" + std::to_string(priority) + ", type=" + packetTypeName + ")");
			        }
			        else
			        {
				        // Queue is full, drop the packet
				        logger.warning("Packet queue full, dropping packet of type: " + packetTypeName);
			        }
		        });
	}
}

void NetworkManager::processQueuedPackets()
{
	// Check connection state locally
	bool canProcessQueue = false;
	{
		std::lock_guard<std::mutex> guard(connectionStateMutex);
		canProcessQueue = isConnected && connectionState == ConnectionState::CONNECTED;
	}

	if (!canProcessQueue)
	{
		logger.debug("Not processing queued packets - not connected");
		return;
	}

	// Get server peer
	ENetPeer* serverPeer = getServerPeer();
	if (!serverPeer)
	{
		logger.warning("Cannot process queued packets - server peer not available");
		return;
	}

	uint64_t currentTime = getCurrentTimeMs();
	uint32_t processLimit = 20; // Process at most 20 packets at once to avoid blocking

	logger.debug("Processing queued packets...");

	// Process packets in batches without blocking on futures
	for (uint32_t batch = 0; batch < processLimit / 5; batch++)
	{
		// Get up to 5 packets from the queue
		std::vector<QueuedPacket> packetsToProcess;

		// Use a mutex to protect the queue instead of scheduling a task
		{
			std::lock_guard<std::mutex> guard(queueMutex);
			uint32_t count = 0;
			while (!outgoingQueue.empty() && count < 5)
			{
				QueuedPacket packet = outgoingQueue.top();
				outgoingQueue.pop();

				// Check if packet is too old
				if (currentTime - packet.timestamp <= maxQueuedPacketAge)
				{
					packetsToProcess.push_back(packet);
				}
				count++;
			}
		}

		// If no packets to process, we're done
		if (packetsToProcess.empty())
		{
			break;
		}

		// Process each packet and convert to the new format
		for (const auto& packet: packetsToProcess)
		{
			// Try to determine the packet type from the legacy message format
			if (packet.message.substr(0, 5) == "AUTH:")
			{
				// Parse the auth message (AUTH:username,password)
				size_t commaPos = packet.message.find(',', 5);
				if (commaPos != std::string::npos)
				{
					std::string username = packet.message.substr(5, commaPos - 5);
					std::string password = packet.message.substr(commaPos + 1);

					// Create and send auth request packet
					auto authPacket = packetManager.createAuthRequest(username, password);
					packetManager.sendPacket(serverPeer, *authPacket, true);
				}
			}
			else if (packet.message.substr(0, 5) == "PING:")
			{
				// Create and send heartbeat packet
				auto heartbeatPacket = packetManager.createHeartbeat(static_cast<uint32_t>(getCurrentTimeMs()));
				packetManager.sendPacket(serverPeer, *heartbeatPacket, true);
			}
			else if (packet.message.substr(0, 9) == "POSITION:")
			{
				// Parse the position message (POSITION:x,y,z)
				try
				{
					std::string posStr = packet.message.substr(9);
					std::vector<float> coords;

					// Parse comma-separated coordinates
					size_t start = 0, end = 0;
					while ((end = posStr.find(',', start)) != std::string::npos)
					{
						coords.push_back(std::stof(posStr.substr(start, end - start)));
						start = end + 1;
					}
					coords.push_back(std::stof(posStr.substr(start))); // Last coordinate

					if (coords.size() >= 3)
					{
						Position pos;
						pos.x = coords[0];
						pos.y = coords[1];
						pos.z = coords[2];

						// Create and send position update packet
						auto posPacket = packetManager.createPositionUpdate(0, pos); // PlayerID not available from queue, use 0
						packetManager.sendPacket(serverPeer, *posPacket, false);
					}
				}
				catch (const std::exception& e)
				{
					logger.error("Failed to parse queued position: " + std::string(e.what()));
				}
			}
			else if (packet.message.substr(0, 10) == "MOVE_DELTA:")
			{
				// Parse delta position message (more complex, would need parsing logic)
				// For simplicity, we'll just send a full position update instead
				logger.debug("Queued delta position updates not supported, skipping");
			}
			else if (packet.message.substr(0, 5) == "CHAT:")
			{
				// Parse chat message (CHAT:message)
				std::string message = packet.message.substr(5);

				// Create and send chat message packet
				// Note: For simplicity, we'll use "Queued" as the sender name
				auto chatPacket = packetManager.createChatMessage("Queued", message);
				packetManager.sendPacket(serverPeer, *chatPacket, true);
			}
			else if (packet.message.substr(0, 8) == "COMMAND:")
			{
				// Parse command message (COMMAND:command args...)
				std::string commandText = packet.message.substr(8);

				// Split into command and args
				std::vector<std::string> args;
				std::string command;

				size_t spacePos = commandText.find(' ');
				if (spacePos != std::string::npos)
				{
					command = commandText.substr(0, spacePos);

					// Parse arguments
					std::string argsStr = commandText.substr(spacePos + 1);
					std::istringstream iss(argsStr);
					std::string arg;

					while (iss >> arg)
					{
						args.push_back(arg);
					}
				}
				else
				{
					command = commandText;
				}

				// Create and send command packet
				auto commandPacket = packetManager.createCommand(command, args);
				packetManager.sendPacket(serverPeer, *commandPacket, true);
			}
			else
			{
				// Unknown packet type, log and skip
				logger.warning("Unknown queued packet type: " + packet.message.substr(0, 20) + "...");
			}
		}

		// Small delay between batches
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	// Log results - get queue count locally
	size_t remaining = 0;
	{
		std::lock_guard<std::mutex> guard(queueMutex);
		remaining = outgoingQueue.size();
	}

	logger.info("Processed queued packets, " + std::to_string(remaining) + " remaining");

	// Schedule processing of remaining packets if needed
	if (remaining > 0)
	{
		threadManager->scheduleTask(
		        [this]()
		        {
			        std::this_thread::sleep_for(std::chrono::milliseconds(50));
			        processQueuedPackets();
		        });
	}
}

void NetworkManager::clearPacketQueue()
{
	threadManager->scheduleResourceTask({ GameResources::queueResourceId },
	        [this]()
	        {
		        std::priority_queue<QueuedPacket> empty;
		        std::swap(outgoingQueue, empty);
		        logger.debug("Packet queue cleared");
	        });
}

size_t NetworkManager::getQueuedPacketCount()
{
	return outgoingQueue.size();
}

// The update method needs refactoring to use ThreadManager
void NetworkManager::update(const std::function<void()>& updatePositionCallback, const std::function<void(const ENetPacket*)>& handlePacketCallback, const std::function<void()>& disconnectCallback)
{
	// Update bandwidth stats (no ENet operations)
	updateBandwidthStats();

	// Check if we're connected
	bool currentlyConnected = false;
	{
		std::lock_guard<std::mutex> guard(connectionStateMutex);
		currentlyConnected = isConnected && client != nullptr && connectionState == ConnectionState::CONNECTED;
	}

	if (!currentlyConnected)
	{
		return;
	}

	// Process network events - needs enetMutex
	std::lock_guard<std::mutex> enetGuard(enetMutex);

	int eventCount = 0;
	int errorCount = 0;
	const int MAX_EVENTS_PER_UPDATE = 10;
	const int MAX_ERRORS_PER_UPDATE = 3;

	// Use a 0 timeout to ensure we don't block
	while (eventCount < MAX_EVENTS_PER_UPDATE && errorCount < MAX_ERRORS_PER_UPDATE)
	{
		ENetEvent event;
		int serviceResult = enet_host_service(client, &event, 0);

		if (serviceResult > 0)
		{
			eventCount++;

			switch (event.type)
			{
				case ENET_EVENT_TYPE_RECEIVE:
				{
					// Update stats
					packetsReceived++;
					bytesReceived += event.packet->dataLength;
					bandwidthStats.bytesReceivedLastSecond += event.packet->dataLength;
					lastNetworkActivity = getCurrentTimeMs();

					// Try to process the packet with the PacketManager
					auto receivedPacket = packetManager.receivePacket(event.packet);

					if (receivedPacket)
					{
						// Successfully parsed as a structured packet
						// logger.info("Received packet of type: " + GameProtocol::getPacketTypeName(receivedPacket->getType())); // Uncomment when debugging

						// Check if it's a packet we handle internally
						switch (receivedPacket->getType())
						{
							case GameProtocol::PacketType::AuthResponse:
								// Handle auth response
								if (authManager)
								{
									// Forward to auth manager
									authManager->processAuthResponse(event.packet->data, event.packet->dataLength, nullptr, nullptr);
								}
								break;

							case GameProtocol::PacketType::Heartbeat:
								// Handle heartbeat as ping response
								if (waitingForPingResponse)
								{
									// Process ping response
									uint64_t currentTime = getCurrentTimeMs();
									uint32_t pingTime = static_cast<uint32_t>(currentTime - lastPingSentTime);
									pingMs = pingTime;
									waitingForPingResponse = false;

									// Reset failure counter on successful ping
									successivePingFailures = 0;

									// Adaptively decrease timeout
									if (adaptiveTimeoutMultiplier > 1 && pingTime * 3 < serverResponseTimeout)
									{
										adaptiveTimeoutMultiplier--;
										serverResponseTimeout = initialServerResponseTimeout * adaptiveTimeoutMultiplier;
										logger.debug("Decreased server response timeout to " + std::to_string(serverResponseTimeout) + "ms");
									}

									// Update ping statistics
									updatePingStatistics(pingTime);
								}
								break;

							default:
								// Pass to the callback handler if provided
								if (handlePacketCallback)
								{
									try
									{
										// Schedule in thread pool to avoid blocking
										// create a copy of the packet before passing it to the callback as the packet could be destroyed before the callback is executed
										ENetPacket* packetCopy = enet_packet_create(event.packet->data, event.packet->dataLength, event.packet->flags);
										threadManager->scheduleNetworkTask([callback = handlePacketCallback, packet = packetCopy]() { callback(packet); });
									}
									catch (std::exception& e)
									{
										logger.error("Error in packet callback: " + std::string(e.what()));
									}
								}
								break;
						}
					}
					else
					{
						// Not a recognized structured packet, pass to callback
						if (handlePacketCallback)
						{
							try
							{
								// Schedule in thread pool to avoid blocking
								// create a copy of the packet before passing it to the callback as the packet could be destroyed before the callback is executed
								ENetPacket* packetCopy = enet_packet_create(event.packet->data, event.packet->dataLength, event.packet->flags);
								threadManager->scheduleNetworkTask([callback = handlePacketCallback, packet = packetCopy]() { callback(packet); });
							}
							catch (std::exception& e)
							{
								logger.error("Error in packet callback: " + std::string(e.what()));
							}
						}
					}

					enet_packet_destroy(event.packet);
					break;
				}

				case ENET_EVENT_TYPE_DISCONNECT:
					logger.warning("Disconnected from server");

					// Schedule disconnect callback
					if (disconnectCallback)
					{
						threadManager->scheduleTask(disconnectCallback);
					}

					// Update connection state
					{
						std::lock_guard<std::mutex> guard(connectionStateMutex);
						isConnected = false;
						connectionState = ConnectionState::DISCONNECTED;
						server = nullptr;
					}

					return; // Exit early on disconnect

				default:
					break;
			}
		}
		else if (serviceResult < 0)
		{
			errorCount++;
			logger.error("ENet service error: " + std::to_string(serviceResult));

			{
				std::lock_guard<std::mutex> guard(diagnosticsMutex);
				diagnostics.enetServiceErrors++;
				diagnostics.lastErrorTime = getCurrentTimeMs();
			}

			if (errorCount >= MAX_ERRORS_PER_UPDATE)
			{
				logger.error("Too many ENet service errors, skipping remaining updates");
				break;
			}
		}
		else
		{
			// No more events to process
			break;
		}
	}

	// Call position update callback without blocking
	if (updatePositionCallback)
	{
		// Check if still connected before scheduling
		bool shouldUpdatePosition = false;
		{
			std::lock_guard<std::mutex> guard(connectionStateMutex);
			shouldUpdatePosition = isConnected && connectionState == ConnectionState::CONNECTED;
		}

		if (shouldUpdatePosition)
		{
			threadManager->scheduleTask(updatePositionCallback);
		}
	}

	// Check heartbeat
	uint64_t currentTime = getCurrentTimeMs();
	bool shouldSendHeartbeat = false;

	{
		std::lock_guard<std::mutex> guard(connectionStateMutex);
		uint64_t timeSinceLastHeartbeat = currentTime - lastHeartbeatSent;
		shouldSendHeartbeat = timeSinceLastHeartbeat >= heartbeatIntervalMs;
	}

	if (shouldSendHeartbeat)
	{
		sendHeartbeat();
	}
}

void NetworkManager::sendHeartbeat()
{
	uint64_t currentTime = getCurrentTimeMs();

	threadManager->scheduleResourceTask({ GameResources::networkResourceId }, [this, currentTime]() { lastHeartbeatSent = currentTime; });

	// Get server peer
	ENetPeer* serverPeer = getServerPeer();
	if (!serverPeer)
	{
		return;
	}

	// Create and send a heartbeat packet
	auto heartbeatPacket = packetManager.createHeartbeat(static_cast<uint32_t>(currentTime));
	packetManager.sendPacket(serverPeer, *heartbeatPacket, false); // Using unreliable for heartbeats
}

void NetworkManager::sendPing()
{
	// First check if we're connected
	bool currentlyConnected = threadManager->scheduleReadTaskWithResult({ GameResources::networkResourceId }, [this]() { return isConnected && connectionState == ConnectionState::CONNECTED; }).get();

	if (!currentlyConnected)
		return;

	uint64_t currentTime = getCurrentTimeMs();
	bool shouldSendPing = false;
	bool shouldLogTimeout = false;
	uint32_t currentPingSequence = 0;

	// Use ThreadManager to check and update ping-related state
	threadManager->scheduleResourceTask({ GameResources::networkResourceId },
	        [this, currentTime, &shouldSendPing, &shouldLogTimeout, &currentPingSequence]()
	        {
		        // Check if it's time to send a ping
		        if (currentTime - lastPingTime >= pingIntervalMs)
		        {
			        lastPingTime = currentTime;
			        lastPingSentTime = currentTime;
			        waitingForPingResponse = true;
			        shouldSendPing = true;
			        currentPingSequence = ++pingSequence; // Increment ping sequence

			        // Update diagnostics
			        diagnostics.pingSent++;
		        }

		        // Check for ping response timeout
		        if (waitingForPingResponse && currentTime - lastPingSentTime > serverResponseTimeout)
		        {
			        shouldLogTimeout = true;
			        waitingForPingResponse = false; // Reset flag to prevent repeated warnings

			        // Update diagnostics
			        diagnostics.pingLost++;

			        // Update packet loss percentage
			        if (diagnostics.pingSent > 0)
			        {
				        diagnostics.packetLossPercentage = (diagnostics.pingLost * 100) / diagnostics.pingSent;
			        }

			        // Adaptively increase timeout for the next attempt
			        if (adaptiveTimeoutMultiplier < 4)
			        { // Cap multiplier at 4x
				        adaptiveTimeoutMultiplier++;
				        uint32_t newTimeout = initialServerResponseTimeout * adaptiveTimeoutMultiplier;
				        if (newTimeout > maxServerResponseTimeout)
				        {
					        newTimeout = maxServerResponseTimeout;
				        }
				        serverResponseTimeout = newTimeout;
				        logger.debug("Increased server response timeout to " + std::to_string(serverResponseTimeout) + "ms");
			        }

			        // Increment failure counter
			        successivePingFailures++;

			        if (successivePingFailures >= maxPingFailures)
			        {
				        networkDegraded = true;
				        logger.warning("Network quality degraded - " + std::to_string(successivePingFailures) + " consecutive ping failures");

				        if (successivePingFailures >= maxPingFailures * 2)
				        {
					        // Consider flagging for disconnect at this point
					        logger.error("Connection appears to be lost - marking as degraded");
				        }
			        }
		        }
	        });

	// Send ping with sequence number if needed
	if (shouldSendPing)
	{
		// Get server peer
		ENetPeer* serverPeer = getServerPeer();
		if (!serverPeer)
		{
			return;
		}

		// Create heartbeat packet with current time as client time
		auto heartbeatPacket = packetManager.createHeartbeat(static_cast<uint32_t>(currentTime));

		// Send the packet reliably for better detection
		packetManager.sendPacket(serverPeer, *heartbeatPacket, true);
	}

	// Handle ping timeout
	if (shouldLogTimeout)
	{
		logger.warning("Ping response timeout - Server may have connectivity issues");
	}
}

// Connection health checking
void NetworkManager::checkConnectionHealth()
{
	// First check if conditions allow for checking connection health
	bool shouldCheckHealth = threadManager->scheduleReadTaskWithResult({ GameResources::networkResourceId }, [this]() { return isConnected && connectionState == ConnectionState::CONNECTED && !reconnecting; }).get();

	if (!shouldCheckHealth)
	{
		return;
	}

	uint64_t currentTime = getCurrentTimeMs();
	bool shouldDisconnect = false;

	// Check connection health - requires resource access
	threadManager->scheduleResourceTask({ GameResources::networkResourceId },
	        [this, currentTime, &shouldDisconnect]()
	        {
		        // Check if we've received any responses from server recently
		        if (currentTime - lastNetworkActivity > serverResponseTimeout)
		        {
			        logger.error("No server activity for " + std::to_string(serverResponseTimeout / 1000) + " seconds");
			        isConnected = false;
			        connectionState = ConnectionState::DISCONNECTED;
			        shouldDisconnect = true;
		        }

		        // If we're in a zone transition, be more lenient
		        if (inZoneTransition && currentTime - zoneTransitionStartTime < zoneTransitionTimeout)
		        {
			        // Don't disconnect during zone transition unless timeout is excessive
			        if (shouldDisconnect)
			        {
				        logger.warning("Network timeout during zone transition - extending timeout");
				        shouldDisconnect = false;
			        }
		        }

		        // Check for severely degraded network
		        if (networkDegraded && packetLossEstimate > 0.25f && successivePingFailures >= maxPingFailures * 3)
		        {
			        logger.error("Network severely degraded - packet loss: " + std::to_string(packetLossEstimate * 100.0f) + "% - disconnecting");
			        isConnected = false;
			        connectionState = ConnectionState::DISCONNECTED;
			        shouldDisconnect = true;
		        }
	        });

	if (shouldDisconnect)
	{
		disconnect(false); // Whats the point in telling the server if we cant even reach it correctly?
	}
}

// Bandwidth statistics update
void NetworkManager::updateBandwidthStats()
{
	uint64_t currentTime = getCurrentTimeMs();

	// Update bandwidth statistics - requires bandwidth resource access
	threadManager->scheduleResourceTask({ GameResources::bandwidthResourceId },
	        [this, currentTime]()
	        {
		        // Check if we've moved to a new second
		        if (bandwidthStats.currentSecondStart == 0)
		        {
			        bandwidthStats.currentSecondStart = currentTime;
		        }
		        else if (currentTime - bandwidthStats.currentSecondStart >= 1000)
		        {
			        // We've completed a second, update history
			        bandwidthStats.sendRateHistory.push_back(bandwidthStats.bytesSentLastSecond);
			        bandwidthStats.receiveRateHistory.push_back(bandwidthStats.bytesReceivedLastSecond);

			        // Trim history if needed
			        while (bandwidthStats.sendRateHistory.size() > bandwidthStats.maxHistorySize)
			        {
				        bandwidthStats.sendRateHistory.erase(bandwidthStats.sendRateHistory.begin());
			        }
			        while (bandwidthStats.receiveRateHistory.size() > bandwidthStats.maxHistorySize)
			        {
				        bandwidthStats.receiveRateHistory.erase(bandwidthStats.receiveRateHistory.begin());
			        }

			        // Calculate averages
			        if (!bandwidthStats.sendRateHistory.empty())
			        {
				        float sum = 0;
				        for (size_t rate: bandwidthStats.sendRateHistory)
				        {
					        sum += rate;
				        }
				        bandwidthStats.averageSendRateBps = sum / bandwidthStats.sendRateHistory.size();
			        }

			        if (!bandwidthStats.receiveRateHistory.empty())
			        {
				        float sum = 0;
				        for (size_t rate: bandwidthStats.receiveRateHistory)
				        {
					        sum += rate;
				        }
				        bandwidthStats.averageReceiveRateBps = sum / bandwidthStats.receiveRateHistory.size();
			        }

			        // Reset for next second
			        bandwidthStats.bytesSentLastSecond = 0;
			        bandwidthStats.bytesReceivedLastSecond = 0;
			        bandwidthStats.currentSecondStart = currentTime;

			        // Check if we need to enable throttling
			        if (outgoingBandwidthLimit > 0 && bandwidthStats.averageSendRateBps > outgoingBandwidthLimit * 0.9f)
			        {
				        if (!bandwidthThrottlingEnabled)
				        {
					        logger.warning("Bandwidth usage high, enabling throttling");
					        bandwidthThrottlingEnabled = true;
				        }
			        }
			        else if (bandwidthThrottlingEnabled && bandwidthStats.averageSendRateBps < throttledOutgoingLimit * 0.7f)
			        {
				        logger.info("Bandwidth usage normalized, disabling throttling");
				        bandwidthThrottlingEnabled = false;
			        }
		        }
	        });
}

// Check if packet send is allowed based on bandwidth constraints
bool NetworkManager::isPacketSendAllowed(const std::string& message, float size)
{
	// Access bandwidth settings without blocking
	std::lock_guard<std::mutex> guard(bandwidthMutex);

	if (outgoingBandwidthLimit == 0)
	{
		return true; // No limit set
	}

	uint64_t currentTime = getCurrentTimeMs();

	// Get message type config
	MessageTypeConfig config = getMessageConfig(message);

	// Critical messages always pass
	if (config.category == MessageCategory::CRITICAL)
	{
		return true;
	}

	// Check if we need to throttle this message
	if (bandwidthThrottlingEnabled && config.canThrottle)
	{
		// Check category token bucket first
		TokenBucket& categoryBucket = categoryTokenBuckets[config.category];
		if (!categoryBucket.consumeTokens(size * config.throttleMultiplier, currentTime))
		{
			return false;
		}
	}

	// Check global token bucket
	return bandwidthTokenBucket.consumeTokens(size, currentTime);
}

// Get message configuration
MessageTypeConfig NetworkManager::getMessageConfig(const std::string& message)
{
	// Default configuration
	MessageTypeConfig defaultConfig = { MessageCategory::MISC, PRIORITY_NORMAL, true, 0.5f };

	// Find the appropriate configuration for this message type
	for (const auto& configPair: messageTypeConfigs)
	{
		if (!configPair.first.empty() && message.find(configPair.first) == 0)
		{
			return configPair.second;
		}
	}

	return defaultConfig;
}

// Configure a message type
void NetworkManager::configureMessageType(const std::string& prefix, uint8_t priority, bool canThrottle, float throttleMultiplier)
{
	threadManager->scheduleResourceTask({ GameResources::configResourceId },
	        [this, prefix, priority, canThrottle, throttleMultiplier]()
	        {
		        // Determine message category based on prefix
		        MessageCategory category;
		        if (prefix == "AUTH:" || prefix == "PING:" || prefix == "PONG:" || prefix == "HEARTBEAT")
		        {
			        category = MessageCategory::CRITICAL;
		        }
		        else if (prefix == "POSITION:" || prefix == "MOVE_DELTA:")
		        {
			        category = MessageCategory::POSITION;
		        }
		        else if (prefix == "CHAT:")
		        {
			        category = MessageCategory::CHAT;
		        }
		        else if (prefix == "TELEMETRY:")
		        {
			        category = MessageCategory::TELEMETRY;
		        }
		        else if (prefix == "COMMAND:" || prefix == "ACTION:" || prefix == "COMBAT:")
		        {
			        category = MessageCategory::GAMEPLAY;
		        }
		        else
		        {
			        category = MessageCategory::MISC;
		        }

		        messageTypeConfigs[prefix] = { category, priority, canThrottle, throttleMultiplier };

		        logger.debug("Configured message type: prefix=\"" + prefix + "\", category=" + std::to_string(static_cast<int>(category)) + ", priority=" + std::to_string(priority) + ", canThrottle=" + (canThrottle ? "true" : "false") + ", throttleMultiplier=" + std::to_string(throttleMultiplier));
	        });
}

// Configure bandwidth management
void NetworkManager::configureBandwidthManagement(size_t outgoingLimitBps, size_t throttledLimitBps, bool enableThrottling)
{
	threadManager->scheduleResourceTask({ GameResources::bandwidthResourceId },
	        [this, outgoingLimitBps, throttledLimitBps, enableThrottling]()
	        {
		        outgoingBandwidthLimit = outgoingLimitBps;
		        throttledOutgoingLimit = throttledLimitBps;
		        bandwidthThrottlingEnabled = enableThrottling;

		        // Initialize token bucket with new rate
		        bandwidthTokenBucket = TokenBucket((float) outgoingLimitBps, (float) outgoingLimitBps); // 1 second burst capacity

		        logger.debug("Bandwidth management configured: limit=" + std::to_string(outgoingLimitBps) + "bps, throttled=" + std::to_string(throttledLimitBps) + "bps, throttling=" + (enableThrottling ? "enabled" : "disabled"));

		        // Initialize token buckets for message categories with appropriate allocations
		        float criticalShare = 0.4f;   // 40% for critical messages
		        float gameplayShare = 0.3f;   // 30% for gameplay
		        float positionShare = 0.15f;  // 15% for position
		        float chatShare = 0.1f;       // 10% for chat
		        float telemetryShare = 0.02f; // 2% for telemetry
		        float miscShare = 0.03f;      // 3% for misc

		        categoryTokenBuckets[MessageCategory::CRITICAL] = TokenBucket(outgoingLimitBps * criticalShare, outgoingLimitBps * criticalShare * 1.5f);
		        categoryTokenBuckets[MessageCategory::GAMEPLAY] = TokenBucket(outgoingLimitBps * gameplayShare, outgoingLimitBps * gameplayShare * 1.5f);
		        categoryTokenBuckets[MessageCategory::POSITION] = TokenBucket(outgoingLimitBps * positionShare, outgoingLimitBps * positionShare * 1.5f);
		        categoryTokenBuckets[MessageCategory::CHAT] = TokenBucket(outgoingLimitBps * chatShare, outgoingLimitBps * chatShare * 1.5f);
		        categoryTokenBuckets[MessageCategory::TELEMETRY] = TokenBucket(outgoingLimitBps * telemetryShare, outgoingLimitBps * telemetryShare * 1.5f);
		        categoryTokenBuckets[MessageCategory::MISC] = TokenBucket(outgoingLimitBps * miscShare, outgoingLimitBps * miscShare * 1.5f);
	        });
}

// Configure adaptive timeout
void NetworkManager::configureAdaptiveTimeout(uint32_t initial, uint32_t max, uint32_t pingFailures)
{
	threadManager->scheduleResourceTask({ GameResources::networkResourceId },
	        [this, initial, max, pingFailures]()
	        {
		        initialServerResponseTimeout = initial;
		        maxServerResponseTimeout = max;
		        maxPingFailures = pingFailures;
		        serverResponseTimeout = initial;

		        logger.debug("Configured adaptive timeout: initial=" + std::to_string(initial) + "ms, max=" + std::to_string(max) + "ms, maxPingFailures=" + std::to_string(pingFailures));
	        });
}

// Configure packet queueing
void NetworkManager::setPacketQueueing(bool enabled, size_t maxSize, uint32_t maxAgeMs)
{
	threadManager->scheduleResourceTask({ GameResources::queueResourceId, GameResources::configResourceId },
	        [this, enabled, maxSize, maxAgeMs]()
	        {
		        queuePacketsDuringDisconnection = enabled;
		        maxQueueSize = maxSize;
		        maxQueuedPacketAge = maxAgeMs;

		        logger.debug("Packet queueing " + std::string(enabled ? "enabled" : "disabled") + ", maxSize=" + std::to_string(maxSize) + ", maxAge=" + std::to_string(maxAgeMs) + "ms");
	        });
}

// Configure message compression
void NetworkManager::configureCompression(bool enabled, uint32_t minSize, float minRatio)
{
	threadManager->scheduleResourceTask({ GameResources::configResourceId },
	        [this, enabled, minSize, minRatio]()
	        {
		        compressionEnabled = enabled;
		        minSizeForCompression = minSize;
		        minCompressionRatio = minRatio;

		        logger.debug("Compression " + std::string(enabled ? "enabled" : "disabled") + ", minSize=" + std::to_string(minSize) + ", minRatio=" + std::to_string(minRatio));
	        });
}

// Compress a message
std::string NetworkManager::compressMessage(const std::string& message, float* ratio)
{
	// This method doesn't need ThreadManager as it doesn't access shared state
	// It processes the input string and returns a result

	if (message.empty())
	{
		if (ratio)
			*ratio = 1.0f;
		return message;
	}

	std::string result;
	result.reserve(message.length()); // Reserve space for worst case

	char currentChar = message[0];
	int count = 1;

	for (size_t i = 1; i < message.length(); i++)
	{
		if (message[i] == currentChar && count < 255)
		{
			count++;
		}
		else
		{
			// Output run
			if (count >= 4)
			{
				// Worth compressing
				result.push_back(0); // Special marker
				result.push_back(currentChar);
				result.push_back(static_cast<char>(count));
			}
			else
			{
				// Not worth compressing
				for (int j = 0; j < count; j++)
				{
					result.push_back(currentChar);
				}
			}

			// Start new run
			currentChar = message[i];
			count = 1;
		}
	}

	// Handle last run
	if (count >= 4)
	{
		result.push_back(0);
		result.push_back(currentChar);
		result.push_back(static_cast<char>(count));
	}
	else
	{
		for (int j = 0; j < count; j++)
		{
			result.push_back(currentChar);
		}
	}

	// Calculate ratio and set output parameter
	float compressionRatio = static_cast<float>(result.length()) / message.length();
	if (ratio)
		*ratio = compressionRatio;

	// Only return compressed version if it's actually smaller
	if (compressionRatio < 1.0f)
	{
		return result;
	}
	else
	{
		// If compression didn't help, return original
		if (ratio)
			*ratio = 1.0f;
		return message;
	}
}

// Decompress a message
std::string NetworkManager::decompressMessage(const std::string& compressedData)
{
	// Like compressMessage, this method doesn't need ThreadManager
	// as it processes an input string and returns a result without accessing shared state

	if (compressedData.empty())
	{
		return compressedData;
	}

	std::string result;
	result.reserve(compressedData.length() * 2); // Reserve space for possible expansion

	for (size_t i = 0; i < compressedData.length(); i++)
	{
		if (compressedData[i] == 0 && i + 2 < compressedData.length())
		{
			// This is a compressed run
			char c = compressedData[i + 1];
			int count = static_cast<unsigned char>(compressedData[i + 2]);

			for (int j = 0; j < count; j++)
			{
				result.push_back(c);
			}

			i += 2; // Skip the character and count bytes
		}
		else
		{
			// This is a literal character
			result.push_back(compressedData[i]);
		}
	}

	return result;
}

// Update ping statistics
void NetworkManager::updatePingStatistics(uint32_t pingTime)
{
	threadManager->scheduleResourceTask({ GameResources::networkResourceId },
	        [this, pingTime]()
	        {
		        // Update ping history
		        pingHistory.push_back(pingTime);
		        if (pingHistory.size() > pingHistorySize)
		        {
			        pingHistory.erase(pingHistory.begin());
		        }

		        // Update diagnostics
		        diagnostics.pingReceived++;

		        // Update min/max ping
		        if (pingTime < diagnostics.minPingMs)
		        {
			        diagnostics.minPingMs = pingTime;
		        }
		        if (pingTime > diagnostics.maxPingMs)
		        {
			        diagnostics.maxPingMs = pingTime;
		        }

		        // Update average ping (weighted average)
		        diagnostics.avgPingMs = ((diagnostics.avgPingMs * (diagnostics.pingReceived - 1)) + pingTime) / diagnostics.pingReceived;

		        // Calculate jitter and estimate packet loss
		        calculateJitter();
		        estimatePacketLoss();
	        });
}

// Calculate network jitter
void NetworkManager::calculateJitter()
{
	// This method should be called from within a resource task
	// that already has access to the GameResources::networkResourceId

	if (pingHistory.size() < 2)
	{
		jitter = 0.0f;
		return;
	}

	// Calculate mean ping
	float sum = 0.0f;
	for (uint32_t ping: pingHistory)
	{
		sum += ping;
	}
	float mean = sum / pingHistory.size();

	// Calculate variance
	float variance = 0.0f;
	for (uint32_t ping: pingHistory)
	{
		variance += (float) pow(ping - mean, 2);
	}
	variance /= pingHistory.size();

	// Jitter is the standard deviation
	jitter = static_cast<float>(sqrt(variance));
	diagnostics.pingStdDeviation = jitter;
}

// Estimate packet loss
void NetworkManager::estimatePacketLoss()
{
	// This method should be called from within a resource task

	if (diagnostics.pingSent == 0)
	{
		packetLossEstimate = 0.0f;
		return;
	}

	packetLossEstimate = static_cast<float>(diagnostics.pingLost) / diagnostics.pingSent;
	diagnostics.packetLossPercentage = static_cast<uint32_t>(packetLossEstimate * 100.0f);
}

// Process a received packet
bool NetworkManager::processReceivedPacket(const void* packetData, size_t packetLength)
{
	// Create a span from the packet data
	std::span<const uint8_t> data(static_cast<const uint8_t*>(packetData), packetLength);

	// Try to deserialize the packet
	auto packet = GameProtocol::deserializePacket(data);

	// If deserialization failed, return false
	if (!packet)
	{
		return false;
	}

	// Check packet type
	if (packet->getType() == GameProtocol::PacketType::AuthResponse)
	{
		if (authManager)
		{
			// Forward to auth manager and return true if processed
			authManager->processAuthResponse(packetData, packetLength, nullptr, nullptr);
			return true;
		}
	}

	// Not an auth packet or no auth manager to handle it
	return false;
}

// Analyze connection quality
std::string NetworkManager::analyzeConnectionQuality()
{
	return threadManager
	        ->scheduleReadTaskWithResult({ GameResources::networkResourceId, GameResources::bandwidthResourceId },
	                [this]()
	                {
		                std::stringstream analysis;
		                analysis << "=== MMO CLIENT NETWORK ANALYSIS ===\n\n";

		                // Ping quality analysis
		                if (diagnostics.avgPingMs < 50)
		                {
			                analysis << "Excellent latency (" << diagnostics.avgPingMs << "ms)\n";
		                }
		                else if (diagnostics.avgPingMs < 100)
		                {
			                analysis << "Good latency (" << diagnostics.avgPingMs << "ms)\n";
		                }
		                else if (diagnostics.avgPingMs < 150)
		                {
			                analysis << "Moderate latency (" << diagnostics.avgPingMs << "ms)\n";
		                }
		                else
		                {
			                analysis << "Poor latency (" << diagnostics.avgPingMs << "ms)\n";
		                }

		                // Jitter analysis
		                if (diagnostics.pingStdDeviation < 10)
		                {
			                analysis << "Excellent stability (jitter: " << diagnostics.pingStdDeviation << "ms)\n";
		                }
		                else if (diagnostics.pingStdDeviation < 25)
		                {
			                analysis << "Good stability (jitter: " << diagnostics.pingStdDeviation << "ms)\n";
		                }
		                else if (diagnostics.pingStdDeviation < 50)
		                {
			                analysis << "Moderate stability (jitter: " << diagnostics.pingStdDeviation << "ms)\n";
		                }
		                else
		                {
			                analysis << "Poor stability (jitter: " << diagnostics.pingStdDeviation << "ms)\n";
		                }

		                // Packet loss analysis
		                if (diagnostics.packetLossPercentage < 1)
		                {
			                analysis << "Excellent reliability (packet loss: " << diagnostics.packetLossPercentage << "%)\n";
		                }
		                else if (diagnostics.packetLossPercentage < 3)
		                {
			                analysis << "Good reliability (packet loss: " << diagnostics.packetLossPercentage << "%)\n";
		                }
		                else if (diagnostics.packetLossPercentage < 8)
		                {
			                analysis << "Moderate reliability (packet loss: " << diagnostics.packetLossPercentage << "%)\n";
		                }
		                else
		                {
			                analysis << "Poor reliability (packet loss: " << diagnostics.packetLossPercentage << "%)\n";
		                }

		                // Connection stability analysis
		                if (diagnostics.disconnectionCount == 0)
		                {
			                analysis << "Perfect connection stability (no disconnections)\n";
		                }
		                else if (diagnostics.disconnectionCount <= 2)
		                {
			                analysis << "Good connection stability (" << diagnostics.disconnectionCount << " disconnections)\n";
		                }
		                else if (diagnostics.disconnectionCount <= 5)
		                {
			                analysis << "Moderate connection stability (" << diagnostics.disconnectionCount << " disconnections)\n";
		                }
		                else
		                {
			                analysis << "Poor connection stability (" << diagnostics.disconnectionCount << " disconnections)\n";
		                }

		                // Bandwidth usage analysis
		                float bandwidthUsagePercent = 0;
		                if (outgoingBandwidthLimit > 0)
		                {
			                bandwidthUsagePercent = (static_cast<float>(bandwidthStats.averageSendRateBps) * 100.0f) / static_cast<float>(outgoingBandwidthLimit);

			                if (bandwidthUsagePercent < 50)
			                {
				                analysis << "Good bandwidth usage (" << bandwidthUsagePercent << "% of limit)\n";
			                }
			                else if (bandwidthUsagePercent < 80)
			                {
				                analysis << "Moderate bandwidth usage (" << bandwidthUsagePercent << "% of limit)\n";
			                }
			                else
			                {
				                analysis << "High bandwidth usage (" << bandwidthUsagePercent << "% of limit)\n";
			                }
		                }

		                // MMO-specific analysis
		                if (diagnostics.zoneTransitionCount > 0)
		                {
			                analysis << "\n--- MMO-Specific Metrics ---\n";
			                analysis << "Zone Transitions: " << diagnostics.zoneTransitionCount << "\n";
			                analysis << "Average Zone Loading Time: " << diagnostics.avgZoneLoadingTimeMs << "ms\n";

			                if (diagnostics.combatPacketSuccessRate >= 99)
			                {
				                analysis << "Excellent combat performance (" << diagnostics.combatPacketSuccessRate << "% success)\n";
			                }
			                else if (diagnostics.combatPacketSuccessRate >= 95)
			                {
				                analysis << "Good combat performance (" << diagnostics.combatPacketSuccessRate << "% success)\n";
			                }
			                else if (diagnostics.combatPacketSuccessRate >= 90)
			                {
				                analysis << "Moderate combat performance (" << diagnostics.combatPacketSuccessRate << "% success)\n";
			                }
			                else
			                {
				                analysis << "Poor combat performance (" << diagnostics.combatPacketSuccessRate << "% success)\n";
			                }
		                }

		                // Overall recommendation
		                analysis << "\n=== OVERALL ASSESSMENT ===\n";

		                int issues = 0;
		                if (diagnostics.avgPingMs >= 150)
			                issues++;
		                if (diagnostics.pingStdDeviation >= 50)
			                issues++;
		                if (diagnostics.packetLossPercentage >= 8)
			                issues++;
		                if (diagnostics.disconnectionCount > 5)
			                issues++;
		                if (bandwidthUsagePercent >= 80)
			                issues++;
		                if (diagnostics.combatPacketSuccessRate < 90)
			                issues++;

		                if (issues == 0)
		                {
			                analysis << "EXCELLENT - Your connection is performing optimally for MMO gameplay.\n";
		                }
		                else if (issues == 1)
		                {
			                analysis << "GOOD - Your connection is performing well with minor issues.\n";
		                }
		                else if (issues == 2)
		                {
			                analysis << "FAIR - Your connection has some issues that could impact MMO experience.\n";
		                }
		                else
		                {
			                analysis << "POOR - Your connection has multiple significant issues for MMO gameplay.\n";
		                }

		                // Recommendations
		                analysis << "\n=== RECOMMENDATIONS ===\n";

		                if (diagnostics.avgPingMs >= 150)
		                {
			                analysis << "- Consider using a wired connection instead of WiFi\n";
			                analysis << "- Choose a server closer to your geographical location\n";
		                }

		                if (diagnostics.pingStdDeviation >= 50)
		                {
			                analysis << "- Check for background applications using your bandwidth\n";
			                analysis << "- Try restarting your router\n";
		                }

		                if (diagnostics.packetLossPercentage >= 8)
		                {
			                analysis << "- Check your network hardware for issues\n";
			                analysis << "- Contact your ISP if issues persist\n";
		                }

		                if (diagnostics.disconnectionCount > 5)
		                {
			                analysis << "- Check for WiFi interference or signal strength issues\n";
			                analysis << "- Verify your firewall settings\n";
		                }

		                if (bandwidthUsagePercent >= 80)
		                {
			                analysis << "- Reduce position update frequency in game settings\n";
			                analysis << "- Close other applications using your network\n";
		                }

		                if (diagnostics.combatPacketSuccessRate < 90)
		                {
			                analysis << "- Prioritize combat packets in your network settings\n";
			                analysis << "- Consider lowering graphics settings to free up CPU/bandwidth\n";
		                }

		                return analysis.str();
	                })
	        .get();
}

// Generate a diagnostics report
std::string NetworkManager::generateDiagnosticsReport()
{
	return threadManager
	        ->scheduleReadTaskWithResult({ GameResources::networkResourceId, GameResources::bandwidthResourceId, GameResources::queueResourceId },
	                [this]()
	                {
		                std::stringstream report;
		                report << "============ MMO CLIENT NETWORK DIAGNOSTICS REPORT ============\n";

		                // Current state
		                report << "Current State: " << getConnectionStateString() << "\n";
		                report << "Server: " << serverAddress << ":" << serverPort << "\n";

		                // Connection metrics
		                report << "\n--- Connection Metrics ---\n";
		                report << "Connected: " << (isConnected ? "Yes" : "No") << "\n";

		                uint64_t uptime = 0;
		                if (diagnostics.connectionStartTime > 0)
		                {
			                if (isConnected)
			                {
				                uptime = getCurrentTimeMs() - diagnostics.connectionStartTime;
			                }
			                else
			                {
				                uptime = diagnostics.totalConnectedTimeMs;
			                }
		                }

		                report << "Uptime: " << (uptime / 1000) << " seconds\n";
		                report << "Disconnections: " << diagnostics.disconnectionCount << "\n";
		                report << "Reconnections: " << diagnostics.reconnectionCount << "\n";
		                report << "Reconnection Success Rate: ";
		                if (diagnostics.reconnectionAttempts > 0)
		                {
			                report << (diagnostics.reconnectionCount * 100 / diagnostics.reconnectionAttempts) << "%\n";
		                }
		                else
		                {
			                report << "N/A\n";
		                }
		                report << "Average Time to Reconnect: ";
		                if (diagnostics.reconnectionCount > 0)
		                {
			                report << (diagnostics.timeToReconnectMs / diagnostics.reconnectionCount) << "ms\n";
		                }
		                else
		                {
			                report << "N/A\n";
		                }
		                report << "Longest Downtime: " << (diagnostics.longestDowntimeMs / 1000) << " seconds\n";

		                // Ping statistics
		                report << "\n--- Ping Statistics ---\n";
		                report << "Current Ping: " << pingMs << "ms\n";
		                report << "Average Ping: " << diagnostics.avgPingMs << "ms\n";
		                report << "Min Ping: " << (diagnostics.minPingMs == UINT32_MAX ? 0 : diagnostics.minPingMs) << "ms\n";
		                report << "Max Ping: " << diagnostics.maxPingMs << "ms\n";
		                report << "Ping Jitter: " << diagnostics.pingStdDeviation << "ms\n";
		                report << "Ping Reliability: ";
		                if (diagnostics.pingSent > 0)
		                {
			                report << (diagnostics.pingReceived * 100 / diagnostics.pingSent) << "%\n";
		                }
		                else
		                {
			                report << "N/A\n";
		                }

		                // Packet statistics
		                report << "\n--- Packet Statistics ---\n";
		                report << "Packets Sent: " << packetsSent << "\n";
		                report << "Packets Received: " << packetsReceived << "\n";
		                report << "Bytes Sent: " << bytesSent << " bytes\n";
		                report << "Bytes Received: " << bytesReceived << " bytes\n";
		                report << "Estimated Packet Loss: " << diagnostics.packetLossPercentage << "%\n";
		                report << "Queued Packets: " << outgoingQueue.size() << "\n";

		                // Bandwidth usage
		                report << "\n--- Bandwidth Usage ---\n";
		                report << "Average Send Rate: " << (bandwidthStats.averageSendRateBps / 1024) << " KB/s\n";
		                report << "Average Receive Rate: " << (bandwidthStats.averageReceiveRateBps / 1024) << " KB/s\n";
		                report << "Bandwidth Throttling: " << (bandwidthThrottlingEnabled ? "Enabled" : "Disabled") << "\n";
		                if (outgoingBandwidthLimit > 0)
		                {
			                report << "Bandwidth Usage: " << (bandwidthStats.averageSendRateBps * 100 / outgoingBandwidthLimit) << "% of limit\n";
		                }

		                // Error tracking
		                report << "\n--- Error Statistics ---\n";
		                report << "Packet Creation Errors: " << diagnostics.packetCreationErrors << "\n";
		                report << "Packet Send Errors: " << diagnostics.packetSendErrors << "\n";
		                report << "ENet Service Errors: " << diagnostics.enetServiceErrors << "\n";
		                if (diagnostics.lastErrorTime > 0)
		                {
			                report << "Last Error Time: " << (getCurrentTimeMs() - diagnostics.lastErrorTime) / 1000 << " seconds ago\n";
		                }
		                else
		                {
			                report << "Last Error Time: Never\n";
		                }

		                // MMO-specific stats
		                report << "\n--- MMO-Specific Statistics ---\n";
		                report << "Zone Transitions: " << diagnostics.zoneTransitionCount << "\n";
		                report << "Average Zone Loading Time: " << diagnostics.avgZoneLoadingTimeMs << "ms\n";
		                report << "Combat Packet Success Rate: " << diagnostics.combatPacketSuccessRate << "%\n";
		                report << "Average Combat Action Latency: " << diagnostics.avgCombatActionLatencyMs << "ms\n";
		                report << "High Density Areas Encountered: " << diagnostics.highDensityAreaCount << "\n";
		                report << "Current Area of Interest: " << currentAreaOfInterest << "\n";
		                report << "Current Priority Mode: " << static_cast<int>(currentPriorityMode) << "\n";

		                report << "\n============ END OF REPORT ============\n";

		                return report.str();
	                })
	        .get();
}

// Prepare for zone transition
void NetworkManager::prepareForZoneTransition()
{
	// This requires both network and queue resources
	threadManager->scheduleResourceTask({ GameResources::networkResourceId, GameResources::queueResourceId },
	        [this]()
	        {
		        // Mark that we're in zone transition
		        inZoneTransition = true;
		        zoneTransitionStartTime = getCurrentTimeMs();

		        // Update diagnostics
		        diagnostics.zoneTransitionCount++;

		        // Temporarily increase timeouts during zone loading
		        uint32_t originalTimeout = serverResponseTimeout;
		        serverResponseTimeout = 30000; // 30 seconds for zone transition

		        // Keep only critical packets in the queue
		        std::priority_queue<QueuedPacket> tempQueue;

		        while (!outgoingQueue.empty())
		        {
			        QueuedPacket packet = outgoingQueue.top();
			        outgoingQueue.pop();

			        if (packet.priority <= PRIORITY_HIGH)
			        { // Keep only high and critical priority packets
				        tempQueue.push(packet);
			        }
		        }

		        outgoingQueue = std::move(tempQueue);

		        logger.info("Prepared for zone transition - increased timeout to 30s, kept " + std::to_string(outgoingQueue.size()) + " critical packets");

		        // Schedule timeout reset after zone transition using ThreadManager
		        threadManager->scheduleTask(
		                [this, originalTimeout]()
		                {
			                std::this_thread::sleep_for(std::chrono::seconds(30));

			                threadManager->scheduleResourceTask({ GameResources::networkResourceId },
			                        [this, originalTimeout]()
			                        {
				                        // Record zone loading time
				                        uint64_t zoneLoadTime = getCurrentTimeMs() - zoneTransitionStartTime;

				                        // Update average (weighted)
				                        if (diagnostics.avgZoneLoadingTimeMs == 0)
				                        {
					                        diagnostics.avgZoneLoadingTimeMs = zoneLoadTime;
				                        }
				                        else
				                        {
					                        diagnostics.avgZoneLoadingTimeMs = (diagnostics.avgZoneLoadingTimeMs * (diagnostics.zoneTransitionCount - 1) + zoneLoadTime) / diagnostics.zoneTransitionCount;
				                        }

				                        inZoneTransition = false;
				                        serverResponseTimeout = originalTimeout;
				                        logger.info("Zone transition complete - restored timeout to " + std::to_string(originalTimeout) + "ms");
			                        });
		                });
	        });
}

// Adapt to high population density
void NetworkManager::adaptToHighPopulationDensity(bool highDensity)
{
	threadManager->scheduleResourceTask({ GameResources::networkResourceId },
	        [this, highDensity]()
	        {
		        if (highDensity != highPopulationDensity)
		        {
			        highPopulationDensity = highDensity;

			        if (highDensity)
			        {
				        // Reduce position update frequency in crowded areas
				        positionUpdateInterval = 200; // 200ms between updates (5 per second)

				        // Reduce position precision
				        positionPrecision = 1; // 1 decimal place precision

				        // Record for diagnostics
				        diagnostics.highDensityAreaCount++;
				        diagnostics.highDensityPositionUpdatesPerSecond = 1000.0f / positionUpdateInterval;

				        logger.info("Adapting to high population density - reduced update frequency and precision");
			        }
			        else
			        {
				        // Normal settings for low-population areas
				        positionUpdateInterval = 100; // 100ms (10 updates per second)
				        positionPrecision = 2;        // 2 decimal places

				        logger.info("Reverting to normal population density settings");
			        }
		        }
	        });
}

// Set priority mode
void NetworkManager::setPriorityMode(PriorityMode mode)
{
	// First check if the mode is already set
	bool shouldUpdate = threadManager->scheduleReadTaskWithResult({ GameResources::networkResourceId }, [this, mode]() { return mode != currentPriorityMode; }).get();

	if (!shouldUpdate)
	{
		return;
	}

	// Update priority mode
	threadManager->scheduleResourceTask({ GameResources::networkResourceId, GameResources::bandwidthResourceId },
	        [this, mode]()
	        {
		        currentPriorityMode = mode;

		        // Adjust category token buckets based on mode
		        switch (mode)
		        {
			        case PriorityMode::COMBAT:
				        // Prioritize gameplay over everything else
				        categoryTokenBuckets[MessageCategory::GAMEPLAY] = TokenBucket(outgoingBandwidthLimit * 0.6f, // 60% for gameplay
				                outgoingBandwidthLimit * 0.8f                                                        // 80% burst
				        );
				        categoryTokenBuckets[MessageCategory::POSITION] = TokenBucket(outgoingBandwidthLimit * 0.2f, // 20% for position
				                outgoingBandwidthLimit * 0.3f                                                        // 30% burst
				        );
				        // Reduce other categories
				        categoryTokenBuckets[MessageCategory::CHAT] = TokenBucket(outgoingBandwidthLimit * 0.05f, // 5% for chat
				                outgoingBandwidthLimit * 0.1f                                                     // 10% burst
				        );
				        logger.info("Switched to COMBAT priority mode - gameplay packets prioritized");
				        break;

			        case PriorityMode::CRAFTING:
				        // Balance between gameplay and chat
				        categoryTokenBuckets[MessageCategory::GAMEPLAY] = TokenBucket(outgoingBandwidthLimit * 0.3f, // 30% for gameplay
				                outgoingBandwidthLimit * 0.5f                                                        // 50% burst
				        );
				        categoryTokenBuckets[MessageCategory::CHAT] = TokenBucket(outgoingBandwidthLimit * 0.3f, // 30% for chat
				                outgoingBandwidthLimit * 0.5f                                                    // 50% burst
				        );
				        // Reduce position updates
				        categoryTokenBuckets[MessageCategory::POSITION] = TokenBucket(outgoingBandwidthLimit * 0.1f, // 10% for position
				                outgoingBandwidthLimit * 0.2f                                                        // 20% burst
				        );
				        logger.info("Switched to CRAFTING priority mode - balanced gameplay and chat");
				        break;

			        case PriorityMode::NORMAL:
			        default:
				        // Reset to default allocation
				        categoryTokenBuckets[MessageCategory::CRITICAL] = TokenBucket(outgoingBandwidthLimit * 0.4f, // 40% for critical
				                outgoingBandwidthLimit * 0.6f                                                        // 60% burst
				        );
				        categoryTokenBuckets[MessageCategory::GAMEPLAY] = TokenBucket(outgoingBandwidthLimit * 0.3f, // 30% for gameplay
				                outgoingBandwidthLimit * 0.4f                                                        // 40% burst
				        );
				        categoryTokenBuckets[MessageCategory::POSITION] = TokenBucket(outgoingBandwidthLimit * 0.15f, // 15% for position
				                outgoingBandwidthLimit * 0.3f                                                         // 30% burst
				        );
				        categoryTokenBuckets[MessageCategory::CHAT] = TokenBucket(outgoingBandwidthLimit * 0.1f, // 10% for chat
				                outgoingBandwidthLimit * 0.2f                                                    // 20% burst
				        );
				        logger.info("Switched to NORMAL priority mode - balanced allocation");
				        break;
		        }
	        });
}

// Set server-requested throttling
void NetworkManager::setServerRequestedThrottling(int throttleLevel)
{
	// First check if the throttle level is already set
	bool shouldUpdate = threadManager->scheduleReadTaskWithResult({ GameResources::networkResourceId }, [this, throttleLevel]() { return throttleLevel != serverThrottleLevel; }).get();

	if (!shouldUpdate)
	{
		return;
	}

	// Update throttling
	threadManager->scheduleResourceTask({ GameResources::networkResourceId, GameResources::bandwidthResourceId },
	        [this, throttleLevel]()
	        {
		        serverThrottleLevel = throttleLevel;

		        if (throttleLevel > 0)
		        {
			        // Server has requested throttling
			        float throttleFactor = 1.0f - (throttleLevel * 0.1f); // Level 1 = 10% reduction, level 5 = 50% reduction
			        if (throttleFactor < 0.1f)
				        throttleFactor = 0.1f; // Min 10% of normal

			        // Apply throttling to bandwidth limits
			        uint32_t throttledLimit = static_cast<uint32_t>(outgoingBandwidthLimit * throttleFactor);
			        bandwidthThrottlingEnabled = true;
			        throttledOutgoingLimit = throttledLimit;

			        // Adjust token buckets
			        bandwidthTokenBucket = TokenBucket((float) throttledLimit, (float) throttledLimit * 1.5f);

			        logger.warning("Server requested throttling level " + std::to_string(throttleLevel) + " - reducing bandwidth to " + std::to_string(throttleFactor * 100) + "%");
		        }
		        else
		        {
			        // Server has removed throttling request
			        bandwidthThrottlingEnabled = false;

			        // Reset token buckets
			        bandwidthTokenBucket = TokenBucket((float) outgoingBandwidthLimit, (float) outgoingBandwidthLimit * 1.5f);

			        logger.info("Server removed throttling request - resuming normal bandwidth");
		        }
	        });
}

// Reset stats
void NetworkManager::resetStats()
{
	threadManager->scheduleResourceTask({ GameResources::networkResourceId, GameResources::bandwidthResourceId },
	        [this]()
	        {
		        // Reset network statistics
		        packetsSent = 0;
		        packetsReceived = 0;
		        bytesSent = 0;
		        bytesReceived = 0;

		        // Reset bandwidth stats
		        bandwidthStats.averageSendRateBps = 0;
		        bandwidthStats.averageReceiveRateBps = 0;
		        bandwidthStats.bytesSentLastSecond = 0;
		        bandwidthStats.bytesReceivedLastSecond = 0;
		        bandwidthStats.sendRateHistory.clear();
		        bandwidthStats.receiveRateHistory.clear();

		        // Reset ping stats
		        pingHistory.clear();
		        packetLossEstimate = 0.0f;
		        jitter = 0.0f;

		        // Reset diagnostics
		        diagnostics.reset();

		        logger.debug("Network statistics reset");
	        });
}

// Get current time in milliseconds
uint64_t NetworkManager::getCurrentTimeMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Helper packet methods
void NetworkManager::sendChatMessage(const std::string& sender, const std::string& message, bool isGlobal)
{
	auto chatPacket = packetManager.createChatMessage(sender, message, isGlobal);
	sendPacket(chatPacket, true); // Chat messages should be reliable
}

void NetworkManager::sendSystemMessage(const std::string& message)
{
	auto sysPacket = packetManager.createSystemMessage(message);
	sendPacket(sysPacket, true); // System messages should be reliable
}

void NetworkManager::sendCommand(const std::string& command, const std::vector<std::string>& args)
{
	auto cmdPacket = packetManager.createCommand(command, args);
	sendPacket(cmdPacket, true); // Commands should be reliable
}

void NetworkManager::sendTeleport(const Position& position)
{
	auto teleportPacket = packetManager.createTeleport(position);
	sendPacket(teleportPacket, true); // Teleports should be reliable
}
