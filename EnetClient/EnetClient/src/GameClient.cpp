#include "GameClient.h"

CurrentGameState GameClient::currentGameState = CurrentGameState::LoginScreen;

// Constructor
GameClient::GameClient()
{
	// Set up console
	consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (consoleHandle == INVALID_HANDLE_VALUE)
	{
		logger.error("Failed to get console handle");
	}

	// Create the thread manager first
	size_t numThreads = max(1u, 4);
	threadManager = std::make_shared<ThreadManager>(numThreads);

	// Create managers without circular dependencies first
	networkManager = std::make_shared<NetworkManager>(threadManager);
	authManager = std::make_shared<AuthManager>(networkManager, threadManager);
	playerManager = std::make_shared<PlayerManager>();

	// Create remaining managers with default constructors
	chatManager = std::make_shared<ChatManager>();
	connectionManager = std::make_shared<ConnectionManager>();
	uiManager = std::make_shared<UIManager>();

	// Set up dependencies for ChatManager
	chatManager->setAuthManager(authManager);
	chatManager->setPlayerManager(playerManager);
	chatManager->setNetworkManager(networkManager);

	// Set up dependencies for ConnectionManager
	connectionManager->setNetworkManager(networkManager);
	connectionManager->setAuthManager(authManager);
	connectionManager->setThreadManager(threadManager);
	connectionManager->setPlayerManager(playerManager);

	// Set up dependencies for UIManager
	uiManager->setPlayerManager(playerManager);
	uiManager->setChatManager(chatManager);
	uiManager->setNetworkManager(networkManager);
	uiManager->setThreadManager(threadManager);

	// Set up circular references
	networkManager->setAuthManager(authManager);
	chatManager->setConnectionManager(connectionManager);
	connectionManager->setChatManager(chatManager);
	connectionManager->setUIManager(uiManager);
	uiManager->setConnectionManager(connectionManager);

	// Try to load stored credentials if none provided
	if (myPlayerName.empty() || myPassword.empty())
	{
		std::string loadedUsername, loadedPassword;
		// Load credentials on thread pool and wait for result
		auto future = threadManager->scheduleTaskWithResult([this, &loadedUsername, &loadedPassword]() { return authManager->loadCredentials(loadedUsername, loadedPassword); });

		if (future.get()) // Wait for the result
		{
			myPlayerName = loadedUsername;
			myPassword = loadedPassword;
			// Copy loaded credentials to the ImGui input buffers
			if (!myPlayerName.empty())
			{
				// Copy credentials to login screen for convenience
				uiManager->setLoginPasswordBuffer(myPlayerName.c_str());
			}
			if (!myPassword.empty())
			{
				uiManager->setLoginPasswordBuffer(myPassword.c_str());
			}
		}
	}

	// Start in login screen state
	currentGameState = CurrentGameState::LoginScreen;
}

// Destructor
GameClient::~GameClient()
{
	connectionManager->disconnect(true);
	currentGameState = CurrentGameState::LoginScreen;

	// First clean up NetworkManager and AuthManager to stop any ongoing operations
	networkManager.reset();
	authManager.reset();

	// Wait for all tasks to complete before shutting down
	if (threadManager)
	{
		threadManager->waitForTasks();
		threadManager.reset();
	}

	logger.debug("Client shutdown complete");
}

// Process a single network update cycle
void GameClient::updateNetwork()
{
	connectionManager->updateNetwork();
}

// Draw the UI with ImGui
void GameClient::drawUI()
{
	uiManager->drawUI();
}
