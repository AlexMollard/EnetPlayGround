#include "ThemeManager.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

ThemeManager::ThemeManager(Logger& logger, bool isDebuggerAttached)
      : logger(logger), isDebuggerAttached(isDebuggerAttached)
{
	createDefaultThemes();
	loadTheme("current_theme.json");
	if (themes.find(currentTheme.name) == themes.end())
	{
		logger.warning("No default theme json was found");
		currentTheme = themes["Dark"];
	}
}

ImVec4 jsonToImVec4(const json& j)
{
	return ImVec4(j[0], j[1], j[2], j[3]);
}

void ThemeManager::createDefaultThemes()
{
	const std::string resDir = isDebuggerAttached ? "../../res/" : "";
	fs::path themesPath = resDir + "themes";
	if (fs::exists(themesPath) && fs::is_directory(themesPath))
	{
		for (const auto& entry: fs::directory_iterator(themesPath))
		{
			if (entry.path().extension() == ".json")
			{
				//logger.debug("Found json at: " + entry.path().string());
				loadThemeFromFile(entry.path().string());
			}
		}
	}
	else
	{
		logger.warning("Themes directory not found: " + themesPath.string());
	}
}

void ThemeManager::loadThemeFromFile(const std::string& filename)
{
	//logger.info("Loading theme from file: " + filename);
	std::ifstream file(filename);
	if (file.is_open())
	{
		json j;
		file >> j;
		file.close();

		UITheme theme;
		theme.name = j["name"];
		theme.textPrimary = jsonToImVec4(j["textPrimary"]);
		theme.textSecondary = jsonToImVec4(j["textSecondary"]);
		theme.textAccent = jsonToImVec4(j["textAccent"]);
		theme.textError = jsonToImVec4(j["textError"]);
		theme.bgPrimary = jsonToImVec4(j["bgPrimary"]);
		theme.bgSecondary = jsonToImVec4(j["bgSecondary"]);
		theme.bgTertiary = jsonToImVec4(j["bgTertiary"]);
		theme.bgInput = jsonToImVec4(j["bgInput"]);
		theme.accentPrimary = jsonToImVec4(j["accentPrimary"]);
		theme.accentSecondary = jsonToImVec4(j["accentSecondary"]);
		theme.accentHover = jsonToImVec4(j["accentHover"]);
		theme.accentActive = jsonToImVec4(j["accentActive"]);
		theme.buttonPrimary = jsonToImVec4(j["buttonPrimary"]);
		theme.buttonSecondary = jsonToImVec4(j["buttonSecondary"]);
		theme.buttonDanger = jsonToImVec4(j["buttonDanger"]);
		theme.buttonDisabled = jsonToImVec4(j["buttonDisabled"]);
		theme.borderPrimary = jsonToImVec4(j["borderPrimary"]);
		theme.borderSecondary = jsonToImVec4(j["borderSecondary"]);
		theme.statusOnline = jsonToImVec4(j["statusOnline"]);
		theme.statusConnecting = jsonToImVec4(j["statusConnecting"]);
		theme.statusOffline = jsonToImVec4(j["statusOffline"]);
		theme.playerSelf = jsonToImVec4(j["playerSelf"]);
		theme.playerOthers = jsonToImVec4(j["playerOthers"]);
		theme.systemMessage = jsonToImVec4(j["systemMessage"]);
		theme.gradientStart = jsonToImVec4(j["gradientStart"]);
		theme.gradientEnd = jsonToImVec4(j["gradientEnd"]);
		theme.windowRounding = j["windowRounding"];
		theme.childRounding = j["childRounding"];
		theme.frameRounding = j["frameRounding"];
		theme.popupRounding = j["popupRounding"];
		theme.scrollbarRounding = j["scrollbarRounding"];
		theme.grabRounding = j["grabRounding"];
		theme.tabRounding = j["tabRounding"];
		theme.borderSize = j["borderSize"];
		theme.childBorderSize = j["childBorderSize"];
		theme.frameBorderSize = j["frameBorderSize"];
		theme.tabBorderSize = j["tabBorderSize"];
		theme.windowPadding = ImVec2(j["windowPadding"][0], j["windowPadding"][1]);
		theme.framePadding = ImVec2(j["framePadding"][0], j["framePadding"][1]);
		theme.itemSpacing = ImVec2(j["itemSpacing"][0], j["itemSpacing"][1]);
		theme.itemInnerSpacing = ImVec2(j["itemInnerSpacing"][0], j["itemInnerSpacing"][1]);
		theme.scrollbarSize = j["scrollbarSize"];
		theme.grabMinSize = j["grabMinSize"];

		themes[theme.name] = theme;
	}
	else
	{
		logger.warning("Failed to open theme file: " + filename);
	}
}

void ThemeManager::saveTheme(const std::string& filename)
{
	json j;
	j["name"] = currentTheme.name;
	j["textPrimary"] = { currentTheme.textPrimary.x, currentTheme.textPrimary.y, currentTheme.textPrimary.z, currentTheme.textPrimary.w };
	j["textSecondary"] = { currentTheme.textSecondary.x, currentTheme.textSecondary.y, currentTheme.textSecondary.z, currentTheme.textSecondary.w };
	j["textAccent"] = { currentTheme.textAccent.x, currentTheme.textAccent.y, currentTheme.textAccent.z, currentTheme.textAccent.w };
	j["textError"] = { currentTheme.textError.x, currentTheme.textError.y, currentTheme.textError.z, currentTheme.textError.w };
	j["bgPrimary"] = { currentTheme.bgPrimary.x, currentTheme.bgPrimary.y, currentTheme.bgPrimary.z, currentTheme.bgPrimary.w };
	j["bgSecondary"] = { currentTheme.bgSecondary.x, currentTheme.bgSecondary.y, currentTheme.bgSecondary.z, currentTheme.bgSecondary.w };
	j["bgTertiary"] = { currentTheme.bgTertiary.x, currentTheme.bgTertiary.y, currentTheme.bgTertiary.z, currentTheme.bgTertiary.w };
	j["bgInput"] = { currentTheme.bgInput.x, currentTheme.bgInput.y, currentTheme.bgInput.z, currentTheme.bgInput.w };
	j["accentPrimary"] = { currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, currentTheme.accentPrimary.w };
	j["accentSecondary"] = { currentTheme.accentSecondary.x, currentTheme.accentSecondary.y, currentTheme.accentSecondary.z, currentTheme.accentSecondary.w };
	j["accentHover"] = { currentTheme.accentHover.x, currentTheme.accentHover.y, currentTheme.accentHover.z, currentTheme.accentHover.w };
	j["accentActive"] = { currentTheme.accentActive.x, currentTheme.accentActive.y, currentTheme.accentActive.z, currentTheme.accentActive.w };
	j["buttonPrimary"] = { currentTheme.buttonPrimary.x, currentTheme.buttonPrimary.y, currentTheme.buttonPrimary.z, currentTheme.buttonPrimary.w };
	j["buttonSecondary"] = { currentTheme.buttonSecondary.x, currentTheme.buttonSecondary.y, currentTheme.buttonSecondary.z, currentTheme.buttonSecondary.w };
	j["buttonDanger"] = { currentTheme.buttonDanger.x, currentTheme.buttonDanger.y, currentTheme.buttonDanger.z, currentTheme.buttonDanger.w };
	j["buttonDisabled"] = { currentTheme.buttonDisabled.x, currentTheme.buttonDisabled.y, currentTheme.buttonDisabled.z, currentTheme.buttonDisabled.w };
	j["borderPrimary"] = { currentTheme.borderPrimary.x, currentTheme.borderPrimary.y, currentTheme.borderPrimary.z, currentTheme.borderPrimary.w };
	j["borderSecondary"] = { currentTheme.borderSecondary.x, currentTheme.borderSecondary.y, currentTheme.borderSecondary.z, currentTheme.borderSecondary.w };
	j["statusOnline"] = { currentTheme.statusOnline.x, currentTheme.statusOnline.y, currentTheme.statusOnline.z, currentTheme.statusOnline.w };
	j["statusConnecting"] = { currentTheme.statusConnecting.x, currentTheme.statusConnecting.y, currentTheme.statusConnecting.z, currentTheme.statusConnecting.w };
	j["statusOffline"] = { currentTheme.statusOffline.x, currentTheme.statusOffline.y, currentTheme.statusOffline.z, currentTheme.statusOffline.w };
	j["playerSelf"] = { currentTheme.playerSelf.x, currentTheme.playerSelf.y, currentTheme.playerSelf.z, currentTheme.playerSelf.w };
	j["playerOthers"] = { currentTheme.playerOthers.x, currentTheme.playerOthers.y, currentTheme.playerOthers.z, currentTheme.playerOthers.w };
	j["systemMessage"] = { currentTheme.systemMessage.x, currentTheme.systemMessage.y, currentTheme.systemMessage.z, currentTheme.systemMessage.w };
	j["gradientStart"] = { currentTheme.gradientStart.x, currentTheme.gradientStart.y, currentTheme.gradientStart.z, currentTheme.gradientStart.w };
	j["gradientEnd"] = { currentTheme.gradientEnd.x, currentTheme.gradientEnd.y, currentTheme.gradientEnd.z, currentTheme.gradientEnd.w };
	j["windowRounding"] = currentTheme.windowRounding;
	j["childRounding"] = currentTheme.childRounding;
	j["frameRounding"] = currentTheme.frameRounding;
	j["popupRounding"] = currentTheme.popupRounding;
	j["scrollbarRounding"] = currentTheme.scrollbarRounding;
	j["grabRounding"] = currentTheme.grabRounding;
	j["tabRounding"] = currentTheme.tabRounding;
	j["borderSize"] = currentTheme.borderSize;
	j["childBorderSize"] = currentTheme.childBorderSize;
	j["frameBorderSize"] = currentTheme.frameBorderSize;
	j["tabBorderSize"] = currentTheme.tabBorderSize;
	j["windowPadding"] = { currentTheme.windowPadding.x, currentTheme.windowPadding.y };
	j["framePadding"] = { currentTheme.framePadding.x, currentTheme.framePadding.y };
	j["itemSpacing"] = { currentTheme.itemSpacing.x, currentTheme.itemSpacing.y };
	j["itemInnerSpacing"] = { currentTheme.itemInnerSpacing.x, currentTheme.itemInnerSpacing.y };
	j["scrollbarSize"] = currentTheme.scrollbarSize;
	j["grabMinSize"] = currentTheme.grabMinSize;

	std::ofstream file(filename);
	if (file.is_open())
	{
		file << j.dump(4);
		file.close();
	}
}

void ThemeManager::loadTheme(const std::string& filename)
{
	//logger.debug("Loading theme: " + filename);
	std::ifstream file(filename);
	if (file.is_open())
	{
		json j;
		file >> j;
		file.close();

		currentTheme.name = j["name"];
		currentTheme.textPrimary = jsonToImVec4(j["textPrimary"]);
		currentTheme.textSecondary = jsonToImVec4(j["textSecondary"]);
		currentTheme.textAccent = jsonToImVec4(j["textAccent"]);
		currentTheme.textError = jsonToImVec4(j["textError"]);
		currentTheme.bgPrimary = jsonToImVec4(j["bgPrimary"]);
		currentTheme.bgSecondary = jsonToImVec4(j["bgSecondary"]);
		currentTheme.bgTertiary = jsonToImVec4(j["bgTertiary"]);
		currentTheme.bgInput = jsonToImVec4(j["bgInput"]);
		currentTheme.accentPrimary = jsonToImVec4(j["accentPrimary"]);
		currentTheme.accentSecondary = jsonToImVec4(j["accentSecondary"]);
		currentTheme.accentHover = jsonToImVec4(j["accentHover"]);
		currentTheme.accentActive = jsonToImVec4(j["accentActive"]);
		currentTheme.buttonPrimary = jsonToImVec4(j["buttonPrimary"]);
		currentTheme.buttonSecondary = jsonToImVec4(j["buttonSecondary"]);
		currentTheme.buttonDanger = jsonToImVec4(j["buttonDanger"]);
		currentTheme.buttonDisabled = jsonToImVec4(j["buttonDisabled"]);
		currentTheme.borderPrimary = jsonToImVec4(j["borderPrimary"]);
		currentTheme.borderSecondary = jsonToImVec4(j["borderSecondary"]);
		currentTheme.statusOnline = jsonToImVec4(j["statusOnline"]);
		currentTheme.statusConnecting = jsonToImVec4(j["statusConnecting"]);
		currentTheme.statusOffline = jsonToImVec4(j["statusOffline"]);
		currentTheme.playerSelf = jsonToImVec4(j["playerSelf"]);
		currentTheme.playerOthers = jsonToImVec4(j["playerOthers"]);
		currentTheme.systemMessage = jsonToImVec4(j["systemMessage"]);
		currentTheme.gradientStart = jsonToImVec4(j["gradientStart"]);
		currentTheme.gradientEnd = jsonToImVec4(j["gradientEnd"]);
		currentTheme.windowRounding = j["windowRounding"];
		currentTheme.childRounding = j["childRounding"];
		currentTheme.frameRounding = j["frameRounding"];
		currentTheme.popupRounding = j["popupRounding"];
		currentTheme.scrollbarRounding = j["scrollbarRounding"];
		currentTheme.grabRounding = j["grabRounding"];
		currentTheme.tabRounding = j["tabRounding"];
		currentTheme.borderSize = j["borderSize"];
		currentTheme.childBorderSize = j["childBorderSize"];
		currentTheme.frameBorderSize = j["frameBorderSize"];
		currentTheme.tabBorderSize = j["tabBorderSize"];
		currentTheme.windowPadding = ImVec2(j["windowPadding"][0], j["windowPadding"][1]);
		currentTheme.framePadding = ImVec2(j["framePadding"][0], j["framePadding"][1]);
		currentTheme.itemSpacing = ImVec2(j["itemSpacing"][0], j["itemSpacing"][1]);
		currentTheme.itemInnerSpacing = ImVec2(j["itemInnerSpacing"][0], j["itemInnerSpacing"][1]);
		currentTheme.scrollbarSize = j["scrollbarSize"];
		currentTheme.grabMinSize = j["grabMinSize"];
	}
	else
	{
		logger.warning("Failed to load theme: " + filename);
	}
}

void ThemeManager::applyTheme()
{
	ImGuiStyle& style = ImGui::GetStyle();

	// Set style parameters
	style.WindowRounding = currentTheme.windowRounding;
	style.ChildRounding = currentTheme.childRounding;
	style.FrameRounding = currentTheme.frameRounding;
	style.PopupRounding = currentTheme.popupRounding;
	style.ScrollbarRounding = currentTheme.scrollbarRounding;
	style.GrabRounding = currentTheme.grabRounding;
	style.TabRounding = currentTheme.tabRounding;

	style.WindowBorderSize = currentTheme.borderSize;
	style.ChildBorderSize = currentTheme.childBorderSize;
	style.FrameBorderSize = currentTheme.frameBorderSize;
	style.TabBorderSize = currentTheme.tabBorderSize;

	style.WindowPadding = currentTheme.windowPadding;
	style.FramePadding = currentTheme.framePadding;
	style.ItemSpacing = currentTheme.itemSpacing;
	style.ItemInnerSpacing = currentTheme.itemInnerSpacing;
	style.ScrollbarSize = currentTheme.scrollbarSize;
	style.GrabMinSize = currentTheme.grabMinSize;

	// Apply colors
	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text] = currentTheme.textPrimary;
	colors[ImGuiCol_TextDisabled] = currentTheme.textSecondary;
	colors[ImGuiCol_WindowBg] = currentTheme.bgPrimary;
	colors[ImGuiCol_ChildBg] = currentTheme.bgSecondary;
	colors[ImGuiCol_PopupBg] = currentTheme.bgSecondary;
	colors[ImGuiCol_Border] = currentTheme.borderPrimary;
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = currentTheme.bgInput;
	colors[ImGuiCol_FrameBgHovered] = ImVec4(currentTheme.bgInput.x + 0.05f, currentTheme.bgInput.y + 0.05f, currentTheme.bgInput.z + 0.05f, currentTheme.bgInput.w);
	colors[ImGuiCol_FrameBgActive] = ImVec4(currentTheme.bgInput.x + 0.10f, currentTheme.bgInput.y + 0.10f, currentTheme.bgInput.z + 0.10f, currentTheme.bgInput.w);
	colors[ImGuiCol_TitleBg] = currentTheme.bgPrimary;
	colors[ImGuiCol_TitleBgActive] = currentTheme.bgSecondary;
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(currentTheme.bgPrimary.x, currentTheme.bgPrimary.y, currentTheme.bgPrimary.z, 0.51f);
	colors[ImGuiCol_MenuBarBg] = currentTheme.bgSecondary;
	colors[ImGuiCol_ScrollbarBg] = ImVec4(currentTheme.bgPrimary.x - 0.02f, currentTheme.bgPrimary.y - 0.02f, currentTheme.bgPrimary.z - 0.02f, 0.53f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(currentTheme.bgTertiary.x, currentTheme.bgTertiary.y, currentTheme.bgTertiary.z, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(currentTheme.bgTertiary.x + 0.10f, currentTheme.bgTertiary.y + 0.10f, currentTheme.bgTertiary.z + 0.10f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(currentTheme.bgTertiary.x + 0.20f, currentTheme.bgTertiary.y + 0.20f, currentTheme.bgTertiary.z + 0.20f, 1.00f);
	colors[ImGuiCol_CheckMark] = currentTheme.accentPrimary;
	colors[ImGuiCol_SliderGrab] = ImVec4(currentTheme.accentPrimary.x - 0.02f, currentTheme.accentPrimary.y - 0.02f, currentTheme.accentPrimary.z - 0.02f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = currentTheme.accentPrimary;
	colors[ImGuiCol_Button] = currentTheme.buttonPrimary;
	colors[ImGuiCol_ButtonHovered] = currentTheme.accentHover;
	colors[ImGuiCol_ButtonActive] = currentTheme.accentActive;
	colors[ImGuiCol_Header] = ImVec4(currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, 0.31f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, 0.80f);
	colors[ImGuiCol_HeaderActive] = currentTheme.accentPrimary;
	colors[ImGuiCol_Separator] = currentTheme.borderSecondary;
	colors[ImGuiCol_SeparatorHovered] = ImVec4(currentTheme.borderSecondary.x, currentTheme.borderSecondary.y, currentTheme.borderSecondary.z, 0.78f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(currentTheme.borderSecondary.x, currentTheme.borderSecondary.y, currentTheme.borderSecondary.z, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, 0.20f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, 0.95f);
	colors[ImGuiCol_Tab] = ImVec4(currentTheme.bgSecondary.x + 0.03f, currentTheme.bgSecondary.y + 0.03f, currentTheme.bgSecondary.z + 0.03f, 0.95f);
	colors[ImGuiCol_TabHovered] = ImVec4(currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, 0.80f);
	colors[ImGuiCol_TabActive] = ImVec4(currentTheme.accentSecondary.x, currentTheme.accentSecondary.y, currentTheme.accentSecondary.z, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(currentTheme.bgPrimary.x, currentTheme.bgPrimary.y, currentTheme.bgPrimary.z, 1.00f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(currentTheme.bgSecondary.x, currentTheme.bgSecondary.y, currentTheme.bgSecondary.z, 1.00f);
	colors[ImGuiCol_DockingPreview] = ImVec4(currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, 0.70f);
	colors[ImGuiCol_DockingEmptyBg] = ImVec4(currentTheme.bgPrimary.x, currentTheme.bgPrimary.y, currentTheme.bgPrimary.z, 1.00f);
	colors[ImGuiCol_PlotLines] = ImVec4(currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, 0.80f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, 0.80f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(currentTheme.accentPrimary.x, currentTheme.accentPrimary.y, currentTheme.accentPrimary.z, 0.35f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_NavHighlight] = currentTheme.accentPrimary;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.70f);

	saveTheme("current_theme.json"); // Save theme to file
}

bool ThemeManager::setTheme(const std::string& themeName)
{
	if (themes.find(themeName) != themes.end())
	{
		currentTheme = themes[themeName];
		applyTheme();
		return true;
	}
	return false;
}

std::vector<std::string> ThemeManager::getThemeNames() const
{
	std::vector<std::string> themeNames;
	for (const auto& theme: themes)
	{
		themeNames.push_back(theme.first);
	}
	return themeNames;
}

void ThemeManager::addTheme(const UITheme& theme)
{
	themes[theme.name] = theme;
}
