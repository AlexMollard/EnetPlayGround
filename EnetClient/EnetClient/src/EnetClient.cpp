#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#include <hello_imgui/hello_imgui.h>
#include <iostream>

#include "GameClient.h"
#include "Logger.h"

// Main function
int main(int argc, char* argv[])
{
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

	// Create game client - pass command line parameters to constructor
	std::shared_ptr<GameClient> client = std::make_shared<GameClient>();

	// Configure ImGui style
	params.callbacks.SetupImGuiStyle = [client]() { client->setupImGuiTheme(); };

	// Initialize the client, but don't connect yet - let the user initiate connection from UI
	params.callbacks.PostInit = [client]()
	{
		// Initialize network components only
		if (!client->initialize())
		{
			std::cerr << "Failed to initialize client. Exiting." << std::endl;
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
