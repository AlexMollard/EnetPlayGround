#pragma once

#include <span>
#include <string>
#include <vector>

#include "PacketHeader.h"

namespace GameProtocol
{

	// Authentication request packet
	class AuthRequestPacket : public Packet
	{
	public:
		std::string username;
		std::string password;

		AuthRequestPacket() = default;

		AuthRequestPacket(std::string username, std::string password)
		      : username(std::move(username)), password(std::move(password))
		{
		}

		PacketType getType() const override
		{
			return PacketType::AuthRequest;
		}

		std::vector<uint8_t> serialize() const override
		{
			std::vector<uint8_t> buffer;

			// Reserve space for header
			buffer.resize(sizeof(PacketHeader));

			// Write payload
			writeStringToBuffer(buffer, username);
			writeStringToBuffer(buffer, password);

			// Fill header
			PacketHeader header(getType(), buffer.size() - sizeof(PacketHeader));
			std::memcpy(buffer.data(), &header, sizeof(header));

			return buffer;
		}

		static AuthRequestPacket deserialize(std::span<const uint8_t> data)
		{
			// Skip header
			data = data.subspan(sizeof(PacketHeader));

			// Read payload
			AuthRequestPacket packet;
			packet.username = readStringFromBuffer(data);
			packet.password = readStringFromBuffer(data);

			return packet;
		}
	};

	// Authentication response packet
	class AuthResponsePacket : public Packet
	{
	public:
		bool success;
		std::string message;
		uint32_t playerId;

		AuthResponsePacket() = default;

		AuthResponsePacket(bool success, std::string message, uint32_t playerId = 0)
		      : success(success), message(std::move(message)), playerId(playerId)
		{
		}

		PacketType getType() const override
		{
			return PacketType::AuthResponse;
		}

		std::vector<uint8_t> serialize() const override
		{
			std::vector<uint8_t> buffer;

			// Reserve space for header
			buffer.resize(sizeof(PacketHeader));

			// Write payload
			writeToBuffer(buffer, success);
			writeStringToBuffer(buffer, message);
			writeToBuffer(buffer, playerId);

			// Fill header
			PacketHeader header(getType(), buffer.size() - sizeof(PacketHeader));
			std::memcpy(buffer.data(), &header, sizeof(header));

			return buffer;
		}

		static AuthResponsePacket deserialize(std::span<const uint8_t> data)
		{
			// Skip header
			data = data.subspan(sizeof(PacketHeader));

			// Read payload
			AuthResponsePacket packet;
			packet.success = readFromBuffer<bool>(data);
			packet.message = readStringFromBuffer(data);
			packet.playerId = readFromBuffer<uint32_t>(data);

			return packet;
		}
	};

	// Registration packet
	class RegistrationPacket : public Packet
	{
	public:
		std::string username;
		std::string password;

		RegistrationPacket() = default;

		RegistrationPacket(std::string username, std::string password)
		      : username(std::move(username)), password(std::move(password))
		{
		}

		PacketType getType() const override
		{
			return PacketType::Registration;
		}

		std::vector<uint8_t> serialize() const override
		{
			std::vector<uint8_t> buffer;

			// Reserve space for header
			buffer.resize(sizeof(PacketHeader));

			// Write payload
			writeStringToBuffer(buffer, username);
			writeStringToBuffer(buffer, password);

			// Fill header
			PacketHeader header(getType(), buffer.size() - sizeof(PacketHeader));
			std::memcpy(buffer.data(), &header, sizeof(header));

			return buffer;
		}

		static RegistrationPacket deserialize(std::span<const uint8_t> data)
		{
			// Skip header
			data = data.subspan(sizeof(PacketHeader));

			// Read payload
			RegistrationPacket packet;
			packet.username = readStringFromBuffer(data);
			packet.password = readStringFromBuffer(data);

			return packet;
		}
	};

	// Position update packet
	class PositionUpdatePacket : public Packet
	{
	public:
		uint32_t playerId;
		SerializablePosition position;

		PositionUpdatePacket() = default;

		PositionUpdatePacket(uint32_t playerId, const Position& position)
		      : playerId(playerId), position(position)
		{
		}

		PacketType getType() const override
		{
			return PacketType::PositionUpdate;
		}

		std::vector<uint8_t> serialize() const override
		{
			std::vector<uint8_t> buffer;

			// Reserve space for header
			buffer.resize(sizeof(PacketHeader));

			// Write payload
			writeToBuffer(buffer, playerId);
			writeToBuffer(buffer, position);

			// Fill header
			PacketHeader header(getType(), buffer.size() - sizeof(PacketHeader));
			std::memcpy(buffer.data(), &header, sizeof(header));

			return buffer;
		}

		static PositionUpdatePacket deserialize(std::span<const uint8_t> data)
		{
			// Skip header
			data = data.subspan(sizeof(PacketHeader));

			// Read payload
			PositionUpdatePacket packet;
			packet.playerId = readFromBuffer<uint32_t>(data);
			packet.position = readFromBuffer<SerializablePosition>(data);

			return packet;
		}
	};

	// Delta position update packet
	class DeltaPositionUpdatePacket : public Packet
	{
	public:
		SerializablePosition position;

		DeltaPositionUpdatePacket() = default;

		DeltaPositionUpdatePacket(const Position& position)
		      : position(position)
		{
		}

		PacketType getType() const override
		{
			return PacketType::DeltaPositionUpdate;
		}

		std::vector<uint8_t> serialize() const override
		{
			std::vector<uint8_t> buffer;

			// Reserve space for header
			buffer.resize(sizeof(PacketHeader));

			// Write payload
			writeToBuffer(buffer, position);

			// Fill header
			PacketHeader header(getType(), buffer.size() - sizeof(PacketHeader));
			std::memcpy(buffer.data(), &header, sizeof(header));

			return buffer;
		}

		static DeltaPositionUpdatePacket deserialize(std::span<const uint8_t> data)
		{
			// Skip header
			data = data.subspan(sizeof(PacketHeader));

			// Read payload
			DeltaPositionUpdatePacket packet;
			packet.position = readFromBuffer<SerializablePosition>(data);

			return packet;
		}
	};

	// Teleport packet
	class TeleportPacket : public Packet
	{
	public:
		SerializablePosition position;

		TeleportPacket() = default;

		TeleportPacket(const Position& position)
		      : position(position)
		{
		}

		PacketType getType() const override
		{
			return PacketType::Teleport;
		}

		std::vector<uint8_t> serialize() const override
		{
			std::vector<uint8_t> buffer;

			// Reserve space for header
			buffer.resize(sizeof(PacketHeader));

			// Write payload
			writeToBuffer(buffer, position);

			// Fill header
			PacketHeader header(getType(), buffer.size() - sizeof(PacketHeader));
			std::memcpy(buffer.data(), &header, sizeof(header));

			return buffer;
		}

		static TeleportPacket deserialize(std::span<const uint8_t> data)
		{
			// Skip header
			data = data.subspan(sizeof(PacketHeader));

			// Read payload
			TeleportPacket packet;
			packet.position = readFromBuffer<SerializablePosition>(data);

			return packet;
		}
	};

	// Chat message packet
	class ChatMessagePacket : public Packet
	{
	public:
		std::string sender;
		std::string message;
		bool isGlobal;

		ChatMessagePacket() = default;

		ChatMessagePacket(std::string sender, std::string message, bool isGlobal = true)
		      : sender(std::move(sender)), message(std::move(message)), isGlobal(isGlobal)
		{
		}

		PacketType getType() const override
		{
			return PacketType::ChatMessage;
		}

		std::vector<uint8_t> serialize() const override
		{
			std::vector<uint8_t> buffer;

			// Reserve space for header
			buffer.resize(sizeof(PacketHeader));

			// Write payload
			writeStringToBuffer(buffer, sender);
			writeStringToBuffer(buffer, message);
			writeToBuffer(buffer, isGlobal);

			// Fill header
			PacketHeader header(getType(), buffer.size() - sizeof(PacketHeader));
			std::memcpy(buffer.data(), &header, sizeof(header));

			return buffer;
		}

		static ChatMessagePacket deserialize(std::span<const uint8_t> data)
		{
			// Skip header
			data = data.subspan(sizeof(PacketHeader));

			// Read payload
			ChatMessagePacket packet;
			packet.sender = readStringFromBuffer(data);
			packet.message = readStringFromBuffer(data);
			packet.isGlobal = readFromBuffer<bool>(data);

			return packet;
		}
	};

	// System message packet
	class SystemMessagePacket : public Packet
	{
	public:
		std::string message;

		SystemMessagePacket() = default;

		SystemMessagePacket(std::string message)
		      : message(std::move(message))
		{
		}

		PacketType getType() const override
		{
			return PacketType::SystemMessage;
		}

		std::vector<uint8_t> serialize() const override
		{
			std::vector<uint8_t> buffer;

			// Reserve space for header
			buffer.resize(sizeof(PacketHeader));

			// Write payload
			writeStringToBuffer(buffer, message);

			// Fill header
			PacketHeader header(getType(), buffer.size() - sizeof(PacketHeader));
			std::memcpy(buffer.data(), &header, sizeof(header));

			return buffer;
		}

		static SystemMessagePacket deserialize(std::span<const uint8_t> data)
		{
			// Skip header
			data = data.subspan(sizeof(PacketHeader));

			// Read payload
			SystemMessagePacket packet;
			packet.message = readStringFromBuffer(data);

			return packet;
		}
	};

	// Command packet
	class CommandPacket : public Packet
	{
	public:
		std::string command;
		std::vector<std::string> arguments;

		CommandPacket() = default;

		CommandPacket(std::string command, std::vector<std::string> args = {})
		      : command(std::move(command)), arguments(std::move(args))
		{
		}

		PacketType getType() const override
		{
			return PacketType::Command;
		}

		std::vector<uint8_t> serialize() const override
		{
			std::vector<uint8_t> buffer;

			// Reserve space for header
			buffer.resize(sizeof(PacketHeader));

			// Write payload
			writeStringToBuffer(buffer, command);

			// Write number of arguments
			uint16_t argCount = static_cast<uint16_t>(arguments.size());
			writeToBuffer(buffer, argCount);

			// Write each argument
			for (const auto& arg: arguments)
			{
				writeStringToBuffer(buffer, arg);
			}

			// Fill header
			PacketHeader header(getType(), buffer.size() - sizeof(PacketHeader));
			std::memcpy(buffer.data(), &header, sizeof(header));

			return buffer;
		}

		static CommandPacket deserialize(std::span<const uint8_t> data)
		{
			// Skip header
			data = data.subspan(sizeof(PacketHeader));

			// Read payload
			CommandPacket packet;
			packet.command = readStringFromBuffer(data);

			// Read argument count
			uint16_t argCount = readFromBuffer<uint16_t>(data);

			// Read each argument
			packet.arguments.reserve(argCount);
			for (uint16_t i = 0; i < argCount; ++i)
			{
				packet.arguments.push_back(readStringFromBuffer(data));
			}

			return packet;
		}
	};

	// World state packet
	class WorldStatePacket : public Packet
	{
	public:
		struct PlayerInfo
		{
			uint32_t id;
			std::string name;
			SerializablePosition position;
		};

		std::vector<PlayerInfo> players;

		WorldStatePacket() = default;

		PacketType getType() const override
		{
			return PacketType::WorldState;
		}

		std::vector<uint8_t> serialize() const override
		{
			std::vector<uint8_t> buffer;

			// Reserve space for header
			buffer.resize(sizeof(PacketHeader));

			// Write number of players
			uint16_t playerCount = static_cast<uint16_t>(players.size());
			writeToBuffer(buffer, playerCount);

			// Write each player's info
			for (const auto& player: players)
			{
				writeToBuffer(buffer, player.id);
				writeStringToBuffer(buffer, player.name);
				writeToBuffer(buffer, player.position);
			}

			// Fill header
			PacketHeader header(getType(), buffer.size() - sizeof(PacketHeader));
			std::memcpy(buffer.data(), &header, sizeof(header));

			return buffer;
		}

		static WorldStatePacket deserialize(std::span<const uint8_t> data)
		{
			// Skip header
			data = data.subspan(sizeof(PacketHeader));

			// Read payload
			WorldStatePacket packet;

			// Read player count
			uint16_t playerCount = readFromBuffer<uint16_t>(data);

			// Read each player's info
			packet.players.reserve(playerCount);
			for (uint16_t i = 0; i < playerCount; ++i)
			{
				PlayerInfo info;
				info.id = readFromBuffer<uint32_t>(data);
				info.name = readStringFromBuffer(data);
				info.position = readFromBuffer<SerializablePosition>(data);
				packet.players.push_back(info);
			}

			return packet;
		}
	};

	class HeartbeatPacket : public GameProtocol::Packet
	{
	public:
		uint32_t clientTime;

		HeartbeatPacket() = default;

		HeartbeatPacket(uint32_t clientTime)
		      : clientTime(clientTime)
		{
		}

		GameProtocol::PacketType getType() const override
		{
			return GameProtocol::PacketType::Heartbeat;
		}

		std::vector<uint8_t> serialize() const override
		{
			std::vector<uint8_t> buffer;

			// Reserve space for header
			buffer.resize(sizeof(GameProtocol::PacketHeader));

			// Write payload
			GameProtocol::writeToBuffer(buffer, clientTime);

			// Fill header
			GameProtocol::PacketHeader header(getType(), buffer.size() - sizeof(GameProtocol::PacketHeader));
			std::memcpy(buffer.data(), &header, sizeof(header));

			return buffer;
		}

		static HeartbeatPacket deserialize(std::span<const uint8_t> data)
		{
			// Skip header
			data = data.subspan(sizeof(GameProtocol::PacketHeader));

			// Read payload
			HeartbeatPacket packet;
			packet.clientTime = GameProtocol::readFromBuffer<uint32_t>(data);

			return packet;
		}
	};

	// Function to deserialize a packet based on its type
	inline std::unique_ptr<Packet> deserializePacket(std::span<const uint8_t> data)
	{
		if (data.size() < sizeof(PacketHeader))
		{
			return nullptr;
		}

		const PacketHeader* header = reinterpret_cast<const PacketHeader*>(data.data());
		if (!header->isValid())
		{
			return nullptr;
		}

		switch (header->type)
		{
			case PacketType::AuthRequest:
				return std::make_unique<AuthRequestPacket>(AuthRequestPacket::deserialize(data));

			case PacketType::AuthResponse:
				return std::make_unique<AuthResponsePacket>(AuthResponsePacket::deserialize(data));

			case PacketType::Registration:
				return std::make_unique<RegistrationPacket>(RegistrationPacket::deserialize(data));

			case PacketType::PositionUpdate:
				return std::make_unique<PositionUpdatePacket>(PositionUpdatePacket::deserialize(data));

			case PacketType::DeltaPositionUpdate:
				return std::make_unique<DeltaPositionUpdatePacket>(DeltaPositionUpdatePacket::deserialize(data));

			case PacketType::Teleport:
				return std::make_unique<TeleportPacket>(TeleportPacket::deserialize(data));

			case PacketType::ChatMessage:
				return std::make_unique<ChatMessagePacket>(ChatMessagePacket::deserialize(data));

			case PacketType::SystemMessage:
				return std::make_unique<SystemMessagePacket>(SystemMessagePacket::deserialize(data));

			case PacketType::Command:
				return std::make_unique<CommandPacket>(CommandPacket::deserialize(data));

			case PacketType::WorldState:
				return std::make_unique<WorldStatePacket>(WorldStatePacket::deserialize(data));

			case PacketType::Heartbeat:
				return std::make_unique<HeartbeatPacket>(HeartbeatPacket::deserialize(data));

			default:
				return nullptr;
		}
	}

	static std::string getPacketTypeName(PacketType type)
	{
		switch (type)
		{
			case PacketType::Heartbeat:
				return "Heartbeat";
			case PacketType::Disconnect:
				return "Disconnect";
			case PacketType::AuthRequest:
				return "AuthRequest";
			case PacketType::AuthResponse:
				return "AuthResponse";
			case PacketType::Registration:
				return "Registration";
			case PacketType::PositionUpdate:
				return "PositionUpdate";
			case PacketType::DeltaPositionUpdate:
				return "DeltaPositionUpdate";
			case PacketType::Teleport:
				return "Teleport";
			case PacketType::ChatMessage:
				return "ChatMessage";
			case PacketType::SystemMessage:
				return "SystemMessage";
			case PacketType::Whisper:
				return "Whisper";
			case PacketType::Command:
				return "Command";
			case PacketType::WorldState:
				return "WorldState";
			default:
				return "Unknown";
		}
	}
} // namespace GameProtocol
