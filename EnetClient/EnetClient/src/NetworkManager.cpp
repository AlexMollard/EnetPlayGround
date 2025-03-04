#include "NetworkManager.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

NetworkManager::NetworkManager(Logger& logger)
      : logger(logger)
{
	logger.debug("Initializing NetworkManager");
}

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

	logger.debug("NetworkManager destroyed");
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

	// Create client host
	client = enet_host_create(nullptr, // Create a client host (no bind address)
	        1,                         // Allow 1 outgoing connection
	        2,                         // Use 2 channels (reliable and unreliable)
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

bool NetworkManager::connectToServer(const char* address, uint16_t port)
{
	std::lock_guard<std::mutex> lock(networkMutex);

	if (isConnected)
	{
		logger.warning("Already connected, disconnecting first");
		disconnect(false);
	}

	serverAddress = address;
	serverPort = port;

	// Create ENet address
	ENetAddress enetAddress;
	enet_address_set_host(&enetAddress, address);
	enetAddress.port = port;

	logger.debug("Connecting to server at " + std::string(address) + ":" + std::to_string(port) + "...");

	// Connect to the server
	server = enet_host_connect(client, &enetAddress, 2, 0);

	if (server == nullptr)
	{
		logger.error("Failed to initiate connection to server");
		return false;
	}

	// Wait for connection establishment with a timeout
	// but process events in small chunks to avoid blocking too long
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
				isConnected = true;
				lastNetworkActivity = getCurrentTimeMs();
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
			logger.error("Error while connecting: " + std::to_string(result));
			enet_peer_reset(server);
			server = nullptr;
			return false;
		}
	}

	// Connection timed out
	logger.error("Connection to server failed (timeout)");
	enet_peer_reset(server);
	server = nullptr;
	return false;
}

void NetworkManager::disconnect(bool showMessage)
{
	std::lock_guard<std::mutex> lock(networkMutex);

	if (isConnected && server != nullptr)
	{
		if (showMessage)
		{
			logger.info("Disconnecting from server...");
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
	waitingForPingResponse = false;
}

bool NetworkManager::reconnectToServer()
{
	if (isConnected)
	{
		disconnect(false);
	}

	logger.info("Attempting to reconnect to server...");
	reconnecting = true;

	for (int attempt = 1; attempt <= RECONNECT_ATTEMPTS; attempt++)
	{
		logger.debug("Reconnection attempt " + std::to_string(attempt) + " of " + std::to_string(RECONNECT_ATTEMPTS));

		if (connectToServer(serverAddress.c_str(), serverPort))
		{
			logger.info("Reconnection successful!");
			reconnecting = false;
			return true;
		}

		// Wait before next attempt
		std::this_thread::sleep_for(std::chrono::seconds(2));
	}

	logger.error("Failed to reconnect after " + std::to_string(RECONNECT_ATTEMPTS) + " attempts");
	reconnecting = false;
	return false;
}

void NetworkManager::sendPacket(const std::string& message, bool reliable)
{
	if (!isConnected)
	{
		return;
	}

	// Create packet outside the lock
	ENetPacket* packet = enet_packet_create(message.c_str(), message.length(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

	bool sendSuccess = false;
	ENetPeer* peerCopy = nullptr;

	{
		std::lock_guard<std::mutex> lock(networkMutex);

		if (!isConnected || server == nullptr)
		{
			// We need to destroy the packet if we can't send it
			sendSuccess = false;
		}
		else
		{
			// Send packet while holding the lock
			sendSuccess = (enet_peer_send(server, 0, packet) == 0);

			if (sendSuccess)
			{
				// Update stats
				packetsSent++;
				bytesSent += message.length();
				lastNetworkActivity = getCurrentTimeMs();
			}
		}
	}

	// Log packet outside the lock
	if (sendSuccess)
	{
		// Don't spam logs with position updates and pings
		if (message.substr(0, 9) != "POSITION:" && message.substr(0, 5) != "PING:")
		{
			logger.logNetworkEvent("Sent: " + message);
		}
	}
	else
	{
		logger.error("Failed to send packet: " + message);
		enet_packet_destroy(packet); // We need to destroy the packet if send failed
	}
}

void NetworkManager::sendPositionUpdate(float x, float y, float z, float lastX, float lastY, float lastZ, bool useCompressedUpdates, float movementThreshold)
{
	if (!isConnected)
		return;

	// Calculate movement distance
	float moveDistance = std::sqrt(pow(x - lastX, 2) + pow(y - lastY, 2) + pow(z - lastZ, 2));

	// Only send if position has changed significantly
	if (moveDistance < movementThreshold)
		return;

	// Prepare position update message
	std::string positionMsg;

	if (useCompressedUpdates)
	{
		// Delta compression: Send only the coordinates that changed significantly
		positionMsg = "MOVE_DELTA:";
		bool addedCoordinate = false;

		if (std::abs(x - lastX) >= movementThreshold)
		{
			positionMsg += "x" + std::to_string(x);
			addedCoordinate = true;
		}

		if (std::abs(y - lastY) >= movementThreshold)
		{
			if (addedCoordinate)
				positionMsg += ",";
			positionMsg += "y" + std::to_string(y);
			addedCoordinate = true;
		}

		if (std::abs(z - lastZ) >= movementThreshold)
		{
			if (addedCoordinate)
				positionMsg += ",";
			positionMsg += "z" + std::to_string(z);
		}
	}
	else
	{
		// Full position update (fallback method)
		positionMsg = "POSITION:" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
	}

	// Use unreliable packets for position updates
	sendPacket(positionMsg, false);
}

void NetworkManager::update(const std::function<void()>& updatePositionCallback, const std::function<void(const ENetPacket*)>& handlePacketCallback, const std::function<void()>& disconnectCallback)
{
	// Use a scoped lock to ensure we don't hold the lock for too long
	{
		std::lock_guard<std::mutex> lock(networkMutex);

		// Only process network updates if connected
		if (!isConnected || client == nullptr)
		{
			return;
		}

		// Process a LIMITED number of pending events per frame
		// to prevent UI blocking when many packets arrive at once
		ENetEvent event;
		int eventCount = 0;
		const int MAX_EVENTS_PER_UPDATE = 10; // Process at most 10 events per update

		// Use a 0 timeout to ensure we don't block
		while (eventCount < MAX_EVENTS_PER_UPDATE && enet_host_service(client, &event, 0) > 0)
		{
			eventCount++;

			switch (event.type)
			{
				case ENET_EVENT_TYPE_RECEIVE:
					// Update stats
					packetsReceived++;
					bytesReceived += event.packet->dataLength;
					lastNetworkActivity = getCurrentTimeMs();
					waitingForPingResponse = false;

					// Process packet with callback - making a copy if needed for async processing
					if (handlePacketCallback)
					{
						handlePacketCallback(event.packet);
					}

					enet_packet_destroy(event.packet);
					break;

				case ENET_EVENT_TYPE_DISCONNECT:
					logger.error("Disconnected from server");
					isConnected = false;
					server = nullptr;

					// Schedule disconnect callback to be called outside the lock
					if (disconnectCallback)
					{
						// Use a copy to ensure callback is called outside lock
						auto callbackCopy = disconnectCallback;
						std::thread([callbackCopy]() { callbackCopy(); }).detach();
					}
					break;

				default:
					break;
			}
		}
	} // End of mutex lock scope

	// Call position update callback outside the lock
	if (updatePositionCallback && isConnected)
	{
		updatePositionCallback();
	}
}

void NetworkManager::sendPing()
{
	if (!isConnected)
		return;

	uint32_t currentTime = getCurrentTimeMs();
	bool shouldSendPing = false;
	bool shouldLogTimeout = false;

	{
		std::lock_guard<std::mutex> lock(networkMutex);

		// Check if it's time to send a ping
		if (currentTime - lastPingTime >= PING_INTERVAL_MS)
		{
			lastPingTime = currentTime;
			lastPingSentTime = currentTime;
			waitingForPingResponse = true;
			shouldSendPing = true;
		}

		// Check for ping response timeout
		if (waitingForPingResponse && currentTime - lastPingSentTime > serverResponseTimeout)
		{
			shouldLogTimeout = true;
			waitingForPingResponse = false; // Reset flag to prevent repeated warnings
		}
	}

	if (shouldSendPing)
	{
		std::string pingMsg = "PING:" + std::to_string(currentTime);
		sendPacket(pingMsg, false); // Using unreliable for pings
	}

	if (shouldLogTimeout)
	{
		logger.error("Ping response timeout - Server may have disconnected");
	}
}

void NetworkManager::checkConnectionHealth(const std::function<void()>& disconnectCallback)
{
	if (!isConnected || reconnecting)
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
			shouldDisconnect = true;
		}
	}

	// Call the callback outside the lock
	if (shouldDisconnect && disconnectCallback)
	{
		disconnectCallback();
	}
}

uint32_t NetworkManager::getCurrentTimeMs()
{
	return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
