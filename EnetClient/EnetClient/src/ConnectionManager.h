#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "Logger.h"
#include "PacketTypes.h"
#include "ThemeManager.h"

class NetworkManager;
class AuthManager;
class ThreadManager;
class UIManager;
class PlayerManager;
class ChatManager;

enum class CurrentGameState
{
	Disconnected,
	LoginScreen,
	RegisterScreen,
	Connecting,
	Authenticating,
	Connected
};

class ConnectionManager
{
public:
	ConnectionManager() = default;
	void startConnection(const std::string& username, const std::string& password, bool rememberCredentials);
	void disconnect(bool tellServer = true);
	void handlePacket(const ENetPacket* enetPacket);
	void updateNetwork();
	void signOut();

	const float getConnectionProgress() const;

	void setChatManager(std::shared_ptr<ChatManager> chatManager);
	void setUIManager(std::shared_ptr<UIManager> uiManager);
	void setPlayerManager(std::shared_ptr<PlayerManager> playerManager);
	void setAuthManager(std::shared_ptr<AuthManager> authManager);
	void setNetworkManager(std::shared_ptr<NetworkManager> networkManager);
	void setThreadManager(std::shared_ptr<ThreadManager> threadManager);

private:
	void handleAuthResponse(const GameProtocol::AuthResponsePacket& packet);
	void handleChatMessage(const GameProtocol::ChatMessagePacket& packet);
	void handleSystemMessage(const GameProtocol::SystemMessagePacket& packet);
	void handleTeleport(const GameProtocol::TeleportPacket& packet);

	std::shared_ptr<NetworkManager> networkManager;
	std::shared_ptr<AuthManager> authManager;
	std::shared_ptr<ThreadManager> threadManager;
	std::shared_ptr<UIManager> uiManager;
	std::shared_ptr<ChatManager> chatManager;
	std::shared_ptr<PlayerManager> playerManager;

	std::atomic<bool> connectionThreadRunning{ false };
	std::thread connectionThread;

	float lastPositionUpdateTime = 0.0f;
	float positionUpdateRateMs = 0.0f;
	float connectionProgress = 0.0f;
	float movementThreshold = 20.0f;
	float lastConnectionCheckTime = 0.0f;

	Logger& logger = Logger::getInstance();
};
