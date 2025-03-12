#pragma once

// Version information
#define VERSION "0.1.0"

namespace Constants
{
	namespace Files
	{
		static const char* AUTH_DB_FILE = "player_auth.dat";  // Player authentication database file
		static const char* WORLD_DB_FILE = "world_data.dat";  // World state and environment data file
		static const char* DEBUG_LOG_FILE = "server.log";     // Server debug and error logging file
		static const char* CONFIG_FILE = "server_config.cfg"; // Server configuration settings file
	} // namespace Files

	namespace Server
	{
		static const int MAX_PLAYERS = 500;                // Maximum number of concurrent players allowed
		static const int DEFAULT_PORT = 7777;              // Default server listening port
		static const int BROADCAST_RATE_MS = 100;          // Rate at which updates are broadcast to clients (milliseconds)
		static const int TIMEOUT_CHECK_INTERVAL_MS = 5000; // How often to check for timed-out connections (milliseconds)
		static const int PLAYER_TIMEOUT_MS = 30000;        // Time after which inactive players are disconnected (milliseconds)
		static const int SAVE_INTERVAL_MS = 60000;         // How often to auto-save world data (milliseconds)
		static const int MAX_CHAT_HISTORY = 100;           // Maximum number of chat messages to store in history
		static const int MAX_PASSWORD_ATTEMPTS = 5;        // Maximum failed password attempts before temporary lockout
		static const char* ADMIN_PASSWORD = "admin123";    // Default admin password (should be changed)
	} // namespace Server

	namespace Player
	{
		namespace Spawn
		{
			static const float DEFAULT_X = 0.0f; // Default spawn X coordinate
			static const float DEFAULT_Y = 0.0f; // Default spawn Y coordinate
			static const float DEFAULT_Z = 0.0f; // Default spawn Z coordinate
		} // namespace Spawn

		static const float INTEREST_RADIUS = 100.0f;  // Only broadcast players within this radius
		static const float MAX_MOVEMENT_SPEED = 2.0f; // Max allowed movement speed per update
	} // namespace Player

	namespace Security
	{
		static const bool MOVEMENT_VALIDATION = true;     // Enable movement validation (not sure if this should be in this namespace just yet)
		static const bool SECURE_PASSWORD_STORAGE = true; // Use secure hash for passwords
	} // namespace Security

	namespace Database
	{
		static const bool USE_DATABASE = true;  // Enable database storage
		static const char* HOST = "localhost";  // Database host
		static const char* USER = "gameserver"; // Database username
		static const char* PASSWORD = "";       // Will be loaded from secure source
		static const char* NAME = "gameserver"; // Database name
		static const int PORT = 3306;           // Database port
	} // namespace Database
} // namespace Constants
