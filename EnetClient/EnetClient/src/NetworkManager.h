#pragma once

#include <atomic>
#include <chrono>
#include <enet/enet.h>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "AuthManager.h"
#include "Constants.h"
#include "Logger.h"
#include "PacketManager.h"
#include "ThreadManager.h"

// Advanced diagnostics
struct NetworkDiagnostics
{
	// Connection quality metrics
	uint32_t avgPingMs = 0;
	uint32_t minPingMs = UINT32_MAX;
	uint32_t maxPingMs = 0;
	float pingStdDeviation = 0.0f;
	uint32_t packetLossPercentage = 0;

	// Connection stability
	uint32_t disconnectionCount = 0;
	uint32_t reconnectionCount = 0;
	uint32_t reconnectionAttempts = 0;
	uint64_t timeToReconnectMs = 0;

	// Ping sequence tracking
	uint32_t pingSent = 0;
	uint32_t pingReceived = 0;
	uint32_t pingLost = 0;

	// Error tracking
	uint32_t packetCreationErrors = 0;
	uint32_t packetSendErrors = 0;
	uint32_t enetServiceErrors = 0;
	uint64_t lastErrorTime = 0;

	// Time tracking
	uint64_t connectionStartTime = 0;
	uint64_t totalConnectedTimeMs = 0;
	uint64_t lastConnectionTime = 0;
	uint64_t longestDowntimeMs = 0;

	// MMO-specific stats
	uint32_t zoneTransitionCount = 0;
	uint64_t avgZoneLoadingTimeMs = 0;
	float combatPacketSuccessRate = 100.0f;
	uint32_t avgCombatActionLatencyMs = 0;
	uint32_t highDensityAreaCount = 0;
	float highDensityPositionUpdatesPerSecond = 0.0f;
	float serverResponseConsistency = 1.0f;

	void reset();
};

struct QueuedPacket
{
	std::string message;
	bool reliable;
	uint64_t timestamp;
	uint8_t priority;
	uint32_t retryCount;

	QueuedPacket(const std::string& msg, bool rel, uint8_t pri = 128)
	      : message(msg), reliable(rel), timestamp(0), priority(pri), retryCount(0)
	{
	}

	bool operator<(const QueuedPacket& other) const
	{
		return priority != other.priority ? priority > other.priority : timestamp > other.timestamp;
	}
};

// Bandwidth management
struct BandwidthStats
{
	size_t bytesSentLastSecond = 0;
	size_t bytesReceivedLastSecond = 0;
	uint64_t currentSecondStart = 0;
	float averageSendRateBps = 0;
	float averageReceiveRateBps = 0;
	std::vector<size_t> sendRateHistory;
	std::vector<size_t> receiveRateHistory;
	uint32_t maxHistorySize = 30;
};

// Message categories
enum class MessageCategory
{
	CRITICAL,  // Auth, connection management
	GAMEPLAY,  // Important gameplay events
	POSITION,  // Position updates
	CHAT,      // Chat messages
	TELEMETRY, // Stats, analytics
	MISC       // Miscellaneous
};

// Message type configuration
struct MessageTypeConfig
{
	MessageCategory category;
	uint8_t priority;
	bool canThrottle;
	float throttleMultiplier;
};

namespace GameResources
{
	// Helper to create resource IDs with the right type
	template<typename T>
	ResourceId create(const std::string& name)
	{
		return ThreadManager::createResourceId<T>(name);
	}

	// This is basically a struct to control task execution
	struct NetworkState
	{
	};

	const ResourceId networkResourceId = create<NetworkState>("NetworkState");
	const ResourceId queueResourceId = create<QueuedPacket>("PacketQueue");
	const ResourceId bandwidthResourceId = create<BandwidthStats>("BandwidthStats");
	const ResourceId configResourceId = create<MessageTypeConfig>("MessageConfig");
} // namespace GameResources

/**
 * NetworkManager - Handles ENet networking for MMO environments
 */
class NetworkManager
{
public:
	// Packet priority levels
	static constexpr uint8_t PRIORITY_CRITICAL = 0;
	static constexpr uint8_t PRIORITY_HIGH = 64;
	static constexpr uint8_t PRIORITY_NORMAL = 128;
	static constexpr uint8_t PRIORITY_LOW = 192;

	NetworkManager(std::shared_ptr<ThreadManager> threadManager = nullptr);
	~NetworkManager();

	bool initialize();
	bool connectToServer(const char* address, uint16_t port);
	void disconnect(bool tellServer = true);
	bool reconnectToServer();

	void updateConnectionDiagnostics(uint64_t connectStartTime);

	// Packet sending
	void sendPacket(std::shared_ptr<GameProtocol::Packet> packet, bool reliable);
	void queuePacket(const std::string& data, bool reliable, uint8_t priority);
	void sendPacketWithPriority(std::shared_ptr<GameProtocol::Packet> packet, bool reliable, uint8_t priority);

	// Network processing
	void update(const std::function<void()>& updatePositionCallback, const std::function<void(const ENetPacket*)>& handlePacketCallback, const std::function<void()>& disconnectCallback);
	void sendPing();
	void checkConnectionHealth();

	// Configuration
	void setServerResponseTimeout(uint32_t timeoutMs)
	{
		threadManager->scheduleResourceTask({ GameResources::networkResourceId }, [this, timeoutMs]() { serverResponseTimeout = timeoutMs; });
	}

	void setConnectionCheckInterval(uint32_t intervalMs)
	{
		threadManager->scheduleResourceTask({ GameResources::configResourceId }, [this, intervalMs]() { connectionCheckInterval = intervalMs; });
	}

	void configureAdaptiveTimeout(uint32_t initial, uint32_t max, uint32_t pingFailures);
	void configureBandwidthManagement(size_t outgoingLimitBps, size_t throttledLimitBps, bool enableThrottling);
	void setPacketQueueing(bool enabled, size_t maxSize = 1000, uint32_t maxAgeMs = 30000);
	void configureMessageType(const std::string& prefix, uint8_t priority, bool canThrottle, float throttleMultiplier);

	void setReconnectAttempts(uint32_t attempts)
	{
		threadManager->scheduleResourceTask({ GameResources::configResourceId }, [this, attempts]() { reconnectAttempts = attempts; });
	}

	void setHeartbeatInterval(uint32_t intervalMs)
	{
		threadManager->scheduleResourceTask({ GameResources::configResourceId }, [this, intervalMs]() { heartbeatIntervalMs = intervalMs; });
	}

	void configureCompression(bool enabled, uint32_t minSize = 100, float minRatio = 0.8f);

	// MMO-specific features
	void prepareForZoneTransition();
	void adaptToHighPopulationDensity(bool highDensity);

	// Priority modes
	enum class PriorityMode
	{
		NORMAL,
		COMBAT,
		CRAFTING
	};
	void setPriorityMode(PriorityMode mode);
	void setServerRequestedThrottling(int throttleLevel);

	// Queue management
	void processQueuedPackets();
	void clearPacketQueue();

	// Diagnostics
	std::string analyzeConnectionQuality();
	std::string generateDiagnosticsReport();
	void resetStats();

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

	size_t getPacketsSent() const
	{
		return packetsSent;
	}

	size_t getPacketsReceived() const
	{
		return packetsReceived;
	}

	size_t getBytesSent() const
	{
		return bytesSent;
	}

	size_t getBytesReceived() const
	{
		return bytesReceived;
	}

	const std::string& getServerAddress() const
	{
		return serverAddress;
	}

	uint16_t getServerPort() const
	{
		return serverPort;
	}

	size_t getQueuedPacketCount();
	std::string getConnectionStateString() const;

	bool isNetworkDegraded() const
	{
		return networkDegraded;
	}

	float getPacketLossEstimate() const
	{
		return packetLossEstimate;
	}

	float getNetworkJitter() const
	{
		return jitter;
	}

	// Setters
	void setServerAddress(const std::string& address)
	{
		threadManager->scheduleResourceTask({ GameResources::networkResourceId }, [this, address]() { serverAddress = address; });
	}

	void setServerPort(uint16_t port)
	{
		threadManager->scheduleResourceTask({ GameResources::networkResourceId }, [this, port]() { serverPort = port; });
	}

	void setConnected(bool connected)
	{
		threadManager->scheduleResourceTask({ GameResources::networkResourceId }, [this, connected]() { isConnected = connected; });
	}

	// Set the auth manager
	void setAuthManager(std::shared_ptr<AuthManager> auth)
	{
		authManager = auth;
	}

	/**
	 * Get the server peer for direct packet sending
	 * @return Pointer to the connected server peer, or nullptr if not connected
	 */
	ENetPeer* getServerPeer() const
	{
		std::lock_guard<std::mutex> guard(enetMutex);

		// Only return the server peer if we're properly connected
		if (isConnected && connectionState == ConnectionState::CONNECTED)
		{
			return server;
		}

		return nullptr;
	}

	PacketManager* GetPacketManager()
	{
		return &packetManager;
	}

	ENetPeer* getServer()
	{
		return server;
	}

	const bool isCompressionEnabled() const
	{
		return compressionEnabled;
	}

private:
	// Mutexes for thread safety
	mutable std::mutex enetMutex;                    // Protects all ENet operations
	mutable std::mutex connectionStateMutex;         // Protects connection state variables
	mutable std::mutex diagnosticsMutex;             // Protects diagnostics data
	mutable std::mutex bandwidthMutex;               // Protects bandwidth management
	mutable std::mutex queueMutex;                   // Protects the packet queue
	std::atomic<bool> connectionInProgress{ false }; // Prevent concurrent connection attempts

	PacketManager packetManager;

	// Connection states
	enum class ConnectionState
	{
		DISCONNECTED,
		CONNECTING,
		CONNECTED,
		RECONNECTING,
		DISCONNECTING
	};

	// ENet components
	ENetHost* client = nullptr;
	ENetPeer* server = nullptr;

	// Connection state
	std::atomic<bool> isConnected{ false };
	std::atomic<bool> reconnecting{ false };
	ConnectionState connectionState = ConnectionState::DISCONNECTED;
	std::string serverAddress = DEFAULT_SERVER;
	uint16_t serverPort = DEFAULT_PORT;
	PriorityMode currentPriorityMode = PriorityMode::NORMAL;
	int serverThrottleLevel = 0;

	// Connection monitoring
	uint64_t lastPingTime = 0;
	uint64_t lastPingSentTime = 0;
	uint64_t lastHeartbeatSent = 0;
	uint32_t pingMs = 0;
	uint64_t lastServerResponseTime = 0;
	uint64_t lastNetworkActivity = 0;
	uint32_t connectionCheckInterval = 1000;
	uint32_t heartbeatIntervalMs = 2000;
	uint32_t pingIntervalMs = 5000;
	uint32_t reconnectAttempts = 3;

	// Timeout management
	uint32_t initialServerResponseTimeout = 5000;
	uint32_t maxServerResponseTimeout = 30000;
	uint32_t serverResponseTimeout = 5000;
	uint32_t adaptiveTimeoutMultiplier = 1;
	bool waitingForPingResponse = false;

	// Authentication
	std::shared_ptr<AuthManager> authManager;

	// Health monitoring
	uint32_t successivePingFailures = 0;
	uint32_t maxPingFailures = 3;
	std::vector<uint32_t> pingHistory;
	uint32_t pingHistorySize = 10;

	// Network quality metrics
	float packetLossEstimate = 0.0f;
	float jitter = 0.0f;
	bool networkDegraded = false;
	uint32_t pingSequence = 0;

	// Network statistics
	size_t packetsSent = 0;
	size_t packetsReceived = 0;
	size_t bytesSent = 0;
	size_t bytesReceived = 0;

	BandwidthStats bandwidthStats;
	size_t outgoingBandwidthLimit = 0;
	size_t throttledOutgoingLimit = 0;
	bool bandwidthThrottlingEnabled = false;

	std::unordered_map<std::string, MessageTypeConfig> messageTypeConfigs;

	// Token bucket for rate limiting
	struct TokenBucket
	{
		float tokens;
		float rate;
		float maxTokens;
		uint64_t lastUpdate;

		// Add a default constructor
		TokenBucket()
		      : tokens(0), rate(0), maxTokens(0), lastUpdate(0)
		{
		}

		TokenBucket(float rate, float maxTokens)
		      : tokens(maxTokens), rate(rate), maxTokens(maxTokens), lastUpdate(0)
		{
		}

		bool consumeTokens(float amount, uint64_t currentTime);
	};

	TokenBucket bandwidthTokenBucket{ 0, 0 };
	std::unordered_map<MessageCategory, TokenBucket> categoryTokenBuckets;

	// Packet queuing system
	std::priority_queue<QueuedPacket> outgoingQueue;
	bool queuePacketsDuringDisconnection = true;
	size_t maxQueueSize = 1000;
	uint32_t maxQueuedPacketAge = 30000;

	// Message compression
	bool compressionEnabled = false;
	size_t minSizeForCompression = 100;
	float minCompressionRatio = 0.8f;

	// MMO-specific features
	bool inZoneTransition = false;
	uint64_t zoneTransitionStartTime = 0;
	uint32_t zoneTransitionTimeout = 30000;
	std::string currentAreaOfInterest;
	int areaOfInterestRadius = 0;
	bool highPopulationDensity = false;
	uint32_t positionPrecision = 2;
	uint32_t positionUpdateInterval = 100;

	NetworkDiagnostics diagnostics;

	std::shared_ptr<ThreadManager> threadManager;

	// Dependencies
	Logger& logger = Logger::getInstance();

	// Private methods
	uint64_t getCurrentTimeMs();
	void sendChatMessage(const std::string& sender, const std::string& message, bool isGlobal);
	void sendSystemMessage(const std::string& message);
	void sendCommand(const std::string& command, const std::vector<std::string>& args);
	void sendTeleport(const Position& position);
	void updateBandwidthStats();
	bool isPacketSendAllowed(const std::string& message, float size);
	MessageTypeConfig getMessageConfig(const std::string& message);
	void sendHeartbeat();
	void cleanExpiredPackets();
	std::string compressMessage(const std::string& message, float* ratio = nullptr);
	std::string decompressMessage(const std::string& compressedData);
	void updatePingStatistics(uint32_t pingTime);
	void calculateJitter();
	void estimatePacketLoss();
	bool processReceivedPacket(const void* packetData, size_t packetLength);
};
