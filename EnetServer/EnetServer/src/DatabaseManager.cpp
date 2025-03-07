#include "DatabaseManager.h"

DatabaseManager::DatabaseManager(Logger& loggerRef)
      : connection(nullptr), host("localhost"), user("gameserver"), password(""), database("gameserver"), port(3306), connected(false), logger(loggerRef)
{
}

DatabaseManager::~DatabaseManager()
{
	disconnect();
}

bool DatabaseManager::connect()
{
	if (connected)
		return true;

	// Initialize MySQL connection
	connection = mysql_init(nullptr);
	if (connection == nullptr)
	{
		logger.error("Failed to initialize MySQL connection");
		return false;
	}

	// Enable auto-reconnect for reliability
	bool reconnect = 1;
	if (mysql_options(connection, MYSQL_OPT_RECONNECT, &reconnect))
	{
		logger.warning("Failed to set MySQL reconnect option");
	}

	// Connect to database
	if (mysql_real_connect(connection, host.c_str(), user.c_str(), password.c_str(), database.c_str(), port, nullptr, 0) == nullptr)
	{
		logger.error("Failed to connect to MySQL database: " + std::string(mysql_error(connection)));
		mysql_close(connection);
		connection = nullptr;
		return false;
	}

	connected = true;
	logger.info("Connected to MySQL database: " + database);

	// Ensure tables exist
	if (!createTablesIfNotExist())
	{
		logger.error("Failed to create database tables");
		disconnect();
		return false;
	}

	return true;
}

void DatabaseManager::disconnect()
{
	if (connection != nullptr)
	{
		mysql_close(connection);
		connection = nullptr;
		connected = false;
		logger.info("Disconnected from MySQL database");
	}
}

void DatabaseManager::setConnectionInfo(const std::string& dbHost, const std::string& dbUser, const std::string& dbPassword, const std::string& dbName, int dbPort)
{
	host = dbHost;
	user = dbUser;
	password = dbPassword;
	database = dbName;
	port = dbPort;
}

bool DatabaseManager::executeQuery(const std::string& query)
{
	if (!connected && !connect())
	{
		return false;
	}

	if (mysql_query(connection, query.c_str()) != 0)
	{
		logger.error("MySQL query error: " + std::string(mysql_error(connection)));
		logger.error("Query was: " + query);

		// Check if we lost connection
		if (mysql_errno(connection) == CR_SERVER_GONE_ERROR || mysql_errno(connection) == CR_SERVER_LOST)
		{
			logger.warning("Lost connection to MySQL server. Attempting to reconnect...");
			connected = false;
			if (!connect())
			{
				logger.error("Failed to reconnect to MySQL server");
				return false;
			}
			// Retry the query after reconnection
			if (mysql_query(connection, query.c_str()) != 0)
			{
				logger.error("MySQL query still failed after reconnect: " + std::string(mysql_error(connection)));
				return false;
			}
			return true;
		}

		return false;
	}

	return true;
}

MySQLResultPtr DatabaseManager::getQueryResult()
{
	MYSQL_RES* result = mysql_store_result(connection);
	return MySQLResultPtr(result);
}

bool DatabaseManager::beginTransaction()
{
	return executeQuery("START TRANSACTION");
}

bool DatabaseManager::commitTransaction()
{
	return executeQuery("COMMIT");
}

bool DatabaseManager::rollbackTransaction()
{
	return executeQuery("ROLLBACK");
}

std::string DatabaseManager::escapeString(const std::string& str)
{
	if (!connected && !connect())
	{
		return str;
	}

	// Allocate buffer for escaped string (needs 2x length + 1 for worst case)
	size_t escapedLength = str.length() * 2 + 1;
	std::vector<char> escaped(escapedLength, 0);

	// Escape the string
	mysql_real_escape_string(connection, escaped.data(), str.c_str(), str.length());

	// Return as string
	return std::string(escaped.data());
}

bool DatabaseManager::createTablesIfNotExist()
{
	// Players table
	std::string createPlayersTable = "CREATE TABLE IF NOT EXISTS players ("
	                                 "id INT UNSIGNED PRIMARY KEY,"
	                                 "username VARCHAR(64) UNIQUE NOT NULL,"
	                                 "password_hash VARCHAR(128) NOT NULL,"
	                                 "last_login TIMESTAMP NULL DEFAULT NULL,"
	                                 "registration_time TIMESTAMP NULL DEFAULT NULL,"
	                                 "is_admin BOOLEAN DEFAULT FALSE,"
	                                 "login_count INT UNSIGNED DEFAULT 0,"
	                                 "last_ip_address VARCHAR(45)"
	                                 ")";

	// Player positions table
	std::string createPositionsTable = "CREATE TABLE IF NOT EXISTS player_positions ("
	                                   "player_id INT UNSIGNED PRIMARY KEY,"
	                                   "pos_x FLOAT NOT NULL,"
	                                   "pos_y FLOAT NOT NULL,"
	                                   "pos_z FLOAT NOT NULL,"
	                                   "last_update TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
	                                   "FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE"
	                                   ")";

	// Create player statistics table (optional, for future expansion)
	std::string createStatsTable = "CREATE TABLE IF NOT EXISTS player_stats ("
	                               "player_id INT UNSIGNED PRIMARY KEY,"
	                               "play_time_seconds INT UNSIGNED DEFAULT 0,"
	                               "last_active TIMESTAMP NULL DEFAULT NULL,"
	                               "FOREIGN KEY (player_id) REFERENCES players(id) ON DELETE CASCADE"
	                               ")";

	// Execute table creation queries
	bool success = true;
	success &= executeQuery(createPlayersTable);
	success &= executeQuery(createPositionsTable);
	success &= executeQuery(createStatsTable);

	return success;
}

// Player data operations
bool DatabaseManager::saveAuthData(const std::unordered_map<std::string, AuthData>& authData)
{
	if (!connected && !connect())
	{
		return false;
	}

	// Begin transaction
	if (!beginTransaction())
	{
		return false;
	}

	bool success = true;

	for (const auto& pair: authData)
	{
		const AuthData& auth = pair.second;

		// Escape strings to prevent SQL injection
		std::string safeUsername = escapeString(auth.username);
		std::string safePasswordHash = escapeString(auth.passwordHash);
		std::string safeIpAddress = escapeString(auth.lastIpAddress);

		// Format timestamps
		std::string lastLoginTime = std::to_string(auth.lastLoginTime);
		std::string registrationTime = std::to_string(auth.registrationTime);

		// Create SQL query to insert or update player
		std::string query = "INSERT INTO players (id, username, password_hash, last_login, registration_time, is_admin, login_count, last_ip_address) "
		                    "VALUES ("
		                    + std::to_string(auth.playerId) + ", '" + safeUsername + "', '" + safePasswordHash
		                    + "', "
		                      "FROM_UNIXTIME("
		                    + lastLoginTime + "), FROM_UNIXTIME(" + registrationTime + "), " + (auth.isAdmin ? "TRUE" : "FALSE") + ", " + std::to_string(auth.loginCount) + ", '" + safeIpAddress
		                    + "') "
		                      "ON DUPLICATE KEY UPDATE "
		                      "password_hash = '"
		                    + safePasswordHash
		                    + "', "
		                      "last_login = FROM_UNIXTIME("
		                    + lastLoginTime
		                    + "), "
		                      "is_admin = "
		                    + (auth.isAdmin ? "TRUE" : "FALSE")
		                    + ", "
		                      "login_count = "
		                    + std::to_string(auth.loginCount)
		                    + ", "
		                      "last_ip_address = '"
		                    + safeIpAddress + "'";

		if (!executeQuery(query))
		{
			success = false;
			break;
		}

		// Update player position
		query = "INSERT INTO player_positions (player_id, pos_x, pos_y, pos_z) "
		        "VALUES ("
		        + std::to_string(auth.playerId) + ", " + std::to_string(auth.lastPosition.x) + ", " + std::to_string(auth.lastPosition.y) + ", " + std::to_string(auth.lastPosition.z)
		        + ") "
		          "ON DUPLICATE KEY UPDATE "
		          "pos_x = "
		        + std::to_string(auth.lastPosition.x) + ", " + "pos_y = " + std::to_string(auth.lastPosition.y) + ", " + "pos_z = " + std::to_string(auth.lastPosition.z);

		if (!executeQuery(query))
		{
			success = false;
			break;
		}
	}

	// Commit or rollback transaction based on success
	if (success)
	{
		if (!commitTransaction())
		{
			return false;
		}
	}
	else
	{
		rollbackTransaction();
	}

	return success;
}

bool DatabaseManager::loadAuthData(std::unordered_map<std::string, AuthData>& authData, uint32_t& nextPlayerId)
{
	if (!connected && !connect())
	{
		return false;
	}

	// Clear existing data
	authData.clear();

	// Query to get player data
	std::string query = "SELECT p.id, p.username, p.password_hash, UNIX_TIMESTAMP(p.last_login) as last_login, "
	                    "UNIX_TIMESTAMP(p.registration_time) as registration_time, p.is_admin, p.login_count, "
	                    "p.last_ip_address, pp.pos_x, pp.pos_y, pp.pos_z "
	                    "FROM players p "
	                    "LEFT JOIN player_positions pp ON p.id = pp.player_id";

	if (!executeQuery(query))
	{
		return false;
	}

	MySQLResultPtr result = getQueryResult();
	if (!result)
	{
		logger.error("Failed to get query result");
		return false;
	}

	uint32_t maxId = 0;

	MYSQL_ROW row;
	while ((row = mysql_fetch_row(result.get())))
	{
		AuthData auth;

		// Parse player data
		auth.playerId = row[0] ? static_cast<uint32_t>(std::stoul(row[0])) : 0;
		auth.username = row[1] ? row[1] : "";
		auth.passwordHash = row[2] ? row[2] : "";
		auth.lastLoginTime = row[3] ? std::stoll(row[3]) : 0;
		auth.registrationTime = row[4] ? std::stoll(row[4]) : 0;
		auth.isAdmin = row[5] ? (std::string(row[5]) == "1") : false;
		auth.loginCount = row[6] ? std::stoul(row[6]) : 0;
		auth.lastIpAddress = row[7] ? row[7] : "";

		// Parse position data
		auth.lastPosition.x = row[8] ? std::stof(row[8]) : 0.0f;
		auth.lastPosition.y = row[9] ? std::stof(row[9]) : 0.0f;
		auth.lastPosition.z = row[10] ? std::stof(row[10]) : 0.0f;

		// Store in the map
		authData[auth.username] = auth;

		// Track highest player ID
		if (auth.playerId > maxId)
		{
			maxId = auth.playerId;
		}

		logger.debug("Loaded account: " + auth.username + " (ID: " + std::to_string(auth.playerId) + ")");
	}

	// Update nextPlayerId to be one more than the highest ID found
	if (maxId >= nextPlayerId)
	{
		nextPlayerId = maxId + 1;
		logger.info("Updated nextPlayerId to " + std::to_string(nextPlayerId));
	}

	logger.info("Successfully loaded " + std::to_string(authData.size()) + " player accounts from database");

	return true;
}

bool DatabaseManager::savePlayerPosition(uint32_t playerId, const Position& position)
{
	if (!connected && !connect())
	{
		return false;
	}

	std::string query = "INSERT INTO player_positions (player_id, pos_x, pos_y, pos_z) "
	                    "VALUES ("
	                    + std::to_string(playerId) + ", " + std::to_string(position.x) + ", " + std::to_string(position.y) + ", " + std::to_string(position.z)
	                    + ") "
	                      "ON DUPLICATE KEY UPDATE "
	                      "pos_x = "
	                    + std::to_string(position.x) + ", " + "pos_y = " + std::to_string(position.y) + ", " + "pos_z = " + std::to_string(position.z);

	return executeQuery(query);
}

bool DatabaseManager::getPlayerIdByName(const std::string& username, uint32_t& playerId)
{
	if (!connected && !connect())
	{
		return false;
	}

	std::string safeUsername = escapeString(username);
	std::string query = "SELECT id FROM players WHERE username = '" + safeUsername + "'";

	if (!executeQuery(query))
	{
		return false;
	}

	MySQLResultPtr result = getQueryResult();
	if (!result)
	{
		return false;
	}

	MYSQL_ROW row = mysql_fetch_row(result.get());
	if (row && row[0])
	{
		playerId = static_cast<uint32_t>(std::stoul(row[0]));
		return true;
	}

	return false;
}
