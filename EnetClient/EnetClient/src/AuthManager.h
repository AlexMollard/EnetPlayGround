#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Constants.h"
#include "Logger.h"
#include "ThreadManager.h"

// Forward declarations
class NetworkManager;

/**
 * AuthManager handles authentication with the server,
 * storing/loading credentials, and managing the session state.
 */
class AuthManager
{
public:
	/**
     * Constructor
     * @param logger Reference to the logger
     * @param networkManager Shared pointer to the network manager
     */
	AuthManager(Logger& logger, std::shared_ptr<NetworkManager> networkManager, std::shared_ptr<ThreadManager> threadManager = nullptr);

	/**
     * Destructor
     */
	~AuthManager();

	/**
     * Authenticate with the server
     * @param username Username to authenticate with
     * @param password Password to authenticate with
     * @param rememberCredentials Whether to save credentials for next session
     * @param authSuccessCallback Callback for when authentication succeeds
     * @param authFailedCallback Callback for when authentication fails, with error message
     * @return True if authentication is initiated
     */
	bool authenticate(const std::string& username, const std::string& password, bool rememberCredentials, const std::function<void(uint32_t)>& authSuccessCallback, const std::function<void(const std::string&)>& authFailedCallback);

	/**
     * Process authentication response packet
     * @param packetData Authentication response packet data
     * @param packetLength Authentication response packet length
     * @param authSuccessCallback Callback for when authentication succeeds
     * @param authFailedCallback Callback for when authentication fails, with error message
     */
	void processAuthResponse(const void* packetData, size_t packetLength, const std::function<void(uint32_t)>& authSuccessCallback = nullptr, const std::function<void(const std::string&)>& authFailedCallback = nullptr);

	/**
     * Save credentials to file
     * @param username Username to save
     * @param password Password to save
     */
	void saveCredentials(const std::string& username, const std::string& password);

	/**
     * Load saved credentials
     * @param username Reference to store loaded username
     * @param password Reference to store loaded password
     * @return True if credentials were loaded
     */
	bool loadCredentials(std::string& username, std::string& password);

	/**
     * Check if player is authenticated
     * @return True if authenticated
     */
	bool isAuthenticated() const
	{
		return authenticated;
	}

	/**
     * Get the player ID
     * @return Player ID
     */
	uint32_t getPlayerId() const
	{
		return playerId;
	}

	/**
     * Set the authenticated state
     * @param auth Authentication state
     */
	void setAuthenticated(bool auth)
	{
		authenticated = auth;
	}

	/**
     * Set the player ID
     * @param id Player ID
     */
	void setPlayerId(uint32_t id)
	{
		playerId = id;
	}

private:
	// Authentication state
	bool authenticated = false;
	uint32_t playerId = 0;
	std::string username;
	std::string password;

	// Stored callbacks for authentication responses
	std::function<void(uint32_t)> authSuccessCallback;
	std::function<void(const std::string&)> authFailedCallback;

	// Dependencies
	Logger& logger;
	std::shared_ptr<NetworkManager> networkManager;

	/**
     * Helper function to split strings
     * @param str String to split
     * @param delimiter Delimiter character
     * @return Vector of split strings
     */
	std::vector<std::string> splitString(const std::string& str, char delimiter);

	std::shared_ptr<ThreadManager> threadManager;
};
