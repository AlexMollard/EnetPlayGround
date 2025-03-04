#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#include <filesystem>
#include <hello_imgui/hello_imgui.h>
#include <iostream>

#include "IconsLucide.h"
#include "GameClient.h"
#include "Logger.h"

constexpr float BASE_FONT_SIZE = 16.0f;

struct FontConfig
{
	std::string path;
	float size;
	const ImWchar* ranges;
	ImFontConfig* config;
};

bool LoadFont(const FontConfig& fontConfig, ImFontAtlas* fonts)
{
	if (!std::filesystem::exists(fontConfig.path))
	{
		return false;
	}
	return fonts->AddFontFromFileTTF(fontConfig.path.c_str(), fontConfig.size, fontConfig.config, fontConfig.ranges) != nullptr;
}

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

	// Setup font loading callback
	params.callbacks.LoadAdditionalFonts = [&client]()
	{
		const std::string resDir = IsDebuggerPresent() ? "../../res/" : "";
		ImGuiIO& io = ImGui::GetIO();

		// Regular font configuration
		FontConfig regularFont{ resDir + "JetBrainsMono.ttf", BASE_FONT_SIZE, nullptr, nullptr };

		// Icon font configuration
		static const ImWchar icons_ranges[] = { ICON_MIN_LC, ICON_MAX_LC, 0 };
		ImFontConfig icons_config;
		icons_config.MergeMode = true;
		icons_config.PixelSnapH = true;
		icons_config.OversampleH = 2;
		icons_config.OversampleV = 2;
		icons_config.GlyphOffset = ImVec2(0, 3);

		FontConfig iconFont{ resDir + "lucide.ttf", BASE_FONT_SIZE, icons_ranges, &icons_config };

		// Load fonts and handle errors
		client->getLogger().debug("Loading fonts...");
		client->getLogger().debug("Regular font: " + regularFont.path);
		client->getLogger().debug("Icon font: " + iconFont.path);
		if (!LoadFont(regularFont, io.Fonts))
		{
			client->getLogger().error("Failed to load regular font");
		}

		if (!LoadFont(iconFont, io.Fonts))
		{
			client->getLogger().error("Failed to load icon font");
		}

		io.FontGlobalScale = 1.0f;

		client->getLogger().debug("Fonts loaded!");
	};

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
