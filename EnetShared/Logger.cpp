#include "Logger.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#	include <Windows.h>
#	include <process.h>
#else
#	include <sys/syscall.h>
#	include <unistd.h>
#endif

#include "Constants.h"

// Initialize static members
std::unique_ptr<Logger> Logger::instance = nullptr;

Logger::Logger(LogLevel level)
      : minLevel(level), logFilePath(DEBUG_LOG_FILE)
{
	// Store main thread ID
#ifdef _WIN32
	mainThreadId = GetCurrentThreadId();
#else
	mainThreadId = static_cast<unsigned int>(syscall(SYS_gettid));
#endif

	logFile.open(logFilePath, std::ios::out | std::ios::app);
	if (!logFile.is_open() && logToFile)
	{
		std::cerr << "Warning: Could not open debug log file." << std::endl;
	}

	info("========== New Session Started ==========");

	// Log system information
	std::stringstream sysInfo;
	sysInfo << "Client Version: " << VERSION;

#ifdef _WIN32
	sysInfo << " | OS: Windows";
	MEMORYSTATUSEX memInfo;
	memInfo.dwLength = sizeof(MEMORYSTATUSEX);
	if (GlobalMemoryStatusEx(&memInfo))
	{
		sysInfo << " | RAM: " << (memInfo.ullTotalPhys / (1024 * 1024)) << " MB";
	}
#else
	sysInfo << " | OS: Unix/Linux";
	// Add Unix/Linux specific system info if needed
#endif

	info(sysInfo.str());
}

Logger::~Logger()
{
	if (logFile.is_open())
	{
		info("========== Session Ended ==========");
		logFile.close();
	}
}

Logger& Logger::getInstance()
{
	if (!instance)
	{
		instance = std::unique_ptr<Logger>(new Logger());
	}
	return *instance;
}

void Logger::setLogLevel(LogLevel level)
{
	minLevel = level;
	info("Log level set to: " + logLevelToString(level));
}

void Logger::setUseColors(bool enable)
{
	useColors = enable;
}

void Logger::setMaxFileSize(size_t bytes)
{
	maxFileSize = bytes;
}

void Logger::setMaxBackupFiles(int count)
{
	maxBackupFiles = count;
}

void Logger::setLogToConsole(bool enable)
{
	logToConsole = enable;
}

void Logger::setLogToFile(bool enable)
{
	logToFile = enable;

	if (enable && !logFile.is_open())
	{
		logFile.open(logFilePath, std::ios::out | std::ios::app);
	}
}

void Logger::setLogFilePath(const std::string& filePath)
{
	if (logFilePath == filePath)
	{
		return;
	}

	// Close existing file if open
	if (logFile.is_open())
	{
		logFile.close();
	}

	logFilePath = filePath;

	if (logToFile)
	{
		logFile.open(logFilePath, std::ios::out | std::ios::app);
		if (!logFile.is_open())
		{
			std::cerr << "Warning: Could not open log file: " << logFilePath << std::endl;
		}
	}
}

LogLevel Logger::getLogLevel() const
{
	return minLevel;
}

std::string Logger::getTimestamp()
{
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::system_clock::to_time_t(now);
	std::stringstream ss;
	tm localTime;

#ifdef _WIN32
	localtime_s(&localTime, &time);
#else
	localtime_r(&time, &localTime);
#endif

	ss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");

	// Add milliseconds
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
	ss << '.' << std::setfill('0') << std::setw(3) << ms.count();

	return ss.str();
}

std::string Logger::getLevelString(LogLevel level)
{
	return logLevelToString(level);
}

unsigned int Logger::getCurrentThreadId()
{
#ifdef _WIN32
	return GetCurrentThreadId();
#else
	return static_cast<unsigned int>(syscall(SYS_gettid));
#endif
}

void Logger::log(LogLevel level, const std::string& message, bool showOnConsole)
{
	if (level < minLevel)
		return;

	std::lock_guard<std::mutex> lock(logMutex);
	std::string timestamp = getTimestamp();
	std::string levelStr = getLevelString(level);
	unsigned int threadId = getCurrentThreadId();

	// Format the log entry
	std::stringstream entryStream;
	entryStream << "[" << timestamp << "] ";
	entryStream << "[" << levelStr << "] ";

	// Add thread info if not main thread
	if (threadId != mainThreadId)
	{
		entryStream << "[Thread:" << threadId << "] ";
	}

	entryStream << message;
	std::string logEntry = entryStream.str();

	// Write to file if enabled
	if (logToFile && logFile.is_open())
	{
		logFile << logEntry << std::endl;
		logFile.flush();
		// Check if we need to rotate log files
		checkFileSize();
	}

	// Write to console if needed
	// Write to console if needed
	if (logToConsole && (showOnConsole || level >= LogLevel::WARNING))
	{
		if (useColors)
		{
#ifdef _WIN32
			HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
			WORD originalAttributes;
			CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
			GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
			originalAttributes = consoleInfo.wAttributes;
			WORD textAttribute;

			switch (level)
			{
				case LogLevel::TRACE:
					textAttribute = FOREGROUND_BLUE | FOREGROUND_INTENSITY; // Bright Blue
					break;
				case LogLevel::DEBUG:
					textAttribute = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; // Cyan
					break;
				case LogLevel::INFO:
					textAttribute = FOREGROUND_GREEN | FOREGROUND_INTENSITY; // Bright Green
					break;
				case LogLevel::WARNING:
					textAttribute = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; // Yellow
					break;
				case LogLevel::ERR:
					textAttribute = FOREGROUND_RED | FOREGROUND_INTENSITY; // Bright Red
					break;
				case LogLevel::FATAL:
					textAttribute = FOREGROUND_RED | FOREGROUND_INTENSITY | BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE; // Bright Red on White
					break;
				default:
					textAttribute = originalAttributes; // Default
					break;
			}

			std::cout << "\r[" << timestamp << "] [";
			SetConsoleTextAttribute(hConsole, textAttribute);
			std::cout << levelStr;
			SetConsoleTextAttribute(hConsole, originalAttributes);
			std::cout << "] ";
			if (threadId != mainThreadId)
			{
				std::cout << "[Thread:" << threadId << "] ";
			}
			std::cout << message << std::endl;
#else
			// ANSI color codes
			std::string colorCode;

			switch (level)
			{
				case LogLevel::TRACE:
					colorCode = "\033[94m"; // Bright Blue
					break;
				case LogLevel::DEBUG:
					colorCode = "\033[96m"; // Cyan
					break;
				case LogLevel::INFO:
					colorCode = "\033[92m"; // Bright Green
					break;
				case LogLevel::WARNING:
					colorCode = "\033[93m"; // Yellow
					break;
				case LogLevel::ERR:
					colorCode = "\033[91m"; // Bright Red
					break;
				case LogLevel::FATAL:
					colorCode = "\033[97;41m"; // White on Red Background
					break;
				default:
					colorCode = "\033[0m"; // Reset/Normal
					break;
			}

			std::cout << "\r[" << timestamp << "] [" << colorCode << levelStr << "\033[0m] ";
			if (threadId != mainThreadId)
			{
				std::cout << "[Thread:" << threadId << "] ";
			}
			std::cout << message << std::endl;
#endif
		}
		else
		{
			std::cout << "\r" << logEntry << std::endl;
		}
	}
}

void Logger::trace(const std::string& message)
{
	log(LogLevel::TRACE, message);
}

void Logger::debug(const std::string& message)
{
	log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message)
{
	log(LogLevel::INFO, message);
}

void Logger::warning(const std::string& message)
{
	log(LogLevel::WARNING, message);
}

void Logger::error(const std::string& message)
{
	log(LogLevel::ERR, message, true);
}

void Logger::fatal(const std::string& message)
{
	log(LogLevel::FATAL, message, true);
}

void Logger::logNetworkEvent(const std::string& message)
{
	log(LogLevel::TRACE, "NETWORK: " + message);
}

void Logger::logDatabaseEvent(const std::string& message)
{
	log(LogLevel::INFO, "DATABASE: " + message);
}

void Logger::logSecurityEvent(const std::string& message)
{
	log(LogLevel::WARNING, "SECURITY: " + message);
}

void Logger::logPerformance(const std::string& message, long long duration)
{
	std::stringstream ss;
	ss << "PERFORMANCE: " << message << " - " << duration << "ms";
	log(LogLevel::DEBUG, ss.str());
}

void Logger::logWithContext(LogLevel level, const std::string& message, const std::string& file, int line, const std::string& function)
{
	if (level < minLevel)
		return;

	std::string contextInfo = file + ":" + std::to_string(line) + " " + function;
	std::string fullMessage = message + " (" + contextInfo + ")";

	log(level, fullMessage);
}

void Logger::checkFileSize()
{
	if (!logFile.is_open() || maxFileSize == 0)
		return;

	// Get current position as file size
	auto currentPos = logFile.tellp();
	if (currentPos < 0)
		return;

	if (static_cast<size_t>(currentPos) >= maxFileSize)
	{
		rotateLogFiles();
	}
}

void Logger::rotateLogFiles()
{
	// Close current file
	if (logFile.is_open())
	{
		logFile.close();
	}

	namespace fs = std::filesystem;

	try
	{
		// Remove oldest log file if we've reached the maximum number of files
		std::string oldestFile = logFilePath + "." + std::to_string(maxBackupFiles);
		if (fs::exists(oldestFile))
		{
			fs::remove(oldestFile);
		}

		// Shift all existing log files
		for (int i = maxBackupFiles - 1; i >= 1; --i)
		{
			std::string oldFile = logFilePath + "." + std::to_string(i);
			std::string newFile = logFilePath + "." + std::to_string(i + 1);
			if (fs::exists(oldFile))
			{
				fs::rename(oldFile, newFile);
			}
		}

		// Rename current log file
		if (fs::exists(logFilePath))
		{
			fs::rename(logFilePath, logFilePath + ".1");
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error during log rotation: " << e.what() << std::endl;
	}

	// Open new log file
	if (logToFile)
	{
		logFile.open(logFilePath, std::ios::out | std::ios::app);
		if (!logFile.is_open())
		{
			std::cerr << "Warning: Could not open new log file after rotation: " << logFilePath << std::endl;
		}
	}
}

std::string Logger::logLevelToString(LogLevel level)
{
	switch (level)
	{
		case LogLevel::TRACE:
			return "TRACE";
		case LogLevel::DEBUG:
			return "DEBUG";
		case LogLevel::INFO:
			return "INFO";
		case LogLevel::WARNING:
			return "WARNING";
		case LogLevel::ERR:
			return "ERROR";
		case LogLevel::FATAL:
			return "FATAL";
		case LogLevel::OFF:
			return "OFF";
		default:
			return "UNKNOWN";
	}
}

LogLevel Logger::stringToLogLevel(const std::string& levelStr)
{
	std::string upperLevelStr = levelStr;
	std::transform(upperLevelStr.begin(), upperLevelStr.end(), upperLevelStr.begin(), ::toupper);

	if (upperLevelStr == "TRACE")
		return LogLevel::TRACE;
	if (upperLevelStr == "DEBUG")
		return LogLevel::DEBUG;
	if (upperLevelStr == "INFO")
		return LogLevel::INFO;
	if (upperLevelStr == "WARNING")
		return LogLevel::WARNING;
	if (upperLevelStr == "ERROR")
		return LogLevel::ERR;
	if (upperLevelStr == "FATAL")
		return LogLevel::FATAL;
	if (upperLevelStr == "OFF")
		return LogLevel::OFF;

	return LogLevel::INFO; // Default
}

// Performance timer implementation
Logger::ScopedTimer::ScopedTimer(Logger& logger, const std::string& operation)
      : logger(logger), operationName(operation), startTime(std::chrono::steady_clock::now())
{
}

Logger::ScopedTimer::~ScopedTimer()
{
	auto endTime = std::chrono::steady_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
	logger.logPerformance(operationName, duration);
}
