#include "PlayerManager.h"

void PlayerManager::updatePlayerPosition(uint32_t playerId, const Position& position)
{
	std::lock_guard<std::mutex> lock(playersMutex);
	auto& player = otherPlayers[playerId];
	player.id = playerId;
	player.position = position;
	player.lastSeen = time(0);
	player.status = "active";
}

void PlayerManager::updatePlayerInfo(const PlayerInfo& playerInfo)
{
	std::lock_guard<std::mutex> lock(playersMutex);
	auto& player = otherPlayers[playerInfo.id];
	player = playerInfo;
	player.lastSeen = time(0);
	player.status = "active";
}

void PlayerManager::removeStalePlayers()
{
	std::lock_guard<std::mutex> lock(playersMutex);
	auto it = otherPlayers.begin();
	while (it != otherPlayers.end())
	{
		if (it->second.status == "stale")
		{
			it = otherPlayers.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void PlayerManager::handlePositionUpdate(const GameProtocol::PositionUpdatePacket& packet)
{
	// Check if this is our position update
	if (packet.playerId == myPlayerId)
	{
		// Update our position
		Position pos = packet.position;
		myPosition.x = pos.x;
		myPosition.y = pos.y;
		myPosition.z = pos.z;
	}
}

void PlayerManager::handleWorldState(const GameProtocol::WorldStatePacket& packet)
{
	// Mark all players for potential removal
	std::lock_guard<std::mutex> lock(playersMutex);
	for (auto& pair: otherPlayers)
	{
		pair.second.status = "stale";
	}

	// Update player data
	for (const auto& playerInfo: packet.players)
	{
		// Skip self
		if (playerInfo.id == myPlayerId)
			continue;

		// Update or add player
		auto it = otherPlayers.find(playerInfo.id);
		if (it != otherPlayers.end())
		{
			// Existing player, update info
			it->second.position.x = playerInfo.position.x;
			it->second.position.y = playerInfo.position.y;
			it->second.position.z = playerInfo.position.z;
			it->second.name = playerInfo.name;
			it->second.lastSeen = time(0);
			it->second.status = "active";
		}
		else
		{
			// New player
			PlayerInfo newPlayer;
			newPlayer.id = playerInfo.id;
			newPlayer.name = playerInfo.name;
			newPlayer.position.x = playerInfo.position.x;
			newPlayer.position.y = playerInfo.position.y;
			newPlayer.position.z = playerInfo.position.z;
			newPlayer.lastSeen = time(0);
			newPlayer.status = "active";

			otherPlayers[playerInfo.id] = newPlayer;
		}
	}

	// Remove stale players (not in the update)
	auto it = otherPlayers.begin();
	while (it != otherPlayers.end())
	{
		if (it->second.status == "stale")
		{
			it = otherPlayers.erase(it);
		}
		else
		{
			++it;
		}
	}
}

PlayerInfo PlayerManager::getPlayer(uint32_t playerId)
{
	return otherPlayers[playerId];
}

PlayerInfo& PlayerManager::getMyPlayer()
{
	return otherPlayers[myPlayerId];
}

uint32_t PlayerManager::getMyPlayerId()
{
	return myPlayerId;
}

Position PlayerManager::getPlayerPosition(uint32_t playerId)
{
	return otherPlayers[playerId].position;
}

Position PlayerManager::getMyPosition()
{
	return myPosition;
}

Position PlayerManager::getLastSentPosition()
{
	return lastSentPosition;
}

void PlayerManager::setLastSentPosition(const Position& position)
{
	lastSentPosition = position;
}

void PlayerManager::setMyPlayerId(uint32_t playerId)
{
	myPlayerId = playerId;
}

void PlayerManager::setMyPosition(const Position& position)
{
	myPosition = position;
}

void PlayerManager::clearPlayers()
{
	std::lock_guard<std::mutex> lock(playersMutex);
	otherPlayers.clear();

	// Reset my position
	myPosition = { 0, 0, 0 };
	myPlayerId = 0;
}

std::mutex& PlayerManager::getMutex()
{
	return playersMutex;
}

const std::unordered_map<uint32_t, PlayerInfo>& PlayerManager::getPlayers() const
{
	return otherPlayers;
}
