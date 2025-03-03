#include "Logger.h"

#include <iostream>
#include <chrono>

#include "Constants.h"

Logger::Logger(bool enableDebug)
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

Logger::~Logger()
{
	if (logFile.is_open())
	{
		log("========== Session Ended ==========");
		logFile.close();
	}
}

void Logger::setDebugMode(bool enable)
{
	debugMode = enable;
	log("Debug mode " + std::string(enable ? "enabled" : "disabled"));
}

std::string Logger::getTimestamp()
{
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::system_clock::to_time_t(now);
	std::stringstream ss;
	tm localTime;
	localtime_s(&localTime, &time);
	ss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
	return ss.str();
}

void Logger::log(const std::string& message, bool showOnConsole)
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

void Logger::logError(const std::string& message)
{
	log("ERROR: " + message, true);
}

void Logger::logNetworkEvent(const std::string& message)
{
	log("NETWORK: " + message);
}
