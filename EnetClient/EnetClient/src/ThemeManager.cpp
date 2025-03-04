#include "ThemeManager.h"

ThemeManager::ThemeManager()
{
	createDefaultThemes();
	currentTheme = themes["Gruvbox"]; // Default theme
}

void ThemeManager::createDefaultThemes()
{
	// --- Dark Theme (Default) ---
	UITheme darkTheme;
	darkTheme.name = "Dark";

	// Text colors
	darkTheme.textPrimary = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
	darkTheme.textSecondary = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
	darkTheme.textAccent = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	darkTheme.textError = ImVec4(1.00f, 0.30f, 0.30f, 1.00f);

	// Background colors
	darkTheme.bgPrimary = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
	darkTheme.bgSecondary = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
	darkTheme.bgTertiary = ImVec4(0.20f, 0.20f, 0.26f, 1.00f);
	darkTheme.bgInput = ImVec4(0.20f, 0.20f, 0.26f, 1.00f);

	// Accent colors
	darkTheme.accentPrimary = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	darkTheme.accentSecondary = ImVec4(0.20f, 0.41f, 0.68f, 1.00f);
	darkTheme.accentHover = ImVec4(0.30f, 0.63f, 1.00f, 1.00f);
	darkTheme.accentActive = ImVec4(0.20f, 0.51f, 0.90f, 1.00f);

	// Button colors
	darkTheme.buttonPrimary = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
	darkTheme.buttonSecondary = ImVec4(0.30f, 0.40f, 0.60f, 0.80f);
	darkTheme.buttonDanger = ImVec4(0.80f, 0.30f, 0.30f, 0.80f);
	darkTheme.buttonDisabled = ImVec4(0.50f, 0.50f, 0.50f, 0.40f);

	// Border colors
	darkTheme.borderPrimary = ImVec4(0.25f, 0.25f, 0.30f, 0.50f);
	darkTheme.borderSecondary = ImVec4(0.30f, 0.30f, 0.38f, 0.50f);

	// Status colors
	darkTheme.statusOnline = ImVec4(0.20f, 0.80f, 0.40f, 1.00f);
	darkTheme.statusConnecting = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	darkTheme.statusOffline = ImVec4(0.90f, 0.30f, 0.30f, 1.00f);

	// Player and message colors
	darkTheme.playerSelf = ImVec4(0.90f, 0.90f, 0.20f, 1.00f);
	darkTheme.playerOthers = ImVec4(0.40f, 0.80f, 0.40f, 1.00f);
	darkTheme.systemMessage = ImVec4(0.85f, 0.75f, 0.55f, 1.00f);

	// Gradient colors
	darkTheme.gradientStart = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
	darkTheme.gradientEnd = ImVec4(0.26f, 0.59f, 0.98f, 0.10f);

	// Style settings
	darkTheme.windowRounding = 12.0f;
	darkTheme.childRounding = 8.0f;
	darkTheme.frameRounding = 6.0f;
	darkTheme.popupRounding = 6.0f;
	darkTheme.scrollbarRounding = 10.0f;
	darkTheme.grabRounding = 6.0f;
	darkTheme.tabRounding = 6.0f;

	darkTheme.borderSize = 1.0f;
	darkTheme.childBorderSize = 1.0f;
	darkTheme.frameBorderSize = 0.0f;
	darkTheme.tabBorderSize = 0.0f;

	darkTheme.windowPadding = ImVec2(15, 15);
	darkTheme.framePadding = ImVec2(10, 7);
	darkTheme.itemSpacing = ImVec2(12, 8);
	darkTheme.itemInnerSpacing = ImVec2(8, 6);
	darkTheme.scrollbarSize = 15.0f;
	darkTheme.grabMinSize = 5.0f;

	themes["Dark"] = darkTheme;

	// --- Light Theme ---
	UITheme lightTheme = darkTheme; // Start with dark theme as base
	lightTheme.name = "Light";

	// Text colors
	lightTheme.textPrimary = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
	lightTheme.textSecondary = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
	lightTheme.textAccent = ImVec4(0.00f, 0.45f, 0.90f, 1.00f);
	lightTheme.textError = ImVec4(0.90f, 0.20f, 0.20f, 1.00f);

	// Background colors
	lightTheme.bgPrimary = ImVec4(0.94f, 0.94f, 0.96f, 1.00f);
	lightTheme.bgSecondary = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
	lightTheme.bgTertiary = ImVec4(0.85f, 0.85f, 0.87f, 1.00f);
	lightTheme.bgInput = ImVec4(0.85f, 0.85f, 0.90f, 1.00f);

	// Accent colors
	lightTheme.accentPrimary = ImVec4(0.00f, 0.45f, 0.90f, 1.00f);
	lightTheme.accentSecondary = ImVec4(0.10f, 0.35f, 0.70f, 1.00f);
	lightTheme.accentHover = ImVec4(0.10f, 0.55f, 1.00f, 1.00f);
	lightTheme.accentActive = ImVec4(0.00f, 0.40f, 0.80f, 1.00f);

	// Button colors
	lightTheme.buttonPrimary = ImVec4(0.00f, 0.45f, 0.90f, 0.70f);
	lightTheme.buttonSecondary = ImVec4(0.70f, 0.70f, 0.80f, 0.80f);
	lightTheme.buttonDanger = ImVec4(0.90f, 0.30f, 0.30f, 0.80f);
	lightTheme.buttonDisabled = ImVec4(0.70f, 0.70f, 0.70f, 0.40f);

	// Border colors
	lightTheme.borderPrimary = ImVec4(0.70f, 0.70f, 0.75f, 0.50f);
	lightTheme.borderSecondary = ImVec4(0.60f, 0.60f, 0.68f, 0.50f);

	// Status colors - keep similar to dark theme for visibility
	lightTheme.statusOnline = ImVec4(0.10f, 0.70f, 0.30f, 1.00f);
	lightTheme.statusConnecting = ImVec4(0.80f, 0.60f, 0.00f, 1.00f);
	lightTheme.statusOffline = ImVec4(0.80f, 0.20f, 0.20f, 1.00f);

	// Player and message colors
	lightTheme.playerSelf = ImVec4(0.70f, 0.70f, 0.00f, 1.00f);
	lightTheme.playerOthers = ImVec4(0.30f, 0.70f, 0.30f, 1.00f);
	lightTheme.systemMessage = ImVec4(0.75f, 0.65f, 0.45f, 1.00f);

	// Gradient colors
	lightTheme.gradientStart = ImVec4(0.00f, 0.45f, 0.90f, 0.70f);
	lightTheme.gradientEnd = ImVec4(0.00f, 0.45f, 0.90f, 0.10f);

	themes["Light"] = lightTheme;

	// --- Cyberpunk Theme ---
	UITheme cyberTheme = darkTheme; // Start with dark theme as base
	cyberTheme.name = "Cyberpunk";

	// Text colors
	cyberTheme.textPrimary = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
	cyberTheme.textSecondary = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
	cyberTheme.textAccent = ImVec4(0.00f, 0.95f, 0.95f, 1.00f);
	cyberTheme.textError = ImVec4(1.00f, 0.20f, 0.50f, 1.00f);

	// Background colors
	cyberTheme.bgPrimary = ImVec4(0.05f, 0.05f, 0.10f, 1.00f);
	cyberTheme.bgSecondary = ImVec4(0.10f, 0.10f, 0.15f, 1.00f);
	cyberTheme.bgTertiary = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);
	cyberTheme.bgInput = ImVec4(0.15f, 0.15f, 0.20f, 1.00f);

	// Accent colors
	cyberTheme.accentPrimary = ImVec4(0.00f, 0.95f, 0.95f, 1.00f);
	cyberTheme.accentSecondary = ImVec4(0.90f, 0.10f, 0.50f, 1.00f);
	cyberTheme.accentHover = ImVec4(0.10f, 1.00f, 1.00f, 1.00f);
	cyberTheme.accentActive = ImVec4(0.00f, 0.80f, 0.80f, 1.00f);

	// Button colors
	cyberTheme.buttonPrimary = ImVec4(0.00f, 0.70f, 0.70f, 0.70f);
	cyberTheme.buttonSecondary = ImVec4(0.90f, 0.10f, 0.50f, 0.70f);
	cyberTheme.buttonDanger = ImVec4(1.00f, 0.10f, 0.40f, 0.80f);
	cyberTheme.buttonDisabled = ImVec4(0.40f, 0.40f, 0.50f, 0.40f);

	// Border colors
	cyberTheme.borderPrimary = ImVec4(0.00f, 0.70f, 0.70f, 0.50f);
	cyberTheme.borderSecondary = ImVec4(0.90f, 0.10f, 0.50f, 0.50f);

	// Status colors
	cyberTheme.statusOnline = ImVec4(0.10f, 0.90f, 0.50f, 1.00f);
	cyberTheme.statusConnecting = ImVec4(0.90f, 0.90f, 0.10f, 1.00f);
	cyberTheme.statusOffline = ImVec4(0.90f, 0.10f, 0.30f, 1.00f);

	// Player and message colors
	cyberTheme.playerSelf = ImVec4(0.00f, 0.90f, 0.90f, 1.00f);
	cyberTheme.playerOthers = ImVec4(0.90f, 0.10f, 0.50f, 1.00f);
	cyberTheme.systemMessage = ImVec4(0.90f, 0.90f, 0.10f, 1.00f);

	// Gradient colors
	cyberTheme.gradientStart = ImVec4(0.00f, 0.80f, 0.80f, 0.70f);
	cyberTheme.gradientEnd = ImVec4(0.90f, 0.10f, 0.50f, 0.70f);

	themes["Cyberpunk"] = cyberTheme;

	// --- Forest Theme ---
	UITheme forestTheme = darkTheme; // Start with dark theme as base
	forestTheme.name = "Forest";

	// Text colors
	forestTheme.textPrimary = ImVec4(0.95f, 0.95f, 0.90f, 1.00f);
	forestTheme.textSecondary = ImVec4(0.75f, 0.80f, 0.70f, 1.00f);
	forestTheme.textAccent = ImVec4(0.50f, 0.80f, 0.30f, 1.00f);
	forestTheme.textError = ImVec4(0.90f, 0.40f, 0.30f, 1.00f);

	// Background colors
	forestTheme.bgPrimary = ImVec4(0.08f, 0.12f, 0.08f, 1.00f);
	forestTheme.bgSecondary = ImVec4(0.10f, 0.15f, 0.10f, 1.00f);
	forestTheme.bgTertiary = ImVec4(0.14f, 0.20f, 0.14f, 1.00f);
	forestTheme.bgInput = ImVec4(0.14f, 0.20f, 0.14f, 1.00f);

	// Accent colors
	forestTheme.accentPrimary = ImVec4(0.50f, 0.80f, 0.30f, 1.00f);
	forestTheme.accentSecondary = ImVec4(0.70f, 0.60f, 0.30f, 1.00f);
	forestTheme.accentHover = ImVec4(0.60f, 0.90f, 0.40f, 1.00f);
	forestTheme.accentActive = ImVec4(0.40f, 0.70f, 0.20f, 1.00f);

	// Button colors
	forestTheme.buttonPrimary = ImVec4(0.40f, 0.70f, 0.20f, 0.70f);
	forestTheme.buttonSecondary = ImVec4(0.60f, 0.50f, 0.30f, 0.70f);
	forestTheme.buttonDanger = ImVec4(0.80f, 0.40f, 0.30f, 0.80f);
	forestTheme.buttonDisabled = ImVec4(0.40f, 0.40f, 0.40f, 0.40f);

	// Border colors
	forestTheme.borderPrimary = ImVec4(0.30f, 0.40f, 0.20f, 0.50f);
	forestTheme.borderSecondary = ImVec4(0.40f, 0.30f, 0.20f, 0.50f);

	// Status colors
	forestTheme.statusOnline = ImVec4(0.40f, 0.80f, 0.30f, 1.00f);
	forestTheme.statusConnecting = ImVec4(0.80f, 0.70f, 0.30f, 1.00f);
	forestTheme.statusOffline = ImVec4(0.80f, 0.40f, 0.30f, 1.00f);

	// Player and message colors
	forestTheme.playerSelf = ImVec4(0.60f, 0.90f, 0.40f, 1.00f);
	forestTheme.playerOthers = ImVec4(0.70f, 0.60f, 0.30f, 1.00f);
	forestTheme.systemMessage = ImVec4(0.80f, 0.70f, 0.50f, 1.00f);

	// Gradient colors
	forestTheme.gradientStart = ImVec4(0.50f, 0.80f, 0.30f, 0.70f);
	forestTheme.gradientEnd = ImVec4(0.70f, 0.60f, 0.30f, 0.70f);

	themes["Forest"] = forestTheme;

	// --- Gruvbox Dark Theme ---
	UITheme gruvboxTheme = darkTheme; // Start with dark theme as base
	gruvboxTheme.name = "Gruvbox";

	// Text colors
	gruvboxTheme.textPrimary = ImVec4(0.98f, 0.95f, 0.78f, 1.00f);   // light0 (#fbf1c7)
	gruvboxTheme.textSecondary = ImVec4(0.92f, 0.86f, 0.70f, 1.00f); // light1 (#ebdbb2)
	gruvboxTheme.textAccent = ImVec4(0.72f, 0.73f, 0.15f, 1.00f);    // bright_green (#b8bb26)
	gruvboxTheme.textError = ImVec4(0.98f, 0.29f, 0.20f, 1.00f);     // bright_red (#fb4934)

	// Background colors
	gruvboxTheme.bgPrimary = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);   // dark0 (#282828)
	gruvboxTheme.bgSecondary = ImVec4(0.24f, 0.22f, 0.21f, 1.00f); // dark1 (#3c3836)
	gruvboxTheme.bgTertiary = ImVec4(0.31f, 0.28f, 0.27f, 1.00f);  // dark2 (#504945)
	gruvboxTheme.bgInput = ImVec4(0.24f / 1.5f, 0.22f / 1.5f, 0.21f / 1.5f, 1.00f); // dark1 (#3c3836)

	// Accent colors
	gruvboxTheme.accentPrimary = ImVec4(0.72f, 0.73f, 0.15f, 1.00f);   // bright_green (#b8bb26)
	gruvboxTheme.accentSecondary = ImVec4(0.98f, 0.74f, 0.18f, 1.00f); // bright_yellow (#fabd2f)
	gruvboxTheme.accentHover = ImVec4(0.80f, 0.81f, 0.25f, 1.00f);     // brighter green
	gruvboxTheme.accentActive = ImVec4(0.65f, 0.65f, 0.12f, 1.00f);    // darker green

	// Button colors
	gruvboxTheme.buttonPrimary = ImVec4(0.72f, 0.73f, 0.15f, 0.60f);   // bright_green with alpha
	gruvboxTheme.buttonSecondary = ImVec4(0.98f, 0.74f, 0.18f, 0.60f); // bright_yellow with alpha
	gruvboxTheme.buttonDanger = ImVec4(0.98f, 0.29f, 0.20f, 0.70f);    // bright_red with alpha
	gruvboxTheme.buttonDisabled = ImVec4(0.40f, 0.36f, 0.32f, 0.40f);  // dark3/dark4 with alpha

	// Border colors
	gruvboxTheme.borderPrimary = ImVec4(0.40f, 0.36f, 0.32f, 0.50f);   // dark3 (#665c54) with alpha
	gruvboxTheme.borderSecondary = ImVec4(0.49f, 0.44f, 0.39f, 0.50f); // dark4 (#7c6f64) with alpha

	// Status colors
	gruvboxTheme.statusOnline = ImVec4(0.56f, 0.75f, 0.49f, 1.00f);     // bright_aqua (#8ec07c)
	gruvboxTheme.statusConnecting = ImVec4(0.98f, 0.74f, 0.18f, 1.00f); // bright_yellow (#fabd2f)
	gruvboxTheme.statusOffline = ImVec4(0.98f, 0.29f, 0.20f, 1.00f);    // bright_red (#fb4934)

	// Player and message colors
	gruvboxTheme.playerSelf = ImVec4(0.98f, 0.74f, 0.18f, 1.00f);    // bright_yellow (#fabd2f)
	gruvboxTheme.playerOthers = ImVec4(0.56f, 0.75f, 0.49f, 1.00f);  // bright_aqua (#8ec07c)
	gruvboxTheme.systemMessage = ImVec4(1.00f, 0.50f, 0.10f, 1.00f); // bright_orange (#fe8019)

	// Gradient colors
	gruvboxTheme.gradientStart = ImVec4(0.72f, 0.73f, 0.15f, 0.70f); // bright_green with alpha
	gruvboxTheme.gradientEnd = ImVec4(0.56f, 0.75f, 0.49f, 0.20f);   // bright_aqua with alpha

	// Style settings - slightly reduced roundness compared to other themes
	gruvboxTheme.windowRounding = 8.0f;
	gruvboxTheme.childRounding = 6.0f;
	gruvboxTheme.frameRounding = 4.0f;
	gruvboxTheme.popupRounding = 4.0f;
	gruvboxTheme.scrollbarRounding = 8.0f;
	gruvboxTheme.grabRounding = 4.0f;
	gruvboxTheme.tabRounding = 4.0f;

	themes["Gruvbox"] = gruvboxTheme;
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
