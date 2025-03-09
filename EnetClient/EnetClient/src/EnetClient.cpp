#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

#include <filesystem>
#include <hello_imgui/hello_imgui.h>
#include <iostream>

#include "GameClient.h"
#include "IconsLucide.h"
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

		// Font sizes
		const float BASE_FONT_SIZE = 16.0f;   // Regular text
		const float HEADER_FONT_SIZE = 22.0f; // Headers/titles
		const float SMALL_FONT_SIZE = 14.0f;  // Small text (optional)

		// ImWchar ranges for icons
		static const ImWchar icons_ranges[] = { ICON_MIN_LC, ICON_MAX_LC, 0 };

		// --- Font 0: Regular font with icons ---
		{
			// Base font config
			ImFontConfig baseConfig;
			baseConfig.SizePixels = BASE_FONT_SIZE;
			strcpy_s(baseConfig.Name, "Regular");

			// Regular font
			FontConfig regularFont{ resDir + "WorkSans-Regular.ttf", BASE_FONT_SIZE, nullptr, &baseConfig };
			if (!LoadFont(regularFont, io.Fonts))
			{
				client->getLogger().error("Failed to load regular font");
			}

			// Icons config to merge with regular font
			ImFontConfig iconsConfig;
			iconsConfig.MergeMode = true;
			iconsConfig.PixelSnapH = true;
			iconsConfig.OversampleH = 2;
			iconsConfig.OversampleV = 2;
			iconsConfig.GlyphOffset = ImVec2(0, 3);
			strcpy_s(iconsConfig.Name, "Icons-Regular");

			// Icon font to merge with regular font
			FontConfig iconFont{ resDir + "lucide.ttf", BASE_FONT_SIZE, icons_ranges, &iconsConfig };
			if (!LoadFont(iconFont, io.Fonts))
			{
				client->getLogger().error("Failed to load icon font for regular");
			}
		}

		// --- Font 1: Header font with icons ---
		{
			// Header font config
			ImFontConfig headerConfig;
			headerConfig.SizePixels = HEADER_FONT_SIZE;
			strcpy_s(headerConfig.Name, "Header");

			// Header font
			FontConfig headerFont{ resDir + "WorkSans-Bold.ttf", HEADER_FONT_SIZE, nullptr, &headerConfig };
			if (!LoadFont(headerFont, io.Fonts))
			{
				client->getLogger().error("Failed to load header font");
			}

			// Icons config to merge with header font
			ImFontConfig iconsConfig;
			iconsConfig.MergeMode = true;
			iconsConfig.PixelSnapH = true;
			iconsConfig.OversampleH = 2;
			iconsConfig.OversampleV = 2;
			iconsConfig.GlyphOffset = ImVec2(0, 3);
			strcpy_s(iconsConfig.Name, "Icons-Header");

			// Icon font to merge with header font
			FontConfig iconFont{ resDir + "lucide.ttf", HEADER_FONT_SIZE, icons_ranges, &iconsConfig };
			if (!LoadFont(iconFont, io.Fonts))
			{
				client->getLogger().error("Failed to load icon font for header");
			}
		}

		// --- Font 2: Bold font ---
		{
			// Bold font config
			ImFontConfig boldConfig;
			boldConfig.SizePixels = BASE_FONT_SIZE;
			strcpy_s(boldConfig.Name, "Bold");

			// Bold font
			FontConfig boldFont{ resDir + "WorkSans-Bold.ttf", BASE_FONT_SIZE, nullptr, &boldConfig };
			if (!LoadFont(boldFont, io.Fonts))
			{
				client->getLogger().error("Failed to load bold font");
			}
		}

		// --- Font 3: Monospace font ---
		{
			// Monospace font config
			ImFontConfig monoConfig;
			monoConfig.SizePixels = BASE_FONT_SIZE;
			strcpy_s(monoConfig.Name, "Monospace");

			// Monospace font
			FontConfig monoFont{ resDir + "JetBrainsMono.ttf", BASE_FONT_SIZE, nullptr, &monoConfig };
			if (!LoadFont(monoFont, io.Fonts))
			{
				client->getLogger().error("Failed to load monospace font");
			}
		}

		// --- Font 4: Italics font ---
		{
			// Italics font config
			ImFontConfig italicsConfig;
			italicsConfig.SizePixels = BASE_FONT_SIZE;
			strcpy_s(italicsConfig.Name, "Italics");

			// Italics font
			FontConfig italicsFont{ resDir + "WorkSans-Italic.ttf", BASE_FONT_SIZE, nullptr, &italicsConfig };
			if (!LoadFont(italicsFont, io.Fonts))
			{
				client->getLogger().error("Failed to load italics font");
			}
		}

		io.FontGlobalScale = 1.0f;
		client->getLogger().debug("Fonts loaded!");

		// Log the number of loaded fonts for debugging
		client->getLogger().debug("Total fonts loaded: " + std::to_string(io.Fonts->Fonts.Size));
	};

	// Configure ImGui style
	params.callbacks.SetupImGuiStyle = [client]() { client->applyTheme(); };

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
