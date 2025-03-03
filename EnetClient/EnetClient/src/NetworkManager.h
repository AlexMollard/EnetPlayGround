#pragma once

#include <enet/enet.h>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "Constants.h"
#include "Logger.h"

/**
 * NetworkManager handles all ENet-related functionality including
 * connection management, packet sending/receiving, and network statistics.
 */
class NetworkManager
{
public:
	/**
     * Constructor
     * @param logger Reference to the logger
     * @param gameState Shared pointer to the game state
     */
	NetworkManager(Logger& logger);

	/**
     * Destructor - cleans up ENet resources
     */
	~NetworkManager();

	/**
     * Initialize ENet library and create host
     * @return True if initialization succeeded
     */
	bool initialize();

	/**
     * Connect to a server
     * @param address Server address
     * @param port Server port
     * @return True if connection attempt was successful
     */
	bool connectToServer(const char* address, uint16_t port);

	/**
     * Disconnect from the server
     * @param showMessage Whether to show a disconnect message
     */
	void disconnect(bool showMessage = true);

	/**
     * Attempt to reconnect to the server
     * @return True if reconnection was initiated
     */
	bool reconnectToServer();

	/**
     * Send a packet to the server
     * @param message Message to send
     * @param reliable Whether to use reliable delivery
     */
	void sendPacket(const std::string& message, bool reliable = true);

	/**
     * Send a player position update to the server
     * @param x X position
     * @param y Y position
     * @param z Z position
     * @param lastX Last sent X position
     * @param lastY Last sent Y position
     * @param lastZ Last sent Z position
     * @param useCompressedUpdates Whether to use delta compression
     * @param movementThreshold Minimum movement distance to trigger update
     */
	void sendPositionUpdate(float x, float y, float z, float lastX, float lastY, float lastZ, bool useCompressedUpdates, float movementThreshold);

	/**
     * Update network state - process incoming packets, etc.
     * @param updatePositionCallback Callback for when position updates should be sent
     * @param handlePacketCallback Callback for processing received packets
     * @param disconnectCallback Callback for when server disconnects
     */
	void update(const std::function<void()>& updatePositionCallback, const std::function<void(const ENetPacket*)>& handlePacketCallback, const std::function<void()>& disconnectCallback);

	/**
     * Send ping to server to check connection
     */
	void sendPing();

	/**
     * Check connection health and handle timeouts
     * @param disconnectCallback Callback for when connection is lost
     */
	void checkConnectionHealth(const std::function<void()>& disconnectCallback);

	/**
     * Set the server response timeout
     * @param timeoutMs Timeout in milliseconds
     */
	void setServerResponseTimeout(uint32_t timeoutMs)
	{
		serverResponseTimeout = timeoutMs;
	}

	/**
     * Set the connection check interval
     * @param intervalMs Interval in milliseconds
     */
	void setConnectionCheckInterval(uint32_t intervalMs)
	{
		connectionCheckInterval = intervalMs;
	}

	// Getters
	bool isConnectedToServer() const
	{
		return isConnected;
	}

	bool clientSetup() const
	{
		return client != nullptr;
	}

	uint32_t getPing() const
	{
		return pingMs;
	}

	uint32_t getPacketsSent() const
	{
		return packetsSent;
	}

	uint32_t getPacketsReceived() const
	{
		return packetsReceived;
	}

	uint32_t getBytesSent() const
	{
		return bytesSent;
	}

	uint32_t getBytesReceived() const
	{
		return bytesReceived;
	}

	const std::string& getServerAddress() const
	{
		return serverAddress;
	}

	void setServerAddress(const std::string& address)
	{
		serverAddress = address;
	}

	uint16_t getServerPort() const
	{
		return serverPort;
	}

	void setServerPort(uint16_t port)
	{
		serverPort = port;
	}

	// Setters
	void setConnected(bool connected)
	{
		isConnected = connected;
	}

private:
	// ENet components
	ENetHost* client = nullptr;
	ENetPeer* server = nullptr;

	// Connection state
	bool isConnected = false;
	bool reconnecting = false;
	std::string serverAddress = DEFAULT_SERVER;
	uint16_t serverPort = DEFAULT_PORT;

	// Connection monitoring
	uint32_t lastPingTime = 0;
	uint32_t lastPingSentTime = 0;
	uint32_t pingMs = 0;
	uint32_t lastServerResponseTime = 0;
	uint32_t lastNetworkActivity = 0;
	uint32_t connectionCheckInterval = 1000;
	uint32_t serverResponseTimeout = 5000;
	bool waitingForPingResponse = false;

	// Network statistics
	uint32_t packetsSent = 0;
	uint32_t packetsReceived = 0;
	uint32_t bytesSent = 0;
	uint32_t bytesReceived = 0;

	// Dependencies
	Logger& logger;
	std::mutex networkMutex;

	/**
     * Get current time in milliseconds
     * @return Current time in milliseconds
     */
	uint32_t getCurrentTimeMs();
};
