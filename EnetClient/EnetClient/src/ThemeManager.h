#pragma once

#include <imgui.h>
#include <string>
#include <unordered_map>

// Theme struct to hold all the color and style settings
struct UITheme
{
	// Theme name
	std::string name;

	// Color palette
	ImVec4 textPrimary;   // Primary text color
	ImVec4 textSecondary; // Secondary/muted text color
	ImVec4 textAccent;    // Accent text color (for headers)
	ImVec4 textError;     // Error message color

	ImVec4 bgPrimary;   // Main background color
	ImVec4 bgSecondary; // Secondary background (panels)
	ImVec4 bgTertiary;  // Tertiary background (child windows)
	ImVec4 bgInput;     // Input field background

	ImVec4 accentPrimary;   // Primary accent color
	ImVec4 accentSecondary; // Secondary accent color
	ImVec4 accentHover;     // Hover state for accent elements
	ImVec4 accentActive;    // Active state for accent elements

	ImVec4 buttonPrimary;   // Primary button color
	ImVec4 buttonSecondary; // Secondary button color
	ImVec4 buttonDanger;    // Danger/warning button color
	ImVec4 buttonDisabled;  // Disabled button color

	ImVec4 borderPrimary;   // Primary border color
	ImVec4 borderSecondary; // Secondary border color

	ImVec4 statusOnline;     // Online status indicator
	ImVec4 statusConnecting; // Connecting status indicator
	ImVec4 statusOffline;    // Offline status indicator

	ImVec4 playerSelf;    // Color for local player
	ImVec4 playerOthers;  // Color for other players
	ImVec4 systemMessage; // System message color

	ImVec4 gradientStart; // Gradient start color for headers/separators
	ImVec4 gradientEnd;   // Gradient end color for headers/separators

	// Style settings
	float windowRounding;    // Window corner rounding
	float childRounding;     // Child window corner rounding
	float frameRounding;     // Frame corner rounding
	float popupRounding;     // Popup corner rounding
	float scrollbarRounding; // Scrollbar corner rounding
	float grabRounding;      // Grab corner rounding
	float tabRounding;       // Tab corner rounding

	float borderSize;      // General border size
	float childBorderSize; // Child window border size
	float frameBorderSize; // Frame border size
	float tabBorderSize;   // Tab border size

	ImVec2 windowPadding;    // Window padding
	ImVec2 framePadding;     // Frame padding
	ImVec2 itemSpacing;      // Item spacing
	ImVec2 itemInnerSpacing; // Item inner spacing
	float scrollbarSize;     // Scrollbar size
	float grabMinSize;       // Minimum grab size
};

// Theme manager class
class ThemeManager
{
public:
	ThemeManager();
	~ThemeManager() = default;

	// Apply the current theme to ImGui
	void applyTheme();

	// Apply a specific theme by name
	bool setTheme(const std::string& themeName);

	// Get the current theme
	const UITheme& getCurrentTheme() const
	{
		return currentTheme;
	}

	// Get list of available themes
	std::vector<std::string> getThemeNames() const;

	// Add/modify a theme
	void addTheme(const UITheme& theme);

private:
	std::unordered_map<std::string, UITheme> themes;
	UITheme currentTheme;

	// Create default themes
	void createDefaultThemes();
};
