#pragma once
#include <string>

// Version information
#define VERSION "0.1.0"

namespace Constants
{
	namespace Files
	{
		static const char* CREDENTIALS_FILE = "client_credentials.dat"; // File storing user login credentials
		static const char* DEBUG_LOG_FILE = "client_debug.log";         // Client debug and error logging file
	} // namespace Files

	namespace UI
	{
		static const char* GAME_NAME = "MMO CLIENT"; // Game client display name
		constexpr int MAX_HISTORY_COMMANDS = 20;     // Maximum number of command history entries
		constexpr int MESSAGE_HISTORY_SIZE = 120;    // Maximum number of chat messages to display
	} // namespace UI

	namespace Network
	{
		constexpr int DEFAULT_PORT = 7777;               // Default server connection port
		static const char* DEFAULT_SERVER = "127.0.0.1"; // Default server address
		constexpr int MOVEMENT_UPDATE_RATE_MS = 50;      // Rate at which movement updates are sent (milliseconds)
	} // namespace Network

	namespace Security
	{
		constexpr bool SECURE_PASSWORD_STORAGE = true; // Use secure hash for stored passwords
	}

	// Runtime constants that shouldn't change after first initialization
	namespace Runtime
	{
		inline bool IsDebuggerPresent = false; // Flag indicating if debugger is attached
	}
} // namespace Constants
