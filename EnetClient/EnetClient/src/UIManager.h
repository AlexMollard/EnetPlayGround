#pragma once

#include "ChatManager.h"
#include "ConnectionManager.h"
#include "NetworkManager.h"
#include "PlayerManager.h"
#include "ThemeManager.h"

class UIManager
{
public:
	UIManager() = default;
	void drawUI();
	void drawLoginScreen();
	void drawRegisterScreen();
	void drawConnectedUI();
	void drawHeader();
	void drawPlayersPanel(float width, float height);
	void drawChatPanel(float width, float height);
	void drawControlsPanel(float width, float height);
	void drawNetworkOptionsUI(float width, float height);
	void drawStatusBar();

	void initiateConnection();
	void initiateRegistration();

	// Set managers
	void setPlayerManager(std::shared_ptr<PlayerManager> playerManager);
	void setChatManager(std::shared_ptr<ChatManager> chatManager);
	void setNetworkManager(std::shared_ptr<NetworkManager> networkManager);
	void setConnectionManager(std::shared_ptr<ConnectionManager> connectionManager);
	void setThreadManager(std::shared_ptr<ThreadManager> threadManager);

	const char* getLoginUsernameBuffer();
	void setLoginUsernameBuffer(const char* username);

	const char* getLoginPasswordBuffer();
	void setLoginPasswordBuffer(const char* password);

	std::string getErrorMessage();
	void setErrorMessage(std::string errorMsg);

private:
	std::shared_ptr<PlayerManager> playerManager;
	std::shared_ptr<ChatManager> chatManager;
	std::shared_ptr<NetworkManager> networkManager;
	std::shared_ptr<ConnectionManager> connectionManager;
	std::shared_ptr<ThreadManager> threadManager;
	
	ThemeManager themeManager;

	Logger& logger = Logger::getInstance();

	// Login UI components
	std::string loginErrorMessage;
	bool rememberCredentials = true;
	char loginUsernameBuffer[64] = { 0 };
	char loginPasswordBuffer[64] = { 0 };

	// Register UI components
	bool isRegistering = false;
	char registerUsernameBuffer[64] = { 0 };
	char registerPasswordBuffer[64] = { 0 };
	char registerConfirmPasswordBuffer[64] = { 0 };
	std::string registerErrorMessage;

	// Chat input
	char chatInputBuffer[256] = { 0 };

	bool showPassword = false;
};
