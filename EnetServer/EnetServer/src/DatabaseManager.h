#pragma once

#include <cstring>
#include <memory>
#include <mysql/mysql.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "Logger.h"
#include "Structs.h"

// Helper struct to handle MYSQL_RES cleanup properly
struct MySQLResultDeleter
{
	void operator()(MYSQL_RES* res)
	{
		if (res)
			mysql_free_result(res);
	}
};

// Using unique_ptr with custom deleter for automatic cleanup
using MySQLResultPtr = std::unique_ptr<MYSQL_RES, MySQLResultDeleter>;

class DatabaseManager
{
public:
	DatabaseManager(Logger& loggerRef);
	~DatabaseManager();

	bool connect();
	void disconnect();

	void setConnectionInfo(const std::string& dbHost, const std::string& dbUser, const std::string& dbPassword, const std::string& dbName, int dbPort);

	bool executeQuery(const std::string& query);
	MySQLResultPtr getQueryResult();

	bool beginTransaction();
	bool commitTransaction();
	bool rollbackTransaction();

	std::string escapeString(const std::string& str);
	bool createTablesIfNotExist();

	// Player data operations
	bool saveAuthData(const std::unordered_map<std::string, AuthData>& authData);
	bool loadAuthData(std::unordered_map<std::string, AuthData>& authData, uint32_t& nextPlayerId);
	bool savePlayerPosition(uint32_t playerId, const Position& position);
	bool getPlayerIdByName(const std::string& username, uint32_t& playerId);

private:
	MYSQL* connection;
	std::string host;
	std::string user;
	std::string password;
	std::string database;
	int port;
	bool connected;

	// Logger reference
	Logger& logger;
};
