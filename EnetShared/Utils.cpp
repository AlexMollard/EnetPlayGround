#include "Utils.h"

#include <chrono>
#include <random>
#include <sstream>

#include "Constants.h"

// Split string by delimiter
std::vector<std::string> Utils::splitString(const std::string& str, char delimiter)
{
	std::vector<std::string> tokens;
	std::stringstream ss(str);
	std::string token;

	while (std::getline(ss, token, delimiter))
	{
		tokens.push_back(token);
	}

	return tokens;
}

// Hash password (simple implementation - I want to upgrade this to bcrypt)
std::string Utils::hashPassword(const std::string& password, const std::string& salt)
{
	if (!SECURE_PASSWORD_STORAGE)
	{
		return password; // No hashing in development mode
	}

	// Very basic hash function - NOT for production use
	std::string salted = password + salt;
	std::size_t hash = std::hash<std::string>{}(salted);
	std::stringstream ss;
	ss << std::hex << hash;
	return ss.str();
}

// Get current timestamp in milliseconds
uint32_t Utils::getCurrentTimeMs()
{
	return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

// Get random string
std::string Utils::getRandomString(size_t length)
{
	static const char charset[] = "0123456789"
	                              "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	                              "abcdefghijklmnopqrstuvwxyz";

	std::random_device rd;
	std::mt19937 generator(rd());
	std::uniform_int_distribution<size_t> distribution(0, sizeof(charset) - 2);

	std::string result;
	result.reserve(length);

	for (size_t i = 0; i < length; ++i)
	{
		result += charset[distribution(generator)];
	}

	return result;
}

// Convert peer address to string
std::string Utils::peerAddressToString(const ENetAddress& address)
{
	char ip[64];
	enet_address_get_host_ip(&address, ip, sizeof(ip));
	return std::string(ip) + ":" + std::to_string(address.port);
}

// Format uptime from seconds
std::string Utils::formatUptime(uint32_t seconds)
{
	uint32_t days = seconds / (24 * 3600);
	seconds %= (24 * 3600);
	uint32_t hours = seconds / 3600;
	seconds %= 3600;
	uint32_t minutes = seconds / 60;
	seconds %= 60;

	std::stringstream ss;
	if (days > 0)
		ss << days << "d ";
	if (hours > 0 || days > 0)
		ss << hours << "h ";
	if (minutes > 0 || hours > 0 || days > 0)
		ss << minutes << "m ";
	ss << seconds << "s";

	return ss.str();
}

// Format bytes to human-readable string
std::string Utils::formatBytes(uint32_t bytes)
{
	static const char* suffixes[] = { "B", "KB", "MB", "GB" };
	int suffixIndex = 0;
	double value = static_cast<double>(bytes);

	while (value >= 1024 && suffixIndex < 3)
	{
		value /= 1024.0;
		suffixIndex++;
	}

	std::stringstream ss;
	ss << std::fixed << std::setprecision(2) << value << " " << suffixes[suffixIndex];
	return ss.str();
}
