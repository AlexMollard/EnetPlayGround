#include "ChatManager.h"
#include "Constants.h"

#include "AuthManager.h"
#include "NetworkManager.h"
#include "ConnectionManager.h"
#include "PlayerManager.h"

void ChatManager::addChatMessage(const std::string& sender, const std::string& content)
{
	std::lock_guard<std::mutex> guard(chatMutex);

	ChatMessage msg;
	msg.sender = sender;
	msg.content = content;
	msg.timestamp = time(0);

	chatMessages.push_back(msg);

	// Keep only the last N messages
	while (chatMessages.size() > MESSAGE_HISTORY_SIZE)
	{
		chatMessages.pop_front();
	}
}

void ChatManager::clearChatMessages()
{
	std::lock_guard<std::mutex> guard(chatMutex);
	chatMessages.clear();
}

const std::deque<ChatMessage>& ChatManager::getChatMessages() const
{
	return chatMessages;
}

// Send a chat message using the new packet system
void ChatManager::sendChatMessage(const std::string& message)
{
	if (!authManager->isAuthenticated() || message.empty())
		return;

	// Create a chat message packet
	auto chatPacket = networkManager->GetPacketManager()->createChatMessage(playerManager->getMyPlayer().name, message);

	// Send the packet
	networkManager->GetPacketManager()->sendPacket(networkManager->getServer(), *chatPacket, true);

	// Also add to local chat
	addChatMessage("You", message);
}


void ChatManager::processChatCommand(const std::string& command)
{
	if (command.empty())
	{
		return;
	}

	// Keep a few client-side commands for UI and local functionality
	if (command[0] == '/')
	{
		// Extract command and arguments
		std::string cmd = command.substr(1);
		size_t spacePos = cmd.find(' ');
		std::vector<std::string> args;
		std::string baseCmd;

		if (spacePos != std::string::npos)
		{
			baseCmd = cmd.substr(0, spacePos);
			std::string argsStr = cmd.substr(spacePos + 1);

			// Parse arguments
			size_t start = 0;
			size_t end = 0;
			while ((end = argsStr.find(' ', start)) != std::string::npos)
			{
				args.push_back(argsStr.substr(start, end - start));
				start = end + 1;
			}
			args.push_back(argsStr.substr(start));
		}
		else
		{
			baseCmd = cmd;
		}

		// Convert command to lowercase
		std::transform(baseCmd.begin(), baseCmd.end(), baseCmd.begin(), [](unsigned char c) { return std::tolower(c); });

		// Add the command to the chat
		addChatMessage("You - command", command);
		
		// Send all other commands to the server
		if (networkManager->isConnectedToServer() && authManager->isAuthenticated())
		{
			// Create command packet
			auto commandPacket = networkManager->GetPacketManager()->createCommand(baseCmd, args);

			// Send the packet
			networkManager->GetPacketManager()->sendPacket(networkManager->getServer(), *commandPacket, true);

			logger.debug("Sent command to server: " + baseCmd + " with " + std::to_string(args.size()) + " arguments");
		}
		else
		{
			logger.warning("Not connected to server or not authenticated, cannot send command: " + command);
		}
	}
	else
	{
		// Not a command, treat as regular chat
		sendChatMessage(command);
	}
}

void ChatManager::setAuthManager(std::shared_ptr<AuthManager> authManager)
{
	this->authManager = authManager;
}

void ChatManager::setPlayerManager(std::shared_ptr<PlayerManager> playerManager)
{
	this->playerManager = playerManager;
}

void ChatManager::setNetworkManager(std::shared_ptr<NetworkManager> networkManager)
{
	this->networkManager = networkManager;
}

void ChatManager::setConnectionManager(std::shared_ptr<ConnectionManager> connectionManager)
{
	this->connectionManager = connectionManager;
}

std::mutex& ChatManager::getMutex()
{
	return chatMutex;
}
