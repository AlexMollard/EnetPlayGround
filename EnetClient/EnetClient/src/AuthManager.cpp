#include "AuthManager.h"

#include <fstream>
#include <sstream>
#include <vector>

#include "NetworkManager.h"
#include "PacketTypes.h"

AuthManager::AuthManager(Logger& logger, std::shared_ptr<NetworkManager> networkManager, std::shared_ptr<ThreadManager> threadManager)
      : logger(logger), networkManager(networkManager), threadManager(threadManager)
{
	logger.debug("Initializing AuthManager");

	// Create a thread manager if none provided
	if (!threadManager)
	{
		this->threadManager = std::make_shared<ThreadManager>();
	}
}

AuthManager::~AuthManager()
{
	logger.debug("AuthManager destroyed");
}

bool AuthManager::authenticate(const std::string& username, const std::string& password, bool rememberCredentials, const std::function<void(uint32_t)>& authSuccessCallback, const std::function<void(const std::string&)>& authFailedCallback)
{
	// Store the callbacks for later use when processing the response
	this->authSuccessCallback = authSuccessCallback;
	this->authFailedCallback = authFailedCallback;

	if (!networkManager->isConnectedToServer())
	{
		logger.error("Cannot authenticate: not connected to server");
		if (authFailedCallback)
		{
			// Use thread manager instead of creating a new thread
			threadManager->scheduleTask([failedCallback = authFailedCallback]() { failedCallback("Not connected to server"); });
		}
		return false;
	}

	// Check for empty credentials
	if (username.empty() || password.empty())
	{
		logger.error("Authentication failed: empty credentials");
		if (authFailedCallback)
		{
			// Use thread manager instead of creating a new thread
			threadManager->scheduleTask([failedCallback = authFailedCallback]() { failedCallback("Username or password cannot be empty"); });
		}
		return false;
	}

	// Store credentials for later use
	this->username = username;
	this->password = password;

	logger.debug("Authenticating as user: " + username);

	// Create and send authentication packet - this should be non-blocking
	auto authPacket = networkManager->GetPacketManager()->createAuthRequest(username, password);

	// Get server peer for sending
	ENetPeer* serverPeer = networkManager->getServerPeer();
	if (!serverPeer)
	{
		logger.error("Failed to get server peer for authentication");
		if (authFailedCallback)
		{
			threadManager->scheduleTask([failedCallback = authFailedCallback]() { failedCallback("Connection to server lost"); });
		}
		return false;
	}

	// Send the authentication packet with reliability
	networkManager->GetPacketManager()->sendPacket(serverPeer, *authPacket, true);

	// Save credentials if requested
	if (rememberCredentials)
	{
		// Use a copy of credentials for thread safety
		std::string usernameCopy = username;
		std::string passwordCopy = password;

		threadManager->scheduleTask([this, usernameCopy, passwordCopy]() { this->saveCredentials(usernameCopy, passwordCopy); });
	}

	return true;
}

void AuthManager::processAuthResponse(const void* packetData, size_t packetLength, const std::function<void(uint32_t)>& authSuccessCallback, const std::function<void(const std::string&)>& authFailedCallback)
{
	// Use the stored callbacks if not provided
	auto successCallback = authSuccessCallback ? authSuccessCallback : this->authSuccessCallback;
	auto failedCallback = authFailedCallback ? authFailedCallback : this->authFailedCallback;

	// If either of the callbacks are null, we can't do anything
	if (!successCallback || !failedCallback)
	{
		logger.fatal("No callbacks provided for auth response processing, this will cause a crash!");
		return;
	}

	// Create a span from the raw packet data
	std::span<const uint8_t> data(static_cast<const uint8_t*>(packetData), packetLength);

	// Check if there's enough data for a header
	if (data.size() < sizeof(GameProtocol::PacketHeader))
	{
		logger.error("Auth response packet too small");
		if (failedCallback)
		{
			threadManager->scheduleTask([failedCallback]() { failedCallback("Received malformed authentication response"); });
		}
		return;
	}

	// Try to deserialize as an AuthResponsePacket
	auto packet = GameProtocol::deserializePacket(data);

	if (!packet || packet->getType() != GameProtocol::PacketType::AuthResponse)
	{
		logger.error("Failed to deserialize auth response packet");
		if (failedCallback)
		{
			threadManager->scheduleTask([failedCallback]() { failedCallback("Received malformed authentication response"); });
		}
		return;
	}

	// Cast to the specific packet type
	auto* authResponse = dynamic_cast<GameProtocol::AuthResponsePacket*>(packet.get());
	if (!authResponse)
	{
		logger.error("Failed to cast auth response packet");
		if (failedCallback)
		{
			threadManager->scheduleTask([failedCallback]() { failedCallback("Received malformed authentication response"); });
		}
		return;
	}

	// Process the authentication response
	if (authResponse->success)
	{
		playerId = authResponse->playerId;
		authenticated = true;

		logger.info("Authentication successful! Player ID: " + std::to_string(playerId));

		if (successCallback)
		{
			// Use thread manager
			threadManager->scheduleTask([successCallback, pid = playerId]() { successCallback(pid); });
		}
	}
	else
	{
		authenticated = false;
		std::string errorMessage = authResponse->message;
		logger.error("Authentication failed: " + errorMessage);

		if (failedCallback)
		{
			// Use thread manager
			threadManager->scheduleTask([failedCallback, errorMsg = errorMessage]() { failedCallback(errorMsg); });
		}
	}
}

void AuthManager::saveCredentials(const std::string& username, const std::string& password)
{
	logger.debug("Saving credentials...");

	std::ofstream file(CREDENTIALS_FILE, std::ios::binary);
	if (!file.is_open())
	{
		logger.error("Failed to save credentials: couldn't open file");
		return;
	}

	try
	{
		// Write username
		size_t nameLength = username.length();
		file.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
		file.write(username.c_str(), nameLength);

		// Write password
		size_t passwordLength = password.length();
		file.write(reinterpret_cast<const char*>(&passwordLength), sizeof(passwordLength));
		file.write(password.c_str(), passwordLength);

		logger.debug("Credentials saved successfully");
	}
	catch (const std::exception& e)
	{
		logger.error("Failed to save credentials: " + std::string(e.what()));
	}

	file.close();
}

bool AuthManager::loadCredentials(std::string& username, std::string& password)
{
	logger.debug("Loading saved credentials...");

	std::ifstream file(CREDENTIALS_FILE, std::ios::binary);
	if (!file.is_open())
	{
		logger.debug("No saved credentials found");
		return false;
	}

	try
	{
		// Read username
		size_t nameLength = 0;
		file.read(reinterpret_cast<char*>(&nameLength), sizeof(nameLength));

		std::vector<char> nameBuffer(nameLength + 1, 0);
		file.read(nameBuffer.data(), nameLength);
		username = std::string(nameBuffer.data(), nameLength);

		// Read password
		size_t passwordLength = 0;
		file.read(reinterpret_cast<char*>(&passwordLength), sizeof(passwordLength));

		std::vector<char> passwordBuffer(passwordLength + 1, 0);
		file.read(passwordBuffer.data(), passwordLength);
		password = std::string(passwordBuffer.data(), passwordLength);

		logger.debug("Loaded credentials for user: " + username);
		file.close();
		return true;
	}
	catch (const std::exception& e)
	{
		logger.error("Failed to load credentials: " + std::string(e.what()));
		username.clear();
		password.clear();
		file.close();
		return false;
	}
}

std::vector<std::string> AuthManager::splitString(const std::string& str, char delimiter)
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
