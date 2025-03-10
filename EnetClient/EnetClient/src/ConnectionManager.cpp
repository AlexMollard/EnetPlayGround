#include "ConnectionManager.h"

#include "ChatManager.h"
#include "UIManager.h"
#include "GameClient.h"

void ConnectionManager::startConnection(const std::string& username, const std::string& password, bool rememberCredentials)
{
	// Initialize state
	connectionProgress = 0.0f;
	GameClient::currentGameState = CurrentGameState::Connecting;
	uiManager->setErrorMessage("");

	// Ensure NetworkManager is initialized first
	if (!networkManager->clientSetup())
	{
		// Initialize NetworkManager
		if (!networkManager->initialize())
		{
			uiManager->setErrorMessage("Failed to initialize client");
			GameClient::currentGameState = CurrentGameState::LoginScreen;
			return;
		}
	}

	// Update state
	connectionProgress = 0.1f;

	// Define authentication success handler
	std::function<void(uint32_t)> handleAuthSuccess = [this](uint32_t playerId)
	{
		// Update player state
		playerManager->setMyPlayerId(playerId);
		GameClient::currentGameState = CurrentGameState::Connected;

		// Send position update
		ENetPeer* serverPeer = networkManager->getServerPeer();
		if (serverPeer)
		{
			auto posRequest = networkManager->GetPacketManager()->createPositionUpdate(playerId, playerManager->getMyPosition());
			networkManager->GetPacketManager()->sendPacket(serverPeer, *posRequest, true);
		}
		else
		{
			logger.error("Failed to get server peer after authentication");
		}
	};

	// Define authentication failure handler
	std::function<void(const std::string&)> handleAuthFailure = [this](const std::string& errorMsg)
	{
		threadManager->scheduleUITask(
		        [this, errorMsg]()
		        {
			        uiManager->setErrorMessage("Authentication failed: " + errorMsg);
			        GameClient::currentGameState = CurrentGameState::LoginScreen;
			        networkManager->disconnect(true);
		        });
	};

	// Schedule the connection and authentication task
	threadManager->scheduleNetworkTask(
	        [this, username, password, rememberCredentials, handleAuthSuccess, handleAuthFailure]()
	        {
		        // Connect to server
		        if (!networkManager->connectToServer(networkManager->getServerAddress().c_str(), networkManager->getServerPort()))
		        {
			        threadManager->scheduleUITask(
			                [this]()
			                {
				                uiManager->setErrorMessage("Failed to connect to server");
				                GameClient::currentGameState = CurrentGameState::LoginScreen;
			                });
			        return;
		        }

		        // Update connection progress
		        threadManager->scheduleUITask([this]() { connectionProgress = 0.5f; });

		        // Attempt authentication
		        if (!authManager->authenticate(username, password, rememberCredentials, handleAuthSuccess, handleAuthFailure))
		        {
			        threadManager->scheduleUITask(
			                [this]()
			                {
				                uiManager->setErrorMessage("Failed to start authentication");
				                GameClient::currentGameState = CurrentGameState::LoginScreen;
			                });
			        networkManager->disconnect(true);
		        }
		        else
		        {
			        threadManager->scheduleUITask(
			                [this]()
			                {
				                GameClient::currentGameState = CurrentGameState::Authenticating;
				                connectionProgress = 0.7f;
			                });
		        }
	        });

	// Clear the chat messages
	chatManager->clearChatMessages();
}

void ConnectionManager::disconnect(bool tellServer)
{
	// Let NetworkManager handle the actual disconnection
	networkManager->disconnect(tellServer);

	// Update client state
	GameClient::currentGameState = CurrentGameState::LoginScreen;
}

// Handle received packet with the new packet system
void ConnectionManager::handlePacket(const ENetPacket* enetPacket)
{
	// Process the packet using the PacketManager
	auto packet = networkManager->GetPacketManager()->receivePacket(enetPacket);

	// Check if we got a valid packet
	if (!packet)
	{
		logger.error("Received invalid packet");
		return;
	}

	// Handle packet based on type
	GameProtocol::PacketType packetType = packet->getType();

	switch (packetType)
	{
		case GameProtocol::PacketType::AuthResponse:
		{
			auto* authResponse = dynamic_cast<GameProtocol::AuthResponsePacket*>(packet.get());
			if (authResponse)
			{
				handleAuthResponse(*authResponse);
			}
			break;
		}

		case GameProtocol::PacketType::PositionUpdate:
		{
			auto* posUpdate = dynamic_cast<GameProtocol::PositionUpdatePacket*>(packet.get());
			if (posUpdate)
			{
				// Not sure right now if this is other players also or just my own
				playerManager->handlePositionUpdate(*posUpdate);
			}
			break;
		}

		case GameProtocol::PacketType::ChatMessage:
		{
			auto* chatMessage = dynamic_cast<GameProtocol::ChatMessagePacket*>(packet.get());
			if (chatMessage)
			{
				handleChatMessage(*chatMessage);
			}
			break;
		}

		case GameProtocol::PacketType::SystemMessage:
		{
			auto* sysMessage = dynamic_cast<GameProtocol::SystemMessagePacket*>(packet.get());
			if (sysMessage)
			{
				handleSystemMessage(*sysMessage);
			}
			break;
		}

		case GameProtocol::PacketType::Teleport:
		{
			auto* teleport = dynamic_cast<GameProtocol::TeleportPacket*>(packet.get());
			if (teleport)
			{
				handleTeleport(*teleport);
			}
			break;
		}

		case GameProtocol::PacketType::WorldState:
		{
			auto* worldState = dynamic_cast<GameProtocol::WorldStatePacket*>(packet.get());
			if (worldState)
			{
				playerManager->handleWorldState(*worldState);
			}
			break;
		}

		case GameProtocol::PacketType::Heartbeat:
		{
			// Just acknowledge heartbeat
			break;
		}

		default:
			logger.warning("Received unhandled packet type: " + std::to_string(static_cast<int>(packetType)));
			break;
	}
}

void ConnectionManager::updateNetwork()
{
	// Only process network updates if connected or connecting
	if (GameClient::currentGameState == CurrentGameState::LoginScreen)
	{
		return;
	}

	// Get current time for delta calculations
	time_t currentTime = time(0);

	// Use NetworkManager to process packets and handle events
	networkManager->update(
	        // Position update callback
	        [this]()
	        {
		        time_t currentTime = time(0);
		        if (currentTime - lastPositionUpdateTime >= positionUpdateRateMs && authManager->isAuthenticated())
		        {
			        const auto myPosition = playerManager->getMyPosition();
			        const auto lastSentPosition = playerManager->getLastSentPosition();
			        auto myPlayerId = playerManager->getMyPlayerId();

			        // Create position update packet
			        if (networkManager->isCompressionEnabled())
			        {
				        // Use delta position update if appropriate
				        if (std::abs(myPosition.x - lastSentPosition.x) > movementThreshold || std::abs(myPosition.y - lastSentPosition.y) > movementThreshold || std::abs(myPosition.z - lastSentPosition.z) > movementThreshold)
				        {
					        // Get server peer
					        ENetPeer* serverPeer = networkManager->getServerPeer();

					        if (serverPeer)
					        {
						        auto deltaPacket = networkManager->GetPacketManager()->createDeltaPositionUpdate(myPosition);
						        networkManager->GetPacketManager()->sendPacket(serverPeer, *deltaPacket, false);
					        }
				        }
			        }
			        else
			        {
				        // Use full position update
				        ENetPeer* serverPeer = networkManager->getServerPeer();
				        if (serverPeer)
				        {
					        auto posPacket = networkManager->GetPacketManager()->createPositionUpdate(myPlayerId, myPosition);
					        networkManager->GetPacketManager()->sendPacket(serverPeer, *posPacket, false);
				        }
			        }

			        playerManager->setLastSentPosition(myPosition);
			        lastPositionUpdateTime = currentTime;
		        }
	        },
	        // Packet handler callback
	        [this](const ENetPacket* packet) { handlePacket(packet); },
	        // Disconnect callback
	        [this]() { signOut(); });

	// Connection health checks at regular intervals
	if (currentTime - lastConnectionCheckTime >= 1000)
	{
		networkManager->sendPing();
		networkManager->checkConnectionHealth();
		lastConnectionCheckTime = currentTime;
	}
}

void ConnectionManager::signOut()
{
	// Disconnect from the server
	disconnect(true);

	// Clear any sensitive session data
	playerManager->clearPlayers();

	// Return to login screen
	GameClient::currentGameState = CurrentGameState::LoginScreen;

	logger.debug("User signed out");

	chatManager->clearChatMessages();
}

const float ConnectionManager::getConnectionProgress() const
{
	return connectionProgress;
}

void ConnectionManager::setChatManager(std::shared_ptr<ChatManager> chatManager)
{
	this->chatManager = chatManager;
}

void ConnectionManager::setUIManager(std::shared_ptr<UIManager> uiManager)
{
	this->uiManager = uiManager;
}

void ConnectionManager::setPlayerManager(std::shared_ptr<PlayerManager> playerManager)
{
	this->playerManager = playerManager;
}

void ConnectionManager::setAuthManager(std::shared_ptr<AuthManager> authManager)
{
	this->authManager = authManager;
}

void ConnectionManager::setNetworkManager(std::shared_ptr<NetworkManager> networkManager)
{
	this->networkManager = networkManager;
}

void ConnectionManager::setThreadManager(std::shared_ptr<ThreadManager> threadManager)
{
	this->threadManager = threadManager;
}

void ConnectionManager::handleAuthResponse(const GameProtocol::AuthResponsePacket& packet)
{
	if (packet.success)
	{
		// Authentication successful
		uint32_t myPlayerId = packet.playerId;
		GameClient::currentGameState = CurrentGameState::Connected;

		// Send a request to get player position
		auto posRequest = networkManager->GetPacketManager()->createPositionUpdate(myPlayerId, playerManager->getPlayerPosition(myPlayerId));
		networkManager->GetPacketManager()->sendPacket(networkManager->getServer(), *posRequest, true);
	}
	else
	{
		// Authentication failed
		uiManager->setErrorMessage("Authentication failed: " + packet.message);
		GameClient::currentGameState = CurrentGameState::LoginScreen;
		networkManager->disconnect(true);
	}
}

void ConnectionManager::handleChatMessage(const GameProtocol::ChatMessagePacket& packet)
{
	// Skip our own messages that are echoed back
	if (packet.sender == playerManager->getMyPlayer().name)
		return;

	// Add to chat
	chatManager->addChatMessage(packet.sender, packet.message);
}

void ConnectionManager::handleSystemMessage(const GameProtocol::SystemMessagePacket& packet)
{
	chatManager->addChatMessage("Server", packet.message);
}

void ConnectionManager::handleTeleport(const GameProtocol::TeleportPacket& packet)
{
	// Update position
	playerManager->setMyPosition({ packet.position.x, packet.position.y, packet.position.z });
	playerManager->setLastSentPosition({ packet.position.x, packet.position.y, packet.position.z });

	// Add system message
	chatManager->addChatMessage("System", "You have been teleported to a new location.");
}
