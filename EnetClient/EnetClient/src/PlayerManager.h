#pragma once

#include <unordered_map>
#include <mutex>
#include "PacketTypes.h"

struct PlayerInfo
{
	uint32_t id = 0;
	std::string name;
	Position position;
	time_t lastSeen = 0;
	std::string status;
};

class PlayerManager
{
public:
	void updatePlayerPosition(uint32_t playerId, const Position& position);
	void updatePlayerInfo(const PlayerInfo& playerInfo);
	void removeStalePlayers();
	void handlePositionUpdate(const GameProtocol::PositionUpdatePacket& packet);
	void handleWorldState(const GameProtocol::WorldStatePacket& packet);

	PlayerInfo getPlayer(uint32_t playerId);
	PlayerInfo& getMyPlayer();
	uint32_t getMyPlayerId();
	Position getPlayerPosition(uint32_t playerId);
	Position getMyPosition();
	Position getLastSentPosition();

	void setLastSentPosition(const Position& position);
	void setMyPlayerId(uint32_t playerId);
	void setMyPosition(const Position& position);

	// This will remove all player and reset all of your player data
	void clearPlayers();

	std::mutex& getMutex();

	const std::unordered_map<uint32_t, PlayerInfo>& getPlayers() const;

private:
	std::unordered_map<uint32_t, PlayerInfo> otherPlayers;
	std::mutex playersMutex;

	uint32_t myPlayerId = 0;
	Position myPosition;
	Position lastSentPosition;
	bool useCompressedUpdates = true;
};
