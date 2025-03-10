#pragma once

#include <enet/enet.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Logger.h"
#include "PacketHeader.h"
#include "PacketTypes.h"

class PacketManager
{
public:
	PacketManager() = default;

	// Send a packet to a peer
	void sendPacket(ENetPeer* peer, const GameProtocol::Packet& packet, bool reliable, std::function<void(size_t)> statsCallback = nullptr)
	{
		if (!peer)
			return;

		logger.trace("Sending packet of type: " + GameProtocol::getPacketTypeName(packet.getType()));

		// Serialize the packet
		std::vector<uint8_t> data = packet.serialize();

		// Create and send ENet packet
		ENetPacket* enetPacket = enet_packet_create(data.data(), data.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

		if (enet_peer_send(peer, 0, enetPacket) < 0)
		{
			// Send failed, clean up
			logger.warning("Failed to send packet of type: " + GameProtocol::getPacketTypeName(packet.getType()));
			enet_packet_destroy(enetPacket);
			return;
		}

		// Call stats callback if provided
		if (statsCallback)
		{
			statsCallback(data.size());
		}}

	// Receive and process a packet
	std::unique_ptr<GameProtocol::Packet> receivePacket(const ENetPacket* packet)
	{
		if (!packet || packet->dataLength < sizeof(GameProtocol::PacketHeader))
		{
			return nullptr;
		}

		// Create a span from the packet data
		std::span<const uint8_t> data(static_cast<const uint8_t*>(packet->data), packet->dataLength);

		// Deserialize the packet
		auto result = GameProtocol::deserializePacket(data);

		if (!result)
		{
			logger.warning("Failed to deserialize packet");
		}

		return result;
	}

	// Helper methods for creating specific packet types

	// Auth request
	static std::shared_ptr<GameProtocol::AuthRequestPacket> createAuthRequest(const std::string& username, const std::string& password)
	{
		return std::make_shared<GameProtocol::AuthRequestPacket>(username, password);
	}

	// Auth response
	static std::shared_ptr<GameProtocol::AuthResponsePacket> createAuthResponse(bool success, const std::string& message, uint32_t playerId = 0)
	{
		return std::make_shared<GameProtocol::AuthResponsePacket>(success, message, playerId);
	}

	// Registration
	static std::shared_ptr<GameProtocol::RegistrationPacket> createRegistration(const std::string& username, const std::string& password)
	{
		return std::make_shared<GameProtocol::RegistrationPacket>(username, password);
	}

	// Position update
	static std::shared_ptr<GameProtocol::PositionUpdatePacket> createPositionUpdate(uint32_t playerId, const Position& position)
	{
		return std::make_shared<GameProtocol::PositionUpdatePacket>(playerId, position);
	}

	// Delta position update
	static std::shared_ptr<GameProtocol::DeltaPositionUpdatePacket> createDeltaPositionUpdate(const Position& position)
	{
		return std::make_shared<GameProtocol::DeltaPositionUpdatePacket>(position);
	}

	// Teleport
	static std::shared_ptr<GameProtocol::TeleportPacket> createTeleport(const Position& position)
	{
		return std::make_shared<GameProtocol::TeleportPacket>(position);
	}

	// System message
	static std::shared_ptr<GameProtocol::SystemMessagePacket> createSystemMessage(const std::string& message)
	{
		return std::make_shared<GameProtocol::SystemMessagePacket>(message);
	}

	// Chat message
	static std::shared_ptr<GameProtocol::ChatMessagePacket> createChatMessage(const std::string& sender, const std::string& message, bool isGlobal = true)
	{
		return std::make_shared<GameProtocol::ChatMessagePacket>(sender, message, isGlobal);
	}

	// Command
	static std::shared_ptr<GameProtocol::CommandPacket> createCommand(const std::string& command, const std::vector<std::string>& args = {})
	{
		return std::make_shared<GameProtocol::CommandPacket>(command, args);
	}

	// World state
	static std::shared_ptr<GameProtocol::WorldStatePacket> createWorldState()
	{
		return std::make_shared<GameProtocol::WorldStatePacket>();
	}

	// Heartbeat
	static std::shared_ptr<GameProtocol::HeartbeatPacket> createHeartbeat(uint32_t clientTime)
	{
		return std::make_shared<GameProtocol::HeartbeatPacket>(clientTime);
	}

private:
	Logger& logger = Logger::getInstance();
};
