#pragma once
#include <set>

struct Position
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	bool operator==(const Position& other) const
	{
		return x == other.x && y == other.y && z == other.z;
	}

	bool operator!=(const Position& other) const
	{
		return !(*this == other);
	}

	float distanceTo(const Position& other) const
	{
		float dx = x - other.x;
		float dy = y - other.y;
		float dz = z - other.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}
};

// Player authentication data
struct AuthData
{
	std::string username;
	std::string passwordHash;
	uint32_t playerId;
	std::time_t lastLoginTime;
	std::time_t registrationTime;
	Position lastPosition;
	std::string lastIpAddress;
	uint32_t loginCount;
	bool isAdmin;
	std::string securityQuestion;
	std::string securityAnswerHash;
};

// Player session data
#include <enet/enet.h>
struct Player
{
	uint32_t id;
	std::string name;
	Position position;
	Position lastValidPosition;
	ENetPeer* peer;
	uint32_t lastUpdateTime;
	uint32_t connectionStartTime;
	uint32_t failedAuthAttempts;
	uint32_t totalBytesReceived;
	uint32_t totalBytesSent;
	uint32_t pingMs;
	bool isAuthenticated;
	bool isAdmin;
	std::string ipAddress;
	std::set<uint32_t> visiblePlayers; // IDs of players currently visible to this player
	uint32_t lastPositionUpdateTime;   // Time of the last position update received
};

// Chat message
struct ChatMessage
{
	std::string sender;
	std::string content;
	uint32_t timestamp;
	bool isGlobal;
	bool isSystem;
	float range;              // For local chat, how far the message travels
	std::string targetPlayer; // For private messages
};
