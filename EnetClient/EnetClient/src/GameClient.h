#pragma once

#include <atomic>
#include <corecrt.h>
#include <deque>
#include <thread>
#include <windows.h>

#include "AuthManager.h"
#include "Logger.h"
#include "NetworkManager.h"
#include "PacketTypes.h"
#include "PlayerManager.h"
#include "ChatManager.h"
#include "UIManager.h"
#include "ConnectionManager.h"

class GameClient
{
public:
	GameClient();
	~GameClient();

	void updateNetwork();
	void drawUI();

	static CurrentGameState currentGameState;
private:
	// Managers
	std::shared_ptr<NetworkManager> networkManager;
	std::shared_ptr<AuthManager> authManager;
	std::shared_ptr<PlayerManager> playerManager;
	std::shared_ptr<ChatManager> chatManager;
	std::shared_ptr<ConnectionManager> connectionManager;
	std::shared_ptr<UIManager> uiManager;
	Logger& logger = Logger::getInstance();

	// Initial login vars
	std::string myPlayerName;
	std::string myPassword;

	// Status flags
	bool reconnecting = false;

	std::thread connectionThread;
	std::atomic<bool> connectionThreadRunning{ false };

	// Utility objects
	HANDLE consoleHandle;

	// Thread management
	std::shared_ptr<ThreadManager> threadManager;
};
