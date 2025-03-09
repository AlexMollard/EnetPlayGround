#pragma once

#include <string>

#include "imgui.h"
#include "ThemeManager.h"

class MarkdownHelper
{
public:
	// Main function to render markdown content
	static void RenderMarkdown(const std::string& markdown, float wrapWidth, const UITheme& theme);

	// Parse and render various markdown elements
	static void RenderInlineMarkdown(const std::string& text, const UITheme& theme);

	// Helper functions for specific markdown elements
	static void RenderHeader(const std::string& text, int level, const UITheme& theme);
	static void RenderCodeBlock(const std::string& code, float width, const UITheme& theme);
	static void RenderBulletItem(const std::string& text, const UITheme& theme);
	static void RenderLink(const std::string& text, const std::string& url, const UITheme& theme);
	static void RenderInlineCode(const std::string& code, const UITheme& theme);
	static void RenderBoldText(const std::string& text, const UITheme& theme);
	static void RenderItalicText(const std::string& text, const UITheme& theme);

	// Utility functions
	static bool IsLinkHovered();
	static void OpenUrl(const std::string& url);

	static ImFont* GetMonoFont();
	static ImFont* GetBoldFont();
	static ImFont* GetItalicFont();
};
