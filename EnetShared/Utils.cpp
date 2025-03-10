#include "Utils.h"

#include <chrono>
#include <random>
#include <sstream>

#ifdef _MSC_VER // Microsoft Visual C++ compiler
#	include <stdlib.h>
#else // Other compilers
#	include <cstdlib>
#endif

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

std::string Utils::formatTimestamp(int64_t timestamp)
{
	// get the current time
	time_t now = time(0);

	// get the difference
	int diff = now - timestamp;

	// if the difference is less than 60 seconds
	if (diff < 60)
	{
		return "Just now";
	}

	// if the difference is less than 60 minutes
	if (diff < 3600)
	{
		return std::to_string(diff / 60) + "m ago";
	}

	// if the difference is less than 24 hours
	if (diff < 86400)
	{
		return std::to_string(diff / 3600) + "h ago";
	}

	// if the difference is less than 7 days
	if (diff < 604800)
	{
		return std::to_string(diff / 86400) + "d ago";
	}

	// if the difference is less than 30 days
	if (diff < 2592000)
	{
		return std::to_string(diff / 604800) + "w ago";
	}

	// if the difference is less than 365 days
	if (diff < 31536000)
	{
		return std::to_string(diff / 2592000) + "mo ago";
	}

	return std::to_string(diff / 31536000) + "y ago";
}

std::string Utils::getEnvVar(const std::string& key)
{
#ifdef _MSC_VER // Microsoft Visual C++ compiler

	// system("set"); // for testing only, I was having trouble trying to get some environment variables

	char* buf = nullptr;
	size_t size = 0;
	std::string result;

	// _dupenv_s allocates memory that must be freed
	if (_dupenv_s(&buf, &size, key.c_str()) == 0 && buf != nullptr)
	{
		result = buf;
		free(buf); // Free the allocated memory
	}
	return result;
#else // Other compilers
	char* val = std::getenv(key.c_str());
	return val == nullptr ? std::string() : std::string(val);
#endif
}

// Join strings with a delimiter
std::string Utils::joinStrings(const std::vector<std::string>& strings, const std::string& delimiter)
{
	std::string result;
	bool first = true;

	for (const auto& str: strings)
	{
		if (!first)
		{
			result += delimiter;
		}
		result += str;
		first = false;
	}

	return result;
}
