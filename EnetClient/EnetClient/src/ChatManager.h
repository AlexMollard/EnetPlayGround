#pragma once

#include <deque>
#include <mutex>
#include <string>
#include "PacketTypes.h"
#include "Logger.h"

class AuthManager;
class PlayerManager;
class NetworkManager;
class ConnectionManager;

class ChatManager
{
public:
	ChatManager() = default;
	void addChatMessage(const std::string& sender, const std::string& content);
	void clearChatMessages();
	const std::deque<ChatMessage>& getChatMessages() const;
	void sendChatMessage(const std::string& message);
	void processChatCommand(const std::string& command);

	// Set managers
	void setAuthManager(std::shared_ptr<AuthManager> authManager);
	void setPlayerManager(std::shared_ptr<PlayerManager> playerManager);
	void setNetworkManager(std::shared_ptr<NetworkManager> networkManager);
	void setConnectionManager(std::shared_ptr<ConnectionManager> connectionManager);

	std::mutex& getMutex();

private:
	std::shared_ptr<AuthManager> authManager;
	std::shared_ptr<PlayerManager> playerManager;
	std::shared_ptr<NetworkManager> networkManager;
	std::shared_ptr<ConnectionManager> connectionManager;

	std::deque<ChatMessage> chatMessages;
	std::mutex chatMutex;

	Logger& logger = Logger::getInstance();
};
