#pragma once

// Configuration
#define VERSION "1.0.0"
#define AUTH_DB_FILE "player_auth.dat"
#define WORLD_DB_FILE "world_data.dat"
#define DEBUG_LOG_FILE "server.log"
#define CONFIG_FILE "server_config.cfg"
#define MAX_PLAYERS 500
#define DEFAULT_PORT 7777
#define BROADCAST_RATE_MS 100
#define TIMEOUT_CHECK_INTERVAL_MS 5000
#define PLAYER_TIMEOUT_MS 30000
#define SAVE_INTERVAL_MS 60000
#define MAX_CHAT_HISTORY 100
#define MAX_PASSWORD_ATTEMPTS 5
#define DEFAULT_SPAWN_X 0.0f
#define DEFAULT_SPAWN_Y 0.0f
#define DEFAULT_SPAWN_Z 0.0f
#define INTEREST_RADIUS 100.0f       // Only broadcast players within this radius
#define MOVEMENT_VALIDATION true     // Enable movement validation
#define MAX_MOVEMENT_SPEED 2.0f      // Max allowed movement speed per update
#define SECURE_PASSWORD_STORAGE true // Use secure hash for passwords
#define ADMIN_PASSWORD "admin123"    // Default admin password (should be changed)
