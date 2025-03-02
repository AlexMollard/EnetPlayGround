#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#include "GameClient.h"
#include "Logger.h"

// Main function
int main(int argc, char* argv[])
{
	// Parse command line arguments
	std::string serverAddress = DEFAULT_SERVER;
	uint16_t serverPort = DEFAULT_PORT;
	bool debugMode = false;

	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "-server" && i + 1 < argc)
		{
			serverAddress = argv[++i];
		}
		else if (arg == "-port" && i + 1 < argc)
		{
			serverPort = static_cast<uint16_t>(std::stoi(argv[++i]));
		}
		else if (arg == "-debug")
		{
			debugMode = true;
		}
		else if (arg == "-help" || arg == "-h" || arg == "--help")
		{
			std::cout << "MMO Client Usage:\n";
			std::cout << "  -server <address>  Server address (default: " << DEFAULT_SERVER << ")\n";
			std::cout << "  -port <port>       Server port (default: " << DEFAULT_PORT << ")\n";
			std::cout << "  -debug             Enable debug mode\n";
			std::cout << "  -help              Show this help\n";
			return 0;
		}
	}

	// Initialize ImGui parameters first
	HelloImGui::RunnerParams params;

	// Window configuration
	params.appWindowParams.windowTitle = "MMO Client";
	params.appWindowParams.windowGeometry.size = { 1024, 768 };
	params.appWindowParams.windowGeometry.sizeAuto = false;

	// Set application icon (optional)
	// params.appWindowParams.windowIcon = "path/to/icon.png";

	// Docking configuration
	params.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
	params.imGuiWindowParams.showMenuBar = false;
	params.imGuiWindowParams.showStatusBar = false;

	// Create game client
	std::shared_ptr<GameClient> client = std::make_shared<GameClient>("", "", debugMode);

	// Configure ImGui style
	params.callbacks.SetupImGuiStyle = [client]() { client->setupImGuiTheme(); };

	// Initialization of the client will happen in the setup phase
	params.callbacks.PostInit = [client, serverAddress, serverPort]()
	{
		// Initialize
		if (!client->initialize())
		{
			std::cerr << "Failed to initialize client. Exiting." << std::endl;
			HelloImGui::GetRunnerParams()->appShallExit = true;
			return;
		}

		// Connect to server
		if (!client->connectToServer(serverAddress.c_str(), serverPort))
		{
			std::cerr << "Failed to connect to server. Exiting." << std::endl;
			HelloImGui::GetRunnerParams()->appShallExit = true;
			return;
		}
	};

	// Set the rendering callback
	params.callbacks.ShowGui = [client]() { client->drawUI(); };

	// Set the frame callback for network updates
	params.callbacks.PreNewFrame = [client]()
	{
		client->updateNetwork();
		client->handleImGuiInput();
	};

	// Handle clean exit
	params.callbacks.BeforeExit = [client]()
	{
		// Perform clean shutdown
		client->disconnect();
	};

	// Run the Hello ImGui application
	HelloImGui::Run(params);

	return 0;
}
