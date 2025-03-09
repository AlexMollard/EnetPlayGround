#include "MarkDownHelper.h"

#include <cstring>
#include <sstream>
#include <windows.h>
#include <shellapi.h>
#include "IconsLucide.h"

// Main function to render markdown content
void MarkdownHelper::RenderMarkdown(const std::string& markdown, float wrapWidth, const UITheme& theme)
{
	// Save original styles
	ImVec4 originalTextColor = ImGui::GetStyle().Colors[ImGuiCol_Text];
	float originalFontSize = ImGui::GetFontSize();

	// Text wrapping setup
	ImGui::PushTextWrapPos(wrapWidth);

	// Parse and render markdown
	std::istringstream stream(markdown);
	std::string line;
	bool inCodeBlock = false;
	std::string codeBlockContent;

	while (std::getline(stream, line))
	{
		// Skip empty lines
		if (line.empty())
		{
			ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() * 0.5f));
			continue;
		}

		// Check for code blocks (```code```)
		if (line.find("```") == 0)
		{
			if (inCodeBlock)
			{
				// End code block, render accumulated content
				RenderCodeBlock(codeBlockContent, wrapWidth, theme);
				codeBlockContent.clear();
			}
			else
			{
				// Start collecting code block content
				codeBlockContent = "";
			}
			inCodeBlock = !inCodeBlock;
			continue;
		}

		if (inCodeBlock)
		{
			// Accumulate code block content
			codeBlockContent += line + "\n";
			continue;
		}

		// Check for headers (# Header)
		if (line[0] == '#')
		{
			size_t level = 0;
			while (level < line.size() && line[level] == '#')
				level++;

			if (level > 0 && level <= 6 && level < line.size() && line[level] == ' ')
			{
				// Extract header text and render
				std::string headerText = line.substr(level + 1); // Skip "# " prefix
				RenderHeader(headerText, level, theme);
				continue;
			}
		}

		// Check for bullet lists
		if (line.size() >= 2 && (line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ')
		{
			std::string bulletText = line.substr(2); // Skip "- " prefix
			RenderBulletItem(bulletText, theme);
			continue;
		}

		// Regular paragraph - process inline formatting
		RenderInlineMarkdown(line, theme);

		// Add line spacing
		ImGui::Spacing();
	}

	// If we're still in a code block at the end, close it
	if (inCodeBlock && !codeBlockContent.empty())
	{
		RenderCodeBlock(codeBlockContent, wrapWidth, theme);
	}

	// Pop text wrap
	ImGui::PopTextWrapPos();
}

// Process inline markdown elements (bold, italic, code, links)
void MarkdownHelper::RenderInlineMarkdown(const std::string& text, const UITheme& theme)
{
	const char* remaining = text.c_str();
	const char* textEnd = remaining + text.size();

	while (remaining < textEnd)
	{
		const char* markdownStart = nullptr;
		const char* markdownEnd = nullptr;

		// Check for bold (**text**)
		if (strncmp(remaining, "**", 2) == 0)
		{
			markdownStart = remaining;
			markdownEnd = strstr(markdownStart + 2, "**");

			if (markdownEnd)
			{
				// Display text before the formatting
				if (markdownStart > remaining)
				{
					ImGui::TextUnformatted(remaining, markdownStart);
					ImGui::SameLine(0.0f, 0.0f);
				}

				// Display bold text
				std::string boldText(markdownStart + 2, markdownEnd);
				RenderBoldText(boldText, theme);

				remaining = markdownEnd + 2;
				ImGui::SameLine(0.0f, 0.0f);
				continue;
			}
		}

		// Check for italic (*text*)
		if (*remaining == '*' && (remaining + 1 < textEnd) && *(remaining + 1) != '*')
		{
			markdownStart = remaining;
			markdownEnd = strchr(markdownStart + 1, '*');

			if (markdownEnd)
			{
				// Display text before the formatting
				if (markdownStart > remaining)
				{
					ImGui::TextUnformatted(remaining, markdownStart);
					ImGui::SameLine(0.0f, 0.0f);
				}

				// Display italic text
				std::string italicText(markdownStart + 1, markdownEnd);
				RenderItalicText(italicText, theme);

				remaining = markdownEnd + 1;
				ImGui::SameLine(0.0f, 0.0f);
				continue;
			}
		}

		// Check for inline code (`code`)
		if (*remaining == '`')
		{
			markdownStart = remaining;
			markdownEnd = strchr(markdownStart + 1, '`');

			if (markdownEnd)
			{
				// Display text before the formatting
				if (markdownStart > remaining)
				{
					ImGui::TextUnformatted(remaining, markdownStart);
					ImGui::SameLine(0.0f, 0.0f);
				}

				// Display inline code
				std::string codeText(markdownStart + 1, markdownEnd);
				RenderInlineCode(codeText, theme);

				remaining = markdownEnd + 1;
				ImGui::SameLine(0.0f, 0.0f);
				continue;
			}
		}

		// Check for links ([text](url))
		if (*remaining == '[')
		{
			const char* linkTextStart = remaining + 1;
			const char* linkTextEnd = strchr(linkTextStart, ']');

			if (linkTextEnd && *(linkTextEnd + 1) == '(')
			{
				const char* urlStart = linkTextEnd + 2;
				const char* urlEnd = strchr(urlStart, ')');

				if (urlEnd)
				{
					// Display text before the link
					if (remaining > text.c_str())
					{
						ImGui::TextUnformatted(text.c_str(), remaining);
						ImGui::SameLine(0.0f, 0.0f);
					}

					// Extract link text and URL
					std::string linkText(linkTextStart, linkTextEnd);
					std::string url(urlStart, urlEnd);

					// Render link
					RenderLink(linkText, url, theme);

					remaining = urlEnd + 1;
					ImGui::SameLine(0.0f, 0.0f);
					continue;
				}
			}
		}

		// No special formatting, just advance one character
		const char* nextChar = remaining + 1;
		while (nextChar < textEnd && !strchr("*`[", *nextChar))
			nextChar++;

		ImGui::TextUnformatted(remaining, nextChar);
		if (nextChar < textEnd)
			ImGui::SameLine(0.0f, 0.0f);

		remaining = nextChar;
	}
}

// Render header with appropriate styling
void MarkdownHelper::RenderHeader(const std::string& text, int level, const UITheme& theme)
{
	// Scale factor for headers (h1 is largest, h6 is smallest)
	float scaleFactor = 1.0f + (0.5f - (level * 0.1f));

	// Push styling for header
	ImGui::PushStyleColor(ImGuiCol_Text, theme.accentPrimary);

	ImGui::SetWindowFontScale(scaleFactor);

	ImGui::TextWrapped("%s", text.c_str());

	// Pop styling
	ImGui::PopStyleColor();

	ImGui::SetWindowFontScale(1.0f);

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
}

// Render code block
void MarkdownHelper::RenderCodeBlock(const std::string& code, float width, const UITheme& theme)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 0.6f));
	ImGui::BeginChild(("CodeBlock" + std::to_string(ImGui::GetCursorPosY())).c_str(), ImVec2(width - 20, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Border);
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f)); // Light gray text

	// Use monospace font
	ImGui::PushFont(GetMonoFont());

	// Split the code into lines
	std::istringstream codeStream(code);
	std::string line;

	while (std::getline(codeStream, line))
	{
		ImGui::TextUnformatted(line.c_str());
	}

	// Pop monospace font if used
	ImGui::PopFont();

	ImGui::PopStyleColor(); // Text color
	ImGui::EndChild();
	ImGui::PopStyleColor(); // Background color

	ImGui::Spacing();
}

// Render bullet item
void MarkdownHelper::RenderBulletItem(const std::string& text, const UITheme& theme)
{
	float indent = ImGui::GetStyle().IndentSpacing * 0.5f;
	ImGui::Indent(indent);

	// Bullet point
	ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_Text], ICON_LC_DOT);
	ImGui::SameLine();

	// Process inline formatting in the bullet item
	RenderInlineMarkdown(text, theme);

	ImGui::Unindent(indent);
	ImGui::Spacing();
}

// Render hyperlink
void MarkdownHelper::RenderLink(const std::string& text, const std::string& url, const UITheme& theme)
{
	// Display as a colored link
	ImGui::PushStyleColor(ImGuiCol_Text, theme.accentPrimary);
	ImGui::TextUnformatted(text.c_str());

	// Handle hover effect and click
	if (ImGui::IsItemHovered())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		ImGui::SetTooltip("%s", url.c_str());

		if (ImGui::IsMouseClicked(0))
		{
			OpenUrl(url);
		}
	}

	ImGui::PopStyleColor();
}

// Render inline code
void MarkdownHelper::RenderInlineCode(const std::string& code, const UITheme& theme)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.5f, 1.0f));   // Light orange
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f)); // Dark background

	ImGui::PushFont(GetMonoFont());

	if (ImGui::Button(code.c_str()))
	{
		// copy the code to clipboard
		ImGui::SetClipboardText(code.c_str());
	}

	ImGui::PopFont();

	ImGui::PopStyleColor(2);
}

// Render bold text
void MarkdownHelper::RenderBoldText(const std::string& text, const UITheme& theme)
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme.textPrimary);

	// In a real implementation, you would use a bold font:
	ImGui::PushFont(GetBoldFont());

	ImGui::TextUnformatted(text.c_str());

	ImGui::PopFont();

	ImGui::PopStyleColor();
}

// Render italic text
void MarkdownHelper::RenderItalicText(const std::string& text, const UITheme& theme)
{
	ImGui::PushStyleColor(ImGuiCol_Text, theme.textPrimary);

	// In a real implementation, you would use an italic font:
	ImGui::PushFont(GetItalicFont());

	ImGui::TextUnformatted(text.c_str());

	ImGui::PopFont();

	ImGui::PopStyleColor();
}

// Utility function to check if a link is hovered
bool MarkdownHelper::IsLinkHovered()
{
	return ImGui::IsItemHovered();
}

// Utility function to open URL
void MarkdownHelper::OpenUrl(const std::string& url)
{
   // Convert std::string to std::wstring
   std::wstring wideUrl(url.begin(), url.end());

   // Platform-specific URL opening code
#ifdef _WIN32
   ShellExecute(0, 0, wideUrl.c_str(), 0, 0, SW_SHOW);
#elif defined(__APPLE__)
   std::string command = "open " + url;
   system(command.c_str());
#else
   std::string command = "xdg-open " + url;
   system(command.c_str());
#endif
}

ImFont* MarkdownHelper::GetMonoFont()
{
	return ImGui::GetIO().Fonts->Fonts[3];
}

ImFont* MarkdownHelper::GetBoldFont()
{
	return ImGui::GetIO().Fonts->Fonts[2];
}

ImFont* MarkdownHelper::GetItalicFont()
{
	return ImGui::GetIO().Fonts->Fonts[4];
}
