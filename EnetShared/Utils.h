#pragma once

#include <vector>
#include <string>
#include <enet/enet.h>

// Helper utilities
class Utils
{
public:
	// Split string by delimiter
	static std::vector<std::string> splitString(const std::string& str, char delimiter);

	// Hash password
	static std::string hashPassword(const std::string& password, const std::string& salt = "");

	// Get current timestamp in milliseconds
	static uint32_t getCurrentTimeMs();

	// Get random string
	static std::string getRandomString(size_t length);

	// Convert peer address to string
	static std::string peerAddressToString(const ENetAddress& address);

	// Format uptime from seconds
	static std::string formatUptime(uint32_t seconds);

	// Format bytes to human-readable string
	static std::string formatBytes(uint32_t bytes);
};
