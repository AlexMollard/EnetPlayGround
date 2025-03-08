#pragma once

#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "StackTrace.h"

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

// Structure to hold error context information
struct ErrorContext
{
	std::optional<int> errorCode;
	std::optional<std::string> category;
	std::optional<std::string> component;
	std::optional<std::string> details;

	// Format the error context as a string
	std::string toString() const;
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
	bool autoStackTraceForErrors = true;
	int stackTraceMaxFrames = 32;

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

	static std::string currentInputLine;
	static std::mutex inputMutex;
	static bool inputActive;

public:
	Logger();
	~Logger();

	// Singleton accessor (optional)
	static Logger& getInstance();

	// Console input protection
	static void setConsoleInputLine(const std::string& input);
	static void clearConsoleInputLine();

	// Configure logger
	void setLogLevel(LogLevel level);
	void setUseColors(bool enable);
	void setMaxFileSize(size_t bytes);
	void setMaxBackupFiles(int count);
	void setLogToConsole(bool enable);
	void setLogToFile(bool enable);
	void setLogFilePath(const std::string& filePath);
	void setAutoStackTraceForErrors(bool enable);
	void setStackTraceMaxFrames(int frames);

	// Get current logger state
	LogLevel getLogLevel() const;
	bool getAutoStackTraceForErrors() const;
	int getStackTraceMaxFrames() const;

	// Core logging methods
	void log(LogLevel level, const std::string& message, bool showOnConsole = true);

	// Log level specific methods
	void trace(const std::string& message);
	void debug(const std::string& message);
	void info(const std::string& message);
	void warning(const std::string& message);
	void error(const std::string& message);
	void fatal(const std::string& message);

	// Enhanced error logging with context
	void error(const std::string& message, const ErrorContext& context);
	void fatal(const std::string& message, const ErrorContext& context);

	// Category methods
	void logNetworkEvent(const std::string& message);
	void logDatabaseEvent(const std::string& message);
	void logSecurityEvent(const std::string& message);
	void logPerformance(const std::string& message, long long duration);

	// Log with context
	void logWithContext(LogLevel level, const std::string& message, const std::string& file, int line, const std::string& function);

	// Stack trace logging
	void logWithStackTrace(const char* message);
	void logWithStackTrace(const std::string& message, const std::source_location& location = std::source_location::current());
	void logWithStackTrace(LogLevel level, const std::string& message, const std::source_location& location = std::source_location::current());

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

// Enhanced error macros with context
#define LOG_ERROR_WITH_CONTEXT(logger, message, context)                                                       \
	do                                                                                                         \
	{                                                                                                          \
		const auto loc = std::source_location::current();                                                      \
		std::stringstream ss;                                                                                  \
		ss << message << " [" << loc.file_name() << ":" << loc.line() << " in " << loc.function_name() << "]"; \
		logger.error(ss.str(), context);                                                                       \
	}                                                                                                          \
	while (0)

#define LOG_FATAL_WITH_CONTEXT(logger, message, context)                                                       \
	do                                                                                                         \
	{                                                                                                          \
		const auto loc = std::source_location::current();                                                      \
		std::stringstream ss;                                                                                  \
		ss << message << " [" << loc.file_name() << ":" << loc.line() << " in " << loc.function_name() << "]"; \
		logger.fatal(ss.str(), context);                                                                       \
	}                                                                                                          \
	while (0)

// Callstack log macros
#define LOG_STACK_TRACE(logger, level, message) logger.logWithStackTrace(level, message, std::source_location::current())
#define LOG_STACK_TRACE_TRACE(logger, message) logger.logWithStackTrace(LogLevel::TRACE, message, std::source_location::current())
#define LOG_STACK_TRACE_DEBUG(logger, message) logger.logWithStackTrace(LogLevel::DEBUG, message, std::source_location::current())
#define LOG_STACK_TRACE_INFO(logger, message) logger.logWithStackTrace(LogLevel::INFO, message, std::source_location::current())
#define LOG_STACK_TRACE_WARNING(logger, message) logger.logWithStackTrace(LogLevel::WARNING, message, std::source_location::current())
#define LOG_STACK_TRACE_ERROR(logger, message) logger.logWithStackTrace(LogLevel::ERR, message, std::source_location::current())
#define LOG_STACK_TRACE_FATAL(logger, message) logger.logWithStackTrace(LogLevel::FATAL, message, std::source_location::current())

// Performance tracking macro
#define LOG_PERF_SCOPE(logger, operation) Logger::ScopedTimer scopedTimer(logger, operation)

// Error context creation helper
#define MAKE_ERROR_CONTEXT(...) \
	ErrorContext                \
	{                           \
		__VA_ARGS__             \
	}
