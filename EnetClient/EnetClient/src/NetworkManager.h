#pragma once

#include <atomic>
#include <chrono>
#include <enet/enet.h>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Constants.h"
#include "Logger.h"
#include "AuthManager.h"

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

	NetworkManager(Logger& logger);
	~NetworkManager();

	bool initialize();
	bool connectToServer(const char* address, uint16_t port);
	void disconnect(bool showMessage = true);
	bool reconnectToServer();

	// Packet sending
	void sendPacket(const std::string& message, bool reliable = true);
	void sendPacketWithPriority(const std::string& message, bool reliable, uint8_t priority);
	void sendPositionUpdate(float x, float y, float z, float lastX, float lastY, float lastZ, bool useCompressedUpdates, float movementThreshold);

	// Network processing
	void update(const std::function<void()>& updatePositionCallback, const std::function<void(const ENetPacket*)>& handlePacketCallback, const std::function<void()>& disconnectCallback);
	void sendPing();
	void checkConnectionHealth(const std::function<void()>& disconnectCallback);

	// Configuration
	void setServerResponseTimeout(uint32_t timeoutMs)
	{
		serverResponseTimeout = timeoutMs;
	}

	void setConnectionCheckInterval(uint32_t intervalMs)
	{
		connectionCheckInterval = intervalMs;
	}

	void configureAdaptiveTimeout(uint32_t initial, uint32_t max, uint32_t pingFailures);
	void configureBandwidthManagement(uint32_t outgoingLimitBps, uint32_t throttledLimitBps, bool enableThrottling);
	void setPacketQueueing(bool enabled, uint32_t maxSize = 1000, uint32_t maxAgeMs = 30000);
	void configureMessageType(const std::string& prefix, uint8_t priority, bool canThrottle, float throttleMultiplier);

	void setReconnectAttempts(uint32_t attempts)
	{
		reconnectAttempts = attempts;
	}

	void setHeartbeatInterval(uint32_t intervalMs)
	{
		heartbeatIntervalMs = intervalMs;
	}

	void configureCompression(bool enabled, uint32_t minSize = 100, float minRatio = 0.8f);

	// MMO-specific features
	void prepareForZoneTransition();
	void setAreaOfInterest(const std::string& areaId, int radius);
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

	uint16_t getServerPort() const
	{
		return serverPort;
	}

	uint32_t getQueuedPacketCount();
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
		serverAddress = address;
	}

	void setServerPort(uint16_t port)
	{
		serverPort = port;
	}

	void setConnected(bool connected)
	{
		isConnected = connected;
	}

	// Set the auth manager
	void setAuthManager(std::shared_ptr<AuthManager> auth) { 
        authManager = auth; 
    }

private:
	// Connection states
	enum class ConnectionState
	{
		DISCONNECTED,
		CONNECTING,
		CONNECTED,
		RECONNECTING,
		DISCONNECTING
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
	uint32_t lastPingTime = 0;
	uint32_t lastPingSentTime = 0;
	uint32_t lastHeartbeatSent = 0;
	uint32_t pingMs = 0;
	uint32_t lastServerResponseTime = 0;
	uint32_t lastNetworkActivity = 0;
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
	uint32_t packetsSent = 0;
	uint32_t packetsReceived = 0;
	uint32_t bytesSent = 0;
	uint32_t bytesReceived = 0;

	// Bandwidth management
	struct BandwidthStats
	{
		uint32_t bytesSentLastSecond = 0;
		uint32_t bytesReceivedLastSecond = 0;
		uint32_t currentSecondStart = 0;
		float averageSendRateBps = 0;
		float averageReceiveRateBps = 0;
		std::vector<uint32_t> sendRateHistory;
		std::vector<uint32_t> receiveRateHistory;
		uint32_t maxHistorySize = 30;
	};

	BandwidthStats bandwidthStats;
	uint32_t outgoingBandwidthLimit = 0;
	uint32_t throttledOutgoingLimit = 0;
	bool bandwidthThrottlingEnabled = false;

	// Message type configuration
	struct MessageTypeConfig
	{
		MessageCategory category;
		uint8_t priority;
		bool canThrottle;
		float throttleMultiplier;
	};

	std::unordered_map<std::string, MessageTypeConfig> messageTypeConfigs;

	// Token bucket for rate limiting
	struct TokenBucket
	{
		float tokens;
		float rate;
		float maxTokens;
		uint32_t lastUpdate;

		// Add a default constructor
		TokenBucket()
		      : tokens(0), rate(0), maxTokens(0), lastUpdate(0)
		{
		}

		TokenBucket(float rate, float maxTokens)
		      : tokens(maxTokens), rate(rate), maxTokens(maxTokens), lastUpdate(0)
		{
		}

		bool consumeTokens(float amount, uint32_t currentTime);
	};

	TokenBucket bandwidthTokenBucket{ 0, 0 };
	std::unordered_map<MessageCategory, TokenBucket> categoryTokenBuckets;

	// Packet queuing system
	struct QueuedPacket
	{
		std::string message;
		bool reliable;
		uint32_t timestamp;
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

	std::priority_queue<QueuedPacket> outgoingQueue;
	bool queuePacketsDuringDisconnection = true;
	uint32_t maxQueueSize = 1000;
	uint32_t maxQueuedPacketAge = 30000;

	// Message compression
	bool compressionEnabled = false;
	uint32_t minSizeForCompression = 100;
	float minCompressionRatio = 0.8f;

	// MMO-specific features
	bool inZoneTransition = false;
	uint32_t zoneTransitionStartTime = 0;
	uint32_t zoneTransitionTimeout = 30000;
	std::string currentAreaOfInterest;
	int areaOfInterestRadius = 0;
	bool highPopulationDensity = false;
	uint32_t positionPrecision = 2;
	uint32_t positionUpdateInterval = 100;

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
		uint32_t timeToReconnectMs = 0;

		// Ping sequence tracking
		uint32_t pingSent = 0;
		uint32_t pingReceived = 0;
		uint32_t pingLost = 0;

		// Error tracking
		uint32_t packetCreationErrors = 0;
		uint32_t packetSendErrors = 0;
		uint32_t enetServiceErrors = 0;
		uint32_t lastErrorTime = 0;

		// Time tracking
		uint32_t connectionStartTime = 0;
		uint32_t totalConnectedTimeMs = 0;
		uint32_t lastConnectionTime = 0;
		uint32_t longestDowntimeMs = 0;

		// MMO-specific stats
		uint32_t zoneTransitionCount = 0;
		uint32_t avgZoneLoadingTimeMs = 0;
		float combatPacketSuccessRate = 100.0f;
		uint32_t avgCombatActionLatencyMs = 0;
		uint32_t highDensityAreaCount = 0;
		float highDensityPositionUpdatesPerSecond = 0.0f;
		float serverResponseConsistency = 1.0f;

		void reset();
	};

	NetworkDiagnostics diagnostics;

	// Dependencies
	Logger& logger;
	std::mutex networkMutex;
	std::mutex queueMutex;

	// Private methods
	uint32_t getCurrentTimeMs();
	void updateBandwidthStats();
	bool isPacketSendAllowed(const std::string& message, uint32_t size);
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
