#include "AuthManager.h"

#include <fstream>
#include <sstream>
#include <vector>

#include "NetworkManager.h"

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

	// Send authentication message - this should be non-blocking
	std::string authMsg = "AUTH:" + username + "," + password;
	networkManager->sendPacket(authMsg, true);

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

	std::string message(reinterpret_cast<const char*>(packetData), packetLength);

	// Check if this is an authentication response
	size_t commaPos = message.find(',', 13);
	if (commaPos != std::string::npos)
	{
		std::string result = message.substr(14, commaPos - 14);

		if (result == "success")
		{
			try
			{
				playerId = std::stoi(message.substr(commaPos + 1));
				authenticated = true;

				logger.info("Authentication successful! Player ID: " + std::to_string(playerId));

				if (successCallback)
				{
					// Use thread manager
					threadManager->scheduleTask([successCallback, pid = playerId]() { successCallback(pid); });
				}
			}
			catch (const std::exception& e)
			{
				logger.error("Failed to parse player ID: " + std::string(e.what()));
				if (failedCallback)
				{
					// Use thread manager
					threadManager->scheduleTask([failedCallback, errorMsg = std::string("Server sent invalid player ID")]() { failedCallback(errorMsg); });
				}
			}
		}
		else
		{
			authenticated = false;
			std::string errorMessage = message.substr(commaPos + 1);
			logger.error("Authentication failed: " + errorMessage);

			if (failedCallback)
			{
				// Use thread manager
				threadManager->scheduleTask([failedCallback, errorMsg = errorMessage]() { failedCallback(errorMsg); });
			}
		}
	}
	else
	{
		// Malformed auth response
		logger.error("Received malformed authentication response");
		if (failedCallback)
		{
			// Use thread manager
			threadManager->scheduleTask([failedCallback]() { failedCallback("Received malformed authentication response"); });
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
