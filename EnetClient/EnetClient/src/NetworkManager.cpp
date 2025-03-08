#include "NetworkManager.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

// TokenBucket implementation
bool NetworkManager::TokenBucket::consumeTokens(float amount, uint32_t currentTime)
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
void NetworkManager::NetworkDiagnostics::reset()
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
NetworkManager::NetworkManager(Logger& logger, std::shared_ptr<ThreadManager> threadManager)
      : logger(logger), bandwidthTokenBucket(0, 0), threadManager(threadManager) // Will be properly initialized later
{
	logger.debug("Initializing Enhanced NetworkManager");

	// Create a thread manager if none provided
	if (!threadManager)
	{
		this->threadManager = std::make_shared<ThreadManager>();
	}

	// Default packet type configuration
	messageTypeConfigs["AUTH:"] = { MessageCategory::CRITICAL, PRIORITY_CRITICAL, false, 1.0f };
	messageTypeConfigs["PING:"] = { MessageCategory::CRITICAL, PRIORITY_HIGH, false, 1.0f };
	messageTypeConfigs["POSITION:"] = { MessageCategory::POSITION, PRIORITY_NORMAL, true, 0.3f };
	messageTypeConfigs["MOVE_DELTA:"] = { MessageCategory::POSITION, PRIORITY_NORMAL, true, 0.3f };
	messageTypeConfigs["CHAT:"] = { MessageCategory::CHAT, PRIORITY_NORMAL, true, 0.7f };
	messageTypeConfigs["COMMAND:"] = { MessageCategory::GAMEPLAY, PRIORITY_HIGH, false, 0.9f };
	messageTypeConfigs["TELEPORT:"] = { MessageCategory::GAMEPLAY, PRIORITY_CRITICAL, false, 1.0f };

	// Default bandwidth allocation
	categoryTokenBuckets[MessageCategory::CRITICAL] = TokenBucket(50000, 100000); // 50 KB/s, 100 KB burst
	categoryTokenBuckets[MessageCategory::GAMEPLAY] = TokenBucket(40000, 80000);  // 40 KB/s, 80 KB burst
	categoryTokenBuckets[MessageCategory::POSITION] = TokenBucket(30000, 60000);  // 30 KB/s, 60 KB burst
	categoryTokenBuckets[MessageCategory::CHAT] = TokenBucket(20000, 40000);      // 20 KB/s, 40 KB burst
	categoryTokenBuckets[MessageCategory::TELEMETRY] = TokenBucket(5000, 10000);  // 5 KB/s, 10 KB burst
	categoryTokenBuckets[MessageCategory::MISC] = TokenBucket(5000, 10000);       // 5 KB/s, 10 KB burst

	// Global bandwidth limit (150 KB/s, 300 KB burst)
	bandwidthTokenBucket = TokenBucket(150000, 300000);
}

// Destructor with enhanced cleanup
NetworkManager::~NetworkManager()
{
	disconnect();

	// Clean up ENet resources
	if (client != nullptr)
	{
		enet_host_destroy(client);
		client = nullptr;
		enet_deinitialize();
	}

	logger.debug("Enhanced NetworkManager destroyed");
}

bool NetworkManager::initialize()
{
	logger.debug("Initializing ENet");

	// Initialize ENet
	if (enet_initialize() != 0)
	{
		logger.error("Failed to initialize ENet");
		return false;
	}

	// Create client host - CHANGE THIS LINE:
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
}

std::string NetworkManager::getConnectionStateString() const
{
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
	{
		std::lock_guard<std::mutex> lock(networkMutex);

		// Don't try to connect if we're already connected or connecting
		if (connectionState == ConnectionState::CONNECTED || connectionState == ConnectionState::CONNECTING)
		{
			logger.warning("Connection attempt ignored - already in state: " + getConnectionStateString());
			return isConnected;
		}

		connectionState = ConnectionState::CONNECTING;
		serverAddress = address;
		serverPort = port;

		// Reset connection-related stats
		successivePingFailures = 0;
		adaptiveTimeoutMultiplier = 1;
		serverResponseTimeout = initialServerResponseTimeout;

		// Create ENet address
		ENetAddress enetAddress;
		if (enet_address_set_host(&enetAddress, address) != 0)
		{
			logger.error("Failed to resolve host address: " + std::string(address));
			connectionState = ConnectionState::DISCONNECTED;
			return false;
		}
		enetAddress.port = port;

		logger.debug("Connecting to server at " + std::string(address) + ":" + std::to_string(port) + "...");

		// Connect to the server
		server = enet_host_connect(client, &enetAddress, 4, 0);

		if (server == nullptr)
		{
			logger.error("Failed to initiate connection to server");
			connectionState = ConnectionState::DISCONNECTED;
			return false;
		}
	} // End of mutex lock scope

	// Record start time for diagnostics
	uint32_t connectStartTime = getCurrentTimeMs();

	// Wait for connection establishment with a timeout
	uint32_t startTime = getCurrentTimeMs();
	uint32_t timeout = 5000; // 5 seconds timeout

	while (getCurrentTimeMs() - startTime < timeout)
	{
		ENetEvent event;
		// Use a small timeout to check in increments
		int result = enet_host_service(client, &event, 100); // 100ms chunks

		if (result > 0)
		{
			if (event.type == ENET_EVENT_TYPE_CONNECT)
			{
				std::lock_guard<std::mutex> lock(networkMutex);
				isConnected = true;
				connectionState = ConnectionState::CONNECTED;
				lastNetworkActivity = getCurrentTimeMs();
				lastHeartbeatSent = getCurrentTimeMs(); // Initialize heartbeat timer

				// Update diagnostics
				if (diagnostics.connectionStartTime == 0)
				{
					diagnostics.connectionStartTime = getCurrentTimeMs();
				}
				else if (diagnostics.lastConnectionTime > 0)
				{
					diagnostics.reconnectionCount++;
					uint32_t downtime = connectStartTime - diagnostics.lastConnectionTime;
					diagnostics.timeToReconnectMs += downtime;

					// Track longest downtime
					if (downtime > diagnostics.longestDowntimeMs)
					{
						diagnostics.longestDowntimeMs = downtime;
					}
				}

				diagnostics.lastConnectionTime = getCurrentTimeMs();

				// Process any queued packets
				if (!outgoingQueue.empty())
				{
					processQueuedPackets();
				}

				logger.debug("Connected to server at " + std::string(address) + ":" + std::to_string(port));
				return true;
			}
			else if (event.type == ENET_EVENT_TYPE_RECEIVE)
			{
				// Handle any early packets
				enet_packet_destroy(event.packet);
			}
		}
		else if (result < 0)
		{
			std::lock_guard<std::mutex> lock(networkMutex);
			logger.error("Error while connecting: " + std::to_string(result));
			connectionState = ConnectionState::DISCONNECTED;
			if (server != nullptr)
			{
				enet_peer_reset(server);
				server = nullptr;
			}

			// Update diagnostics
			diagnostics.enetServiceErrors++;
			diagnostics.lastErrorTime = getCurrentTimeMs();

			return false;
		}
	}

	// Connection timed out
	std::lock_guard<std::mutex> lock(networkMutex);
	logger.error("Connection to server failed (timeout)");
	connectionState = ConnectionState::DISCONNECTED;
	if (server != nullptr)
	{
		enet_peer_reset(server);
		server = nullptr;
	}
	return false;
}

void NetworkManager::disconnect(bool showMessage)
{
	std::lock_guard<std::mutex> lock(networkMutex);

	// Don't try to disconnect if we're already disconnected
	if (connectionState == ConnectionState::DISCONNECTED)
	{
		return;
	}

	// Update connection state
	connectionState = ConnectionState::DISCONNECTING;

	if (isConnected && server != nullptr)
	{
		if (showMessage)
		{
			logger.info("Disconnecting from server...");
		}

		// Calculate and record session time
		if (diagnostics.lastConnectionTime > 0)
		{
			uint32_t sessionTime = getCurrentTimeMs() - diagnostics.lastConnectionTime;
			diagnostics.totalConnectedTimeMs += sessionTime;
		}

		// Update disconnection count
		diagnostics.disconnectionCount++;

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
			logger.warning("Forcing disconnection after timeout");
			enet_peer_reset(server);
		}

		server = nullptr;
	}

	// Always set these flags regardless of clean disconnection
	isConnected = false;
	connectionState = ConnectionState::DISCONNECTED;
	waitingForPingResponse = false;
	successivePingFailures = 0;
}

bool NetworkManager::reconnectToServer()
{
	if (isConnected)
	{
		disconnect(false);
	}

	logger.info("Attempting to reconnect to server...");
	reconnecting = true;

	// Track reconnection attempts for diagnostics
	diagnostics.reconnectionAttempts++;

	for (int attempt = 1; attempt <= reconnectAttempts; attempt++)
	{
		logger.debug("Reconnection attempt " + std::to_string(attempt) + " of " + std::to_string(reconnectAttempts));

		// Create a future for the connection attempt
		auto future = threadManager->scheduleTaskWithResult([this]() -> bool { return connectToServer(serverAddress.c_str(), serverPort); });

		// Wait for the result
		bool connected = future.get();

		if (connected)
		{
			logger.info("Reconnection successful!");
			reconnecting = false;

			// Process queued packets
			processQueuedPackets();

			return true;
		}

		// Wait before next attempt using exponential backoff
		uint32_t backoffTime = 1000 * min(30, 1 << (attempt - 1)); // Cap at 30 seconds
		logger.debug("Waiting " + std::to_string(backoffTime / 1000) + " seconds before next attempt...");
		std::this_thread::sleep_for(std::chrono::milliseconds(backoffTime));
	}

	logger.error("Failed to reconnect after " + std::to_string(reconnectAttempts) + " attempts");
	reconnecting = false;

	// Clean very old packets from queue
	cleanExpiredPackets();

	return false;
}

void NetworkManager::cleanExpiredPackets()
{
	uint32_t currentTime = getCurrentTimeMs();
	std::lock_guard<std::mutex> lock(queueMutex);
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
}

void NetworkManager::sendPacket(const std::string& message, bool reliable)
{
	// Check if we're connected first
	if (!isConnected)
	{
		// Queue the packet if enabled
		if (queuePacketsDuringDisconnection)
		{
			// Determine priority based on message type
			uint8_t priority = PRIORITY_NORMAL;
			for (const auto& configPair: messageTypeConfigs)
			{
				if (!configPair.first.empty() && message.find(configPair.first) == 0)
				{
					priority = configPair.second.priority;
					break;
				}
			}

			sendPacketWithPriority(message, reliable, priority);
		}
		return;
	}

	// All connection checks inside the lock
	std::lock_guard<std::mutex> lock(networkMutex);

	if (!isConnected || server == nullptr || connectionState != ConnectionState::CONNECTED)
	{
		// Queue the packet if enabled
		if (queuePacketsDuringDisconnection)
		{
			// Determine priority based on message type
			uint8_t priority = PRIORITY_NORMAL;
			for (const auto& configPair: messageTypeConfigs)
			{
				if (!configPair.first.empty() && message.find(configPair.first) == 0)
				{
					priority = configPair.second.priority;
					break;
				}
			}

			// Unlock before calling sendPacketWithPriority to avoid deadlock
		}
		return;
	}

	// Validate that we have a valid message to send
	if (message.empty())
	{
		logger.error("Attempted to send empty packet");
		diagnostics.packetCreationErrors++;
		return;
	}

	// Check bandwidth limits
	uint32_t packetSize = message.length() + 28; // Add estimated header size
	if (!isPacketSendAllowed(message, packetSize))
	{
		if (message.substr(0, 9) != "POSITION:" && message.substr(0, 5) != "PING:")
		{
			logger.debug("Packet throttled due to bandwidth limits: " + message.substr(0, 20) + "...");
		}
		return;
	}

	// Determine if we should compress this message
	std::string packetData = message;
	float compressionRatio = 1.0f;

	if (compressionEnabled && message.length() >= minSizeForCompression)
	{
		// Get message-specific compression threshold
		float threshold = minCompressionRatio;
		for (const auto& configPair: messageTypeConfigs)
		{
			if (!configPair.first.empty() && message.find(configPair.first) == 0)
			{
				// In this implementation we're using the throttleMultiplier as the compression threshold
				// for simplicity, but in a real implementation you might want separate configuration
				threshold = configPair.second.throttleMultiplier;
				break;
			}
		}

		// Compress the message
		std::string compressed = compressMessage(message, &compressionRatio);

		// Only use compression if it actually helps enough
		if (compressionRatio <= threshold)
		{
			// Prefix the message with compression info
			packetData = "COMP:" + compressed;
		}
	}

	// Create packet inside the lock
	ENetPacket* packet = enet_packet_create(packetData.c_str(), packetData.length(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

	if (packet == nullptr)
	{
		logger.error("Failed to create packet: Memory allocation failed");
		diagnostics.packetCreationErrors++;
		return;
	}

	// Send packet while holding the lock
	bool sendSuccess = (enet_peer_send(server, 0, packet) == 0);

	if (sendSuccess)
	{
		// Update stats
		packetsSent++;
		bytesSent += packetData.length();
		bandwidthStats.bytesSentLastSecond += packetData.length();
		lastNetworkActivity = getCurrentTimeMs();

		// Don't spam logs with position updates and pings
		if (message.substr(0, 9) != "POSITION:" && message.substr(0, 5) != "PING:")
		{
			std::string logMessage = "Sent: " + message;
			if (packetData != message)
			{
				logMessage += " (compressed to " + std::to_string(static_cast<int>(compressionRatio * 100)) + "%)";
			}
			logger.logNetworkEvent(logMessage);
		}
	}
	else
	{
		logger.error("Failed to send packet: " + message);
		diagnostics.packetSendErrors++;
		enet_packet_destroy(packet); // We need to destroy the packet if send failed
	}
}

void NetworkManager::sendPacketWithPriority(const std::string& message, bool reliable, uint8_t priority)
{
	// Check if we're connected for the fast path
	{
		std::lock_guard<std::mutex> lock(networkMutex);
		if (isConnected && connectionState == ConnectionState::CONNECTED)
		{
			// Send directly if connected
			sendPacket(message, reliable);
			return;
		}
	}

	// If we reach here, we're disconnected but may want to queue
	if (queuePacketsDuringDisconnection)
	{
		std::lock_guard<std::mutex> lock(queueMutex);

		// Check if queue has room
		if (outgoingQueue.size() < maxQueueSize)
		{
			// Create queued packet with current time
			QueuedPacket packet(message, reliable, priority);
			packet.timestamp = getCurrentTimeMs();
			outgoingQueue.push(packet);

			logger.debug("Queued packet during disconnection (priority=" + std::to_string(priority) + "): " + (message.length() > 50 ? message.substr(0, 47) + "..." : message));
		}
		else
		{
			// Queue is full, drop the packet
			logger.warning("Packet queue full, dropping packet: " + (message.length() > 50 ? message.substr(0, 47) + "..." : message));
		}
	}
}

void NetworkManager::processQueuedPackets()
{
	if (!isConnected || connectionState != ConnectionState::CONNECTED)
	{
		logger.debug("Not processing queued packets - not connected");
		return;
	}

	uint32_t currentTime = getCurrentTimeMs();
	uint32_t processedCount = 0;
	uint32_t expiredCount = 0;
	uint32_t processLimit = 20; // Process at most 20 packets at once to avoid blocking

	logger.debug("Processing queued packets...");

	while (processedCount < processLimit)
	{
		QueuedPacket packet("", false);
		bool hasPacket = false;

		// Safely get the highest priority packet
		{
			std::lock_guard<std::mutex> lock(queueMutex);
			if (outgoingQueue.empty())
			{
				break;
			}

			packet = outgoingQueue.top();
			outgoingQueue.pop();
			hasPacket = true;
		}

		if (hasPacket)
		{
			// Check if packet is too old
			if (currentTime - packet.timestamp > maxQueuedPacketAge)
			{
				expiredCount++;
				continue;
			}

			// Send the packet
			sendPacket(packet.message, packet.reliable);
			processedCount++;

			// Small delay to avoid flooding the network
			if (processedCount % 5 == 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		}
	}

	// Log results
	uint32_t remaining = getQueuedPacketCount();
	logger.info("Processed " + std::to_string(processedCount) + " queued packets, " + std::to_string(expiredCount) + " expired, " + std::to_string(remaining) + " remaining");

	// Schedule processing of remaining packets if needed
	if (remaining > 0)
	{
		std::thread(
		        [this]()
		        {
			        std::this_thread::sleep_for(std::chrono::milliseconds(50));
			        processQueuedPackets();
		        })
		        .detach();
	}
}

void NetworkManager::clearPacketQueue()
{
	std::lock_guard<std::mutex> lock(queueMutex);
	std::priority_queue<QueuedPacket> empty;
	std::swap(outgoingQueue, empty);
	logger.debug("Packet queue cleared");
}

uint32_t NetworkManager::getQueuedPacketCount()
{
	std::lock_guard<std::mutex> lock(queueMutex);
	return outgoingQueue.size();
}

void NetworkManager::sendPositionUpdate(float x, float y, float z, float lastX, float lastY, float lastZ, bool useCompressedUpdates, float movementThreshold)
{
	// First check if we're connected to avoid wasted calculations
	if (!isConnected)
		return;

	// Validate position values
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
	{
		logger.error("Invalid position values: (" + (std::isfinite(x) ? std::to_string(x) : "NaN/Inf") + ", " + (std::isfinite(y) ? std::to_string(y) : "NaN/Inf") + ", " + (std::isfinite(z) ? std::to_string(z) : "NaN/Inf") + ")");
		return;
	}

	// Calculate movement distance with validation
	float moveDistance = 0.0f;
	if (std::isfinite(lastX) && std::isfinite(lastY) && std::isfinite(lastZ))
	{
		moveDistance = std::sqrt(pow(x - lastX, 2) + pow(y - lastY, 2) + pow(z - lastZ, 2));
	}
	else
	{
		// Can't calculate move distance with invalid last position, force an update
		moveDistance = movementThreshold * 2.0f;
	}

	// Only send if position has changed significantly
	if (moveDistance < movementThreshold)
		return;

	// Adjust precision based on population density
	std::ostringstream xStr, yStr, zStr;
	xStr << std::fixed << std::setprecision(positionPrecision) << x;
	yStr << std::fixed << std::setprecision(positionPrecision) << y;
	zStr << std::fixed << std::setprecision(positionPrecision) << z;

	// Prepare position update message
	std::string positionMsg;

	if (useCompressedUpdates)
	{
		// Delta compression: Send only the coordinates that changed significantly
		positionMsg = "MOVE_DELTA:";
		bool addedCoordinate = false;

		if (std::abs(x - lastX) >= movementThreshold)
		{
			positionMsg += "x" + xStr.str();
			addedCoordinate = true;
		}

		if (std::abs(y - lastY) >= movementThreshold)
		{
			if (addedCoordinate)
				positionMsg += ",";
			positionMsg += "y" + yStr.str();
			addedCoordinate = true;
		}

		if (std::abs(z - lastZ) >= movementThreshold)
		{
			if (addedCoordinate)
				positionMsg += ",";
			positionMsg += "z" + zStr.str();
			addedCoordinate = true;
		}

		// Make sure we have at least one coordinate
		if (!addedCoordinate)
		{
			// Fallback to full update if no individual coordinate meets threshold
			positionMsg = "POSITION:" + xStr.str() + "," + yStr.str() + "," + zStr.str();
		}
	}
	else
	{
		// Full position update (fallback method)
		positionMsg = "POSITION:" + xStr.str() + "," + yStr.str() + "," + zStr.str();
	}

	// Use unreliable packets for position updates
	sendPacket(positionMsg, false);
}

void NetworkManager::update(const std::function<void()>& updatePositionCallback, const std::function<void(const ENetPacket*)>& handlePacketCallback, const std::function<void()>& disconnectCallback)
{
	// Update bandwidth statistics
	updateBandwidthStats();

	// Use a scoped lock to ensure we don't hold the lock for too long
	{
		std::lock_guard<std::mutex> lock(networkMutex);

		// Only process network updates if connected and client is initialized
		if (!isConnected || client == nullptr || connectionState != ConnectionState::CONNECTED)
		{
			return;
		}

		// Process a LIMITED number of pending events per frame
		// to prevent UI blocking when many packets arrive at once
		ENetEvent event;
		int eventCount = 0;
		int errorCount = 0;
		const int MAX_EVENTS_PER_UPDATE = 10; // Process at most 10 events per update
		const int MAX_ERRORS_PER_UPDATE = 3;  // Allow a small number of errors before giving up

		// Use a 0 timeout to ensure we don't block
		while (eventCount < MAX_EVENTS_PER_UPDATE && errorCount < MAX_ERRORS_PER_UPDATE)
		{
			int serviceResult = enet_host_service(client, &event, 0);

			if (serviceResult > 0)
			{
				eventCount++;

				switch (event.type)
				{
					case ENET_EVENT_TYPE_RECEIVE:
					{
						// Extract packet data
						std::string packetData(reinterpret_cast<const char*>(event.packet->data), event.packet->dataLength);

						// Check if this is a compressed packet
						if (packetData.substr(0, 5) == "COMP:")
						{
							try
							{
								// Extract compressed data
								std::string compressedData = packetData.substr(5);

								// Decompress
								packetData = decompressMessage(compressedData);
							}
							catch (std::exception& e)
							{
								logger.error("Error decompressing packet: " + std::string(e.what()));
								// Continue with the original data
							}
						}

						// Update stats
						packetsReceived++;
						bytesReceived += event.packet->dataLength;
						bandwidthStats.bytesReceivedLastSecond += event.packet->dataLength;
						lastNetworkActivity = getCurrentTimeMs();

						// Try to process as an auth packet first
						bool handled = processReceivedPacket(packetData.c_str(), packetData.length());

						// If not handled as an auth packet and we have a callback
						if (!handled && handlePacketCallback)
						{
							try
							{
								// We need to create a copy of the packet for the callback
								ENetPacket* packetCopy = enet_packet_create(packetData.c_str(), packetData.length(), event.packet->flags);

								if (packetCopy)
								{
									// Use thread manager for packet handling
									threadManager->scheduleNetworkTask(
									        [callback = handlePacketCallback, packet = packetCopy]()
									        {
										        callback(packet);
										        enet_packet_destroy(packet); // Clean up after use
									        });
								}
								else
								{
									logger.error("Failed to create packet copy");
								}
							}
							catch (std::exception& e)
							{
								logger.error("Error in packet callback: " + std::string(e.what()));
							}
						}

						enet_packet_destroy(event.packet);
						break;
					}

					case ENET_EVENT_TYPE_DISCONNECT:
						logger.error("Disconnected from server");
						isConnected = false;
						connectionState = ConnectionState::DISCONNECTED;
						server = nullptr;

						// Schedule disconnect callback using thread manager
						if (disconnectCallback)
						{
							threadManager->scheduleNetworkTask(disconnectCallback);
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

				// Update diagnostics
				diagnostics.enetServiceErrors++;
				diagnostics.lastErrorTime = getCurrentTimeMs();

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
	} // End of mutex lock scope

	// Call position update callback outside the lock using thread manager
	if (updatePositionCallback && isConnected && connectionState == ConnectionState::CONNECTED)
	{
		threadManager->scheduleTask(updatePositionCallback);
	}

	// Check if we need to send a heartbeat
	uint32_t currentTime = getCurrentTimeMs();
	if (isConnected && connectionState == ConnectionState::CONNECTED && currentTime - lastHeartbeatSent >= heartbeatIntervalMs)
	{
		sendHeartbeat();
	}
}

void NetworkManager::sendHeartbeat()
{
	uint32_t currentTime = getCurrentTimeMs();
	lastHeartbeatSent = currentTime;

	std::string heartbeatMsg = "HEARTBEAT";
	sendPacket(heartbeatMsg, false); // Using unreliable for heartbeats
}

void NetworkManager::sendPing()
{
	if (!isConnected || connectionState != ConnectionState::CONNECTED)
		return;

	uint32_t currentTime = getCurrentTimeMs();
	bool shouldSendPing = false;
	bool shouldLogTimeout = false;

	// Use a local copy of the ping sequence number
	uint32_t currentPingSequence = 0;

	{
		std::lock_guard<std::mutex> lock(networkMutex);

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
	}

	// Send ping with sequence number
	if (shouldSendPing)
	{
		std::string pingMsg = "PING:" + std::to_string(currentTime) + ":" + std::to_string(currentPingSequence);
		sendPacket(pingMsg, true); // Using reliable for pings for better detection
	}

	// Handle ping timeout
	if (shouldLogTimeout)
	{
		logger.warning("Ping response timeout - Server may have connectivity issues");
	}
}

void NetworkManager::checkConnectionHealth(const std::function<void()>& disconnectCallback)
{
	if (!isConnected || connectionState != ConnectionState::CONNECTED || reconnecting)
		return;

	uint32_t currentTime = getCurrentTimeMs();
	bool shouldDisconnect = false;

	{
		std::lock_guard<std::mutex> lock(networkMutex);

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
	}

	// Call the callback outside the lock
	if (shouldDisconnect && disconnectCallback)
	{
		disconnectCallback();
	}
}

void NetworkManager::updateBandwidthStats()
{
	uint32_t currentTime = getCurrentTimeMs();

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
			for (uint32_t rate: bandwidthStats.sendRateHistory)
			{
				sum += rate;
			}
			bandwidthStats.averageSendRateBps = sum / bandwidthStats.sendRateHistory.size();
		}

		if (!bandwidthStats.receiveRateHistory.empty())
		{
			float sum = 0;
			for (uint32_t rate: bandwidthStats.receiveRateHistory)
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
}

bool NetworkManager::isPacketSendAllowed(const std::string& message, uint32_t size)
{
	if (outgoingBandwidthLimit == 0)
	{
		return true; // No limit set
	}

	uint32_t currentTime = getCurrentTimeMs();

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

NetworkManager::MessageTypeConfig NetworkManager::getMessageConfig(const std::string& message)
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

void NetworkManager::configureMessageType(const std::string& prefix, uint8_t priority, bool canThrottle, float throttleMultiplier)
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
}

void NetworkManager::configureBandwidthManagement(uint32_t outgoingLimitBps, uint32_t throttledLimitBps, bool enableThrottling)
{
	std::lock_guard<std::mutex> lock(networkMutex);

	outgoingBandwidthLimit = outgoingLimitBps;
	throttledOutgoingLimit = throttledLimitBps;
	bandwidthThrottlingEnabled = enableThrottling;

	// Initialize token bucket with new rate
	bandwidthTokenBucket = TokenBucket(outgoingLimitBps, outgoingLimitBps); // 1 second burst capacity

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
}

void NetworkManager::configureAdaptiveTimeout(uint32_t initial, uint32_t max, uint32_t pingFailures)
{
	std::lock_guard<std::mutex> lock(networkMutex);

	initialServerResponseTimeout = initial;
	maxServerResponseTimeout = max;
	maxPingFailures = pingFailures;
	serverResponseTimeout = initial;

	logger.debug("Configured adaptive timeout: initial=" + std::to_string(initial) + "ms, max=" + std::to_string(max) + "ms, maxPingFailures=" + std::to_string(pingFailures));
}

void NetworkManager::setPacketQueueing(bool enabled, uint32_t maxSize, uint32_t maxAgeMs)
{
	std::lock_guard<std::mutex> lock(queueMutex);

	queuePacketsDuringDisconnection = enabled;
	maxQueueSize = maxSize;
	maxQueuedPacketAge = maxAgeMs;

	logger.debug("Packet queueing " + std::string(enabled ? "enabled" : "disabled") + ", maxSize=" + std::to_string(maxSize) + ", maxAge=" + std::to_string(maxAgeMs) + "ms");
}

void NetworkManager::configureCompression(bool enabled, uint32_t minSize, float minRatio)
{
	std::lock_guard<std::mutex> lock(networkMutex);

	compressionEnabled = enabled;
	minSizeForCompression = minSize;
	minCompressionRatio = minRatio;

	logger.debug("Compression " + std::string(enabled ? "enabled" : "disabled") + ", minSize=" + std::to_string(minSize) + ", minRatio=" + std::to_string(minRatio));
}

std::string NetworkManager::compressMessage(const std::string& message, float* ratio)
{
	// This is a simple run-length encoding implementation
	// In a real implementation, you'd use a proper compression algorithm like zlib

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

std::string NetworkManager::decompressMessage(const std::string& compressedData)
{
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

void NetworkManager::updatePingStatistics(uint32_t pingTime)
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

	// Calculate jitter
	calculateJitter();

	// Estimate packet loss
	estimatePacketLoss();
}

void NetworkManager::calculateJitter()
{
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
		variance += pow(ping - mean, 2);
	}
	variance /= pingHistory.size();

	// Jitter is the standard deviation
	jitter = sqrt(variance);
	diagnostics.pingStdDeviation = jitter;
}

void NetworkManager::estimatePacketLoss()
{
	if (diagnostics.pingSent == 0)
	{
		packetLossEstimate = 0.0f;
		return;
	}

	packetLossEstimate = static_cast<float>(diagnostics.pingLost) / diagnostics.pingSent;
	diagnostics.packetLossPercentage = static_cast<uint32_t>(packetLossEstimate * 100.0f);
}

bool NetworkManager::processReceivedPacket(const void* packetData, size_t packetLength)
{
	std::string message(reinterpret_cast<const char*>(packetData), packetLength);

	// Check if this is an authentication response
	if (message.substr(0, 14) == "AUTH_RESPONSE:")
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

std::string NetworkManager::analyzeConnectionQuality()
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
		bandwidthUsagePercent = (bandwidthStats.averageSendRateBps * 100.0f) / outgoingBandwidthLimit;

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
}

std::string NetworkManager::generateDiagnosticsReport()
{
	std::lock_guard<std::mutex> lock(networkMutex);

	std::stringstream report;
	report << "============ MMO CLIENT NETWORK DIAGNOSTICS REPORT ============\n";

	// Current state
	report << "Current State: " << getConnectionStateString() << "\n";
	report << "Server: " << serverAddress << ":" << serverPort << "\n";

	// Connection metrics
	report << "\n--- Connection Metrics ---\n";
	report << "Connected: " << (isConnected ? "Yes" : "No") << "\n";

	uint32_t uptime = 0;
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
	report << "Queued Packets: " << getQueuedPacketCount() << "\n";

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
}

void NetworkManager::prepareForZoneTransition()
{
	std::lock_guard<std::mutex> lock(networkMutex);

	// Mark that we're in zone transition
	inZoneTransition = true;
	zoneTransitionStartTime = getCurrentTimeMs();

	// Update diagnostics
	diagnostics.zoneTransitionCount++;

	// Temporarily increase timeouts during zone loading
	uint32_t originalTimeout = serverResponseTimeout;
	serverResponseTimeout = 30000; // 30 seconds for zone transition

	// Flush non-critical packet queue
	{
		std::lock_guard<std::mutex> queueLock(queueMutex);
		std::priority_queue<QueuedPacket> tempQueue;

		// Keep only critical packets
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
	}

	logger.info("Prepared for zone transition - increased timeout to 30s, kept " + std::to_string(getQueuedPacketCount()) + " critical packets");

	// Schedule timeout reset after zone transition
	std::thread(
	        [this, originalTimeout]()
	        {
		        std::this_thread::sleep_for(std::chrono::seconds(30));
		        std::lock_guard<std::mutex> lock(networkMutex);

		        // Record zone loading time
		        uint32_t zoneLoadTime = getCurrentTimeMs() - zoneTransitionStartTime;

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
	        })
	        .detach();
}

void NetworkManager::setAreaOfInterest(const std::string& areaId, int radius)
{
	std::string aoiMessage = "AOI:" + areaId + ":" + std::to_string(radius);
	sendPacketWithPriority(aoiMessage, true, PRIORITY_HIGH);

	// Store current area of interest
	currentAreaOfInterest = areaId;
	areaOfInterestRadius = radius;

	logger.debug("Set area of interest: " + areaId + ", radius=" + std::to_string(radius));
}

void NetworkManager::adaptToHighPopulationDensity(bool highDensity)
{
	std::lock_guard<std::mutex> lock(networkMutex);

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
}

void NetworkManager::setPriorityMode(PriorityMode mode)
{
	if (mode == currentPriorityMode)
		return;

	std::lock_guard<std::mutex> lock(networkMutex);
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
}

void NetworkManager::setServerRequestedThrottling(int throttleLevel)
{
	if (throttleLevel == serverThrottleLevel)
		return;

	std::lock_guard<std::mutex> lock(networkMutex);
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
		bandwidthTokenBucket = TokenBucket(throttledLimit, throttledLimit * 1.5f);

		logger.warning("Server requested throttling level " + std::to_string(throttleLevel) + " - reducing bandwidth to " + std::to_string(throttleFactor * 100) + "%");
	}
	else
	{
		// Server has removed throttling request
		bandwidthThrottlingEnabled = false;

		// Reset token buckets
		bandwidthTokenBucket = TokenBucket(outgoingBandwidthLimit, outgoingBandwidthLimit * 1.5f);

		logger.info("Server removed throttling request - resuming normal bandwidth");
	}
}

void NetworkManager::resetStats()
{
	std::lock_guard<std::mutex> lock(networkMutex);

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
}

uint32_t NetworkManager::getCurrentTimeMs()
{
	return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
