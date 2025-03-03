#pragma once

#include <fstream>
#include <mutex>

class Logger
{
private:
	std::ofstream logFile;
	std::mutex logMutex;
	bool debugMode = false;

public:
	Logger(bool enableDebug = false);
	~Logger();

	void setDebugMode(bool enable);

	std::string getTimestamp();

	void log(const std::string& message, bool showOnConsole = false);
	void logError(const std::string& message);
	void logNetworkEvent(const std::string& message);
};
