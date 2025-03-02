#pragma once

#include "Constants.h"

class Logger
{
private:
	std::ofstream logFile;
	std::mutex logMutex;
	bool debugMode = false;

public:
	Logger(bool enableDebug = false)
	      : debugMode(enableDebug)
	{
		logFile.open(DEBUG_LOG_FILE, std::ios::out | std::ios::app);
		if (!logFile.is_open())
		{
			std::cerr << "Warning: Could not open debug log file." << std::endl;
		}

		log("========== New Session Started ==========");
		log("Client Version: " + std::string(CLIENT_VERSION));
	}

	~Logger()
	{
		if (logFile.is_open())
		{
			log("========== Session Ended ==========");
			logFile.close();
		}
	}

	void setDebugMode(bool enable)
	{
		debugMode = enable;
		log("Debug mode " + std::string(enable ? "enabled" : "disabled"));
	}

	std::string getTimestamp()
	{
		auto now = std::chrono::system_clock::now();
		auto time = std::chrono::system_clock::to_time_t(now);
		std::stringstream ss;
		tm localTime;
		localtime_s(&localTime, &time);
		ss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
		return ss.str();
	}

	void log(const std::string& message, bool showOnConsole = false)
	{
		std::lock_guard<std::mutex> lock(logMutex);

		std::string timestamp = getTimestamp();
		std::string logEntry = "[" + timestamp + "] " + message;

		if (logFile.is_open())
		{
			logFile << logEntry << std::endl;
			logFile.flush();
		}

		if (debugMode || showOnConsole)
		{
			std::cout << "\r" << logEntry << std::endl;
		}
	}

	void logError(const std::string& message)
	{
		log("ERROR: " + message, true);
	}

	void logNetworkEvent(const std::string& message)
	{
		log("NETWORK: " + message);
	}
};
