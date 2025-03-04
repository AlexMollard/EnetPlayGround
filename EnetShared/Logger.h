#pragma once

#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Using ERR instead of ERROR to avoid conflict with Windows.h
enum class LogLevel
{
	TRACE,   // Most detailed information for tracing
	DEBUG,   // Debugging information
	INFO,    // General information
	WARNING, // Warnings
	ERR,     // Errors that can be recovered from
	FATAL,   // Critical errors
	OFF      // Logging disabled
};

class Logger
{
private:
	std::ofstream logFile;
	std::mutex logMutex;
	bool useColors = true;
	LogLevel minLevel = LogLevel::DEBUG;
	size_t maxFileSize = 10 * 1024 * 1024; // 10 MB default
	int maxBackupFiles = 3;

	std::string logFilePath;
	bool logToConsole = true;
	bool logToFile = true;

	// Thread ID tracking
	unsigned int mainThreadId;

	// Private helper methods
	std::string getTimestamp();
	std::string getLevelString(LogLevel level);
	unsigned int getCurrentThreadId();
	void checkFileSize();
	void rotateLogFiles();

	// Singleton pattern (optional)
	static std::unique_ptr<Logger> instance;

public:
	Logger();
	~Logger();

	// Singleton accessor (optional)
	static Logger& getInstance();

	// Configure logger
	void setLogLevel(LogLevel level);
	void setUseColors(bool enable);
	void setMaxFileSize(size_t bytes);
	void setMaxBackupFiles(int count);
	void setLogToConsole(bool enable);
	void setLogToFile(bool enable);
	void setLogFilePath(const std::string& filePath);

	// Get current logger state
	LogLevel getLogLevel() const;

	// Core logging methods
	void log(LogLevel level, const std::string& message, bool showOnConsole = true);

	// Log level specific methods
	void trace(const std::string& message);
	void debug(const std::string& message);
	void info(const std::string& message);
	void warning(const std::string& message);
	void error(const std::string& message);
	void fatal(const std::string& message);

	void logNetworkEvent(const std::string& message);

	// Additional category methods
	void logDatabaseEvent(const std::string& message);
	void logSecurityEvent(const std::string& message);
	void logPerformance(const std::string& message, long long duration);

	// Log with context
	void logWithContext(LogLevel level, const std::string& message, const std::string& file, int line, const std::string& function);

	// Performance tracking
	class ScopedTimer
	{
	private:
		Logger& logger;
		std::string operationName;
		std::chrono::steady_clock::time_point startTime;

	public:
		ScopedTimer(Logger& logger, const std::string& operation);
		~ScopedTimer();
	};

	// Static utility methods
	static std::string logLevelToString(LogLevel level);
	static LogLevel stringToLogLevel(const std::string& levelStr);
};

// Helper macros for context information
#define LOG_TRACE(logger, message) logger.logWithContext(LogLevel::TRACE, message, __FILE__, __LINE__, __FUNCTION__)
#define LOG_DEBUG(logger, message) logger.logWithContext(LogLevel::DEBUG, message, __FILE__, __LINE__, __FUNCTION__)
#define LOG_INFO(logger, message) logger.logWithContext(LogLevel::INFO, message, __FILE__, __LINE__, __FUNCTION__)
#define LOG_WARNING(logger, message) logger.logWithContext(LogLevel::WARNING, message, __FILE__, __LINE__, __FUNCTION__)
#define LOG_ERROR(logger, message) logger.logWithContext(LogLevel::ERR, message, __FILE__, __LINE__, __FUNCTION__)
#define LOG_FATAL(logger, message) logger.logWithContext(LogLevel::FATAL, message, __FILE__, __LINE__, __FUNCTION__)

// Performance tracking macro
#define LOG_PERF_SCOPE(logger, operation) Logger::ScopedTimer scopedTimer(logger, operation)
