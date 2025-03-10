#pragma once

#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Structs.h"

namespace GameProtocol
{

	// Magic number to identify our packets (GSRV - Game Server)
	inline constexpr uint32_t PACKET_MAGIC = 0x47535256;

	// Current protocol version
	inline constexpr uint16_t PACKET_PROTOCOL_VERSION = 1;

	// Packet types
	enum class PacketType : uint8_t
	{
		// System packets
		Heartbeat = 0x00,
		Disconnect = 0x01,

		// Authentication packets
		AuthRequest = 0x10,
		AuthResponse = 0x11,
		Registration = 0x12,

		// Position & movement
		PositionUpdate = 0x20,
		DeltaPositionUpdate = 0x21,
		Teleport = 0x22,

		// Chat & communication
		ChatMessage = 0x30,
		SystemMessage = 0x31,
		Whisper = 0x32,

		// Player commands
		Command = 0x40,

		// World state
		WorldState = 0x50,

		// Maximum value (for validation)
		MaxValue = 0xFF
	};

// Standard packet header
#pragma pack(push, 1)

	struct PacketHeader
	{
		uint32_t magic;    // Magic number to identify our packets
		uint16_t version;  // Protocol version
		PacketType type;   // Packet type
		size_t length;   // Length of the payload in bytes
		uint32_t sequence; // Sequence number (for ordering/reliability)

		// Constructor with default values
		PacketHeader(PacketType type = PacketType::Heartbeat, size_t length = 0, uint32_t sequence = 0)
		      : magic(PACKET_MAGIC), version(PACKET_PROTOCOL_VERSION), type(type), length(length), sequence(sequence)
		{
		}

		// Validate header
		bool isValid() const
		{
			return magic == PACKET_MAGIC && version <= PACKET_PROTOCOL_VERSION && static_cast<uint8_t>(type) < static_cast<uint8_t>(PacketType::MaxValue);
		}
	};

#pragma pack(pop)

	// Ensure our packet header is the expected size
	static_assert(sizeof(PacketHeader) == 19, "PacketHeader size mismatch");

	// Base class for all packets
	class Packet
	{
	public:
		virtual ~Packet() = default;

		// Serialize packet to binary
		virtual std::vector<uint8_t> serialize() const = 0;

		// Get packet type
		virtual PacketType getType() const = 0;
	};

	// Helper functions for serialization/deserialization

	// Write a value to a buffer
	template<typename T>
	    requires std::is_trivially_copyable_v<T>
	void writeToBuffer(std::vector<uint8_t>& buffer, const T& value)
	{
		const size_t size = sizeof(T);
		const size_t offset = buffer.size();
		buffer.resize(offset + size);
		std::memcpy(buffer.data() + offset, &value, size);
	}

	// Write a string to a buffer (with length prefix)
	inline void writeStringToBuffer(std::vector<uint8_t>& buffer, std::string_view str)
	{
		uint16_t length = static_cast<uint16_t>(str.length());
		writeToBuffer(buffer, length);
		const size_t offset = buffer.size();
		buffer.resize(offset + length);
		std::memcpy(buffer.data() + offset, str.data(), length);
	}

	// Read a value from a buffer
	template<typename T>
	    requires std::is_trivially_copyable_v<T>
	T readFromBuffer(std::span<const uint8_t>& data)
	{
		T value;
		std::memcpy(&value, data.data(), sizeof(T));
		data = data.subspan(sizeof(T));
		return value;
	}

	// Read a string from a buffer (with length prefix)
	inline std::string readStringFromBuffer(std::span<const uint8_t>& data)
	{
		uint16_t length = readFromBuffer<uint16_t>(data);
		std::string str(reinterpret_cast<const char*>(data.data()), length);
		data = data.subspan(length);
		return str;
	}

	// Position struct for serialization
	struct SerializablePosition
	{
		float x;
		float y;
		float z;

		// Convert from game Position struct
		SerializablePosition(const Position& pos)
		      : x(pos.x), y(pos.y), z(pos.z)
		{
		}

		SerializablePosition()
		      : x(0.0f), y(0.0f), z(0.0f)
		{
		}

		// Convert to game Position struct
		operator Position() const
		{
			return Position{ x, y, z };
		}
	};

	static_assert(std::is_trivially_copyable_v<SerializablePosition>, "SerializablePosition must be trivially copyable");

} // namespace GameProtocol
