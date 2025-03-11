#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Logger.h"

class ThreadPool
{
public:
	// Create a thread pool with specified threads, logger, and debug mode
	explicit ThreadPool(int threads = std::thread::hardware_concurrency(), Logger& logger = Logger::getInstance(), bool debugMode = false)
	      : stop(false), activeThreads(0), logger(logger), debugMode(debugMode)
	{
		logger.info("Creating pool with " + std::to_string(threads) + " threads" + (debugMode ? " (debug mode)" : ""));

		for (int i = 0; i < threads; ++i)
		{
			workers.emplace_back([this, id = i] { workerThread(id); });
		}
	}

	~ThreadPool()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			stop = true;
		}
		condition.notify_all();
		for (auto& t: workers)
		{
			if (t.joinable())
				t.join();
		}

		logger.info("Thread pool destroyed");
		if (debugMode)
		{
			printResourceStats();
		}
	}

	// Enable or disable debug mode
	void setDebugMode(bool enable)
	{
		debugMode = enable;
		logger.info(std::string("Debug mode ") + (enable ? "enabled" : "disabled"));
	}

	bool isDebugMode() const
	{
		return debugMode;
	}

	// Normal task submission (no resources)
	template<typename F, typename... Args>
	auto run(F&& f, Args&&... args)
	{
		auto task = [f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...)]() mutable { return std::apply(f, args); };

		using RetType = decltype(task());
		auto package = std::make_shared<std::packaged_task<RetType()>>(std::move(task));
		auto future = package->get_future();

		std::string taskId = "T" + std::to_string(nextTaskId++);

		{
			std::lock_guard<std::mutex> lock(mutex);
			if (debugMode)
			{
				logger.trace(taskId + ": Queued (no resources)");
			}

			tasks.emplace(
			        [this, package, taskId]
			        {
				        if (debugMode)
					        LOG_PERF_SCOPE(logger, taskId);
				        if (debugMode)
					        logger.debug(taskId + ": Started");

				        try
				        {
					        (*package)();
				        }
				        catch (const std::exception& e)
				        {
					        LOG_ERROR_WITH_CONTEXT(logger, taskId + ": Exception", MAKE_ERROR_CONTEXT(.component = "ThreadPool", .details = e.what()));
				        }
				        catch (...)
				        {
					        logger.error(taskId + ": Unknown exception");
				        }

				        if (debugMode)
					        logger.debug(taskId + ": Completed");
			        });
		}
		condition.notify_one();
		return future;
	}

	// Read from a resource (shared access)
	template<typename R, typename F, typename... Args>
	auto read(std::string name, F&& f, Args&&... args)
	{
		auto task = [f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...)]() mutable { return std::apply(f, args); };

		using RetType = decltype(task());
		auto package = std::make_shared<std::packaged_task<RetType()>>(std::move(task));
		auto future = package->get_future();

		auto resourceKey = makeKey<R>(name);
		std::string taskId = "T" + std::to_string(nextTaskId++);
		std::string resourceName;

		if (debugMode)
		{
			resourceName = typeName<R>() + ":" + name;
			logger.trace(taskId + ": Queued (read " + resourceName + ")");
		}

		{
			std::lock_guard<std::mutex> lock(mutex);
			tasks.emplace(
			        [this, resourceKey, package, resourceName, taskId]
			        {
				        if (debugMode)
					        LOG_PERF_SCOPE(logger, taskId);
				        if (debugMode)
					        logger.debug(taskId + ": Started");

				        auto& mutex = getMutex(resourceKey);

				        if (debugMode)
				        {
					        recordResourceUsage(resourceName, false);
					        logger.trace(taskId + ": Waiting for shared lock on " + resourceName);
				        }

				        auto lockStart = std::chrono::steady_clock::now();
				        std::shared_lock<std::shared_mutex> lock(mutex);

				        if (debugMode)
				        {
					        auto lockTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lockStart).count();
					        if (lockTime > 5)
					        {
						        logger.debug(taskId + ": Waited " + std::to_string(lockTime) + "ms for lock on " + resourceName);
					        }
				        }

				        try
				        {
					        (*package)();
				        }
				        catch (const std::exception& e)
				        {
					        ErrorContext context;
					        context.component = "ThreadPool";
					        if (debugMode)
						        context.details = "Resource: " + resourceName;
					        LOG_ERROR_WITH_CONTEXT(logger, taskId + ": Exception", context);
					        if (debugMode)
						        LOG_STACK_TRACE_ERROR(logger, e.what());
				        }
				        catch (...)
				        {
					        logger.error(taskId + ": Unknown exception");
					        if (debugMode)
						        LOG_STACK_TRACE_ERROR(logger, "Unknown exception");
				        }

				        if (debugMode)
					        logger.debug(taskId + ": Completed");
			        });
		}
		condition.notify_one();
		return future;
	}

	// Write to a resource (exclusive access)
	template<typename R, typename F, typename... Args>
	auto write(std::string name, F&& f, Args&&... args)
	{
		auto task = [f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...)]() mutable { return std::apply(f, args); };

		using RetType = decltype(task());
		auto package = std::make_shared<std::packaged_task<RetType()>>(std::move(task));
		auto future = package->get_future();

		auto resourceKey = makeKey<R>(name);
		std::string taskId = "T" + std::to_string(nextTaskId++);
		std::string resourceName;

		if (debugMode)
		{
			resourceName = typeName<R>() + ":" + name;
			logger.trace(taskId + ": Queued (write " + resourceName + ")");
		}

		{
			std::lock_guard<std::mutex> lock(mutex);
			tasks.emplace(
			        [this, resourceKey, package, resourceName, taskId]
			        {
				        if (debugMode)
					        LOG_PERF_SCOPE(logger, taskId);
				        if (debugMode)
					        logger.debug(taskId + ": Started");

				        auto& mutex = getMutex(resourceKey);

				        if (debugMode)
				        {
					        recordResourceUsage(resourceName, true);
					        logger.trace(taskId + ": Waiting for exclusive lock on " + resourceName);
				        }

				        auto lockStart = std::chrono::steady_clock::now();
				        std::unique_lock<std::shared_mutex> lock(mutex);

				        if (debugMode)
				        {
					        auto lockTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lockStart).count();
					        if (lockTime > 5)
					        {
						        logger.debug(taskId + ": Waited " + std::to_string(lockTime) + "ms for lock on " + resourceName);
					        }
				        }

				        try
				        {
					        (*package)();
				        }
				        catch (const std::exception& e)
				        {
					        ErrorContext context;
					        context.component = "ThreadPool";
					        if (debugMode)
						        context.details = "Resource: " + resourceName + " (write)";
					        LOG_ERROR_WITH_CONTEXT(logger, taskId + ": Exception", context);
					        if (debugMode)
						        LOG_STACK_TRACE_ERROR(logger, e.what());
				        }
				        catch (...)
				        {
					        logger.error(taskId + ": Unknown exception");
					        if (debugMode)
						        LOG_STACK_TRACE_ERROR(logger, "Unknown exception");
				        }

				        if (debugMode)
					        logger.debug(taskId + ": Completed");
			        });
		}
		condition.notify_one();
		return future;
	}

	// Multi-resource operations
	template<typename... Rs>
	class Resources
	{
	public:
		Resources(std::vector<std::string> names)
		      : names(std::move(names))
		{
		}

		template<typename F>
		auto read(ThreadPool& pool, F&& f, const std::string& description = "")
		{
			// Get the return type of the lambda
			using RetType = decltype(f());

			auto task = [f = std::forward<F>(f)]() mutable -> RetType
			{
				return f();
			};

			// Create a packaged task with the correct return type
			auto package = std::make_shared<std::packaged_task<RetType()>>(std::move(task));
			auto future = package->get_future();

			std::string taskId = "T" + std::to_string(pool.nextTaskId++);
			bool isDebug = pool.debugMode;
			std::string resourceNames;

			if (isDebug)
			{
				resourceNames = pool.formatResourceNames<Rs...>(names);
				LOG_TRACE(pool.logger, taskId + ": Queued (read multiple: " + resourceNames + ")");
			}

			{
				std::lock_guard<std::mutex> lock(pool.mutex);
				pool.tasks.emplace(
				        [&pool, names = this->names, package, taskId, resourceNames, isDebug]
				        {
					        if (isDebug)
						        LOG_PERF_SCOPE(pool.logger, taskId);
					        if (isDebug)
						        LOG_DEBUG(pool.logger, taskId + ": Started");

					        std::vector<std::shared_lock<std::shared_mutex>> locks;
					        std::vector<std::size_t> keys;

					        // Collect and sort keys
					        [&]<typename... Types>(std::type_identity<std::tuple<Types...>>)
					        {
						        size_t i = 0;
						        ((i < names.size() ? keys.push_back(typeidToKey<Types>(i, names[i])) : void(), ++i), ...);
					        }(std::type_identity<std::tuple<Rs...>>{});

					        // Sort the keys as you were doing before
					        std::sort(keys.begin(), keys.end());

					        if (isDebug)
					        {
						        LOG_TRACE(pool.logger, taskId + ": Acquiring " + std::to_string(keys.size()) + " shared locks");
					        }

					        try
					        {
						        // Lock all resources
						        for (const auto& key: keys)
						        {
							        locks.emplace_back(pool.getMutex(key));
						        }

						        (*package)();
					        }
					        catch (const std::exception& e)
					        {
						        ErrorContext context;
						        context.component = "ThreadPool";
						        if (isDebug)
							        context.details = "Multi-resource operation (read): " + resourceNames;
						        LOG_ERROR_WITH_CONTEXT(pool.logger, taskId + ": Exception", context);
						        if (isDebug)
							        LOG_STACK_TRACE_ERROR(pool.logger, e.what());
					        }
					        catch (...)
					        {
						        LOG_ERROR(pool.logger, taskId + ": Unknown exception");
						        if (isDebug)
							        LOG_STACK_TRACE_ERROR(pool.logger, "Unknown exception in multi-resource read");
					        }

					        if (isDebug)
						        LOG_DEBUG(pool.logger, taskId + ": Completed");
				        });
			}
			pool.condition.notify_one();
			return future;
		}

		template<typename F>
		auto write(ThreadPool& pool, F&& f, const std::string& description = "")
		{
			// Get the return type of the lambda
			using RetType = decltype(f());

			auto task = [f = std::forward<F>(f)]() mutable -> RetType
			{
				return f();
			};

			// Create a packaged task with the correct return type
			auto package = std::make_shared<std::packaged_task<RetType()>>(std::move(task));
			auto future = package->get_future();

			std::string taskId = "T" + std::to_string(pool.nextTaskId++);
			bool isDebug = pool.debugMode;
			std::string resourceNames;

			if (isDebug)
			{
				resourceNames = pool.formatResourceNames<Rs...>(names);
				LOG_TRACE(pool.logger, taskId + ": Queued (write multiple: " + resourceNames + ")");
			}

			{
				std::lock_guard<std::mutex> lock(pool.mutex);
				pool.tasks.emplace(
				        [&pool, names = this->names, package, taskId, resourceNames, isDebug]
				        {
					        if (isDebug)
						        LOG_PERF_SCOPE(pool.logger, taskId);
					        if (isDebug)
						        LOG_DEBUG(pool.logger, taskId + ": Started");

					        std::vector<std::unique_lock<std::shared_mutex>> locks;
					        std::vector<std::size_t> keys;

					        // Collect and sort keys
					        [&]<typename... Types>(std::type_identity<std::tuple<Types...>>)
					        {
						        size_t i = 0;
						        ((i < names.size() ? keys.push_back(typeidToKey<Types>(i, names[i])) : void(), ++i), ...);
					        }(std::type_identity<std::tuple<Rs...>>{});

					        // Sort the keys as you were doing before
					        std::sort(keys.begin(), keys.end());

					        if (isDebug)
					        {
						        LOG_TRACE(pool.logger, taskId + ": Acquiring " + std::to_string(keys.size()) + " exclusive locks");
					        }

					        try
					        {
						        // Lock all resources
						        for (const auto& key: keys)
						        {
							        locks.emplace_back(pool.getMutex(key));
						        }

						        (*package)();
					        }
					        catch (const std::exception& e)
					        {
						        ErrorContext context;
						        context.component = "ThreadPool";
						        if (isDebug)
							        context.details = "Multi-resource operation (write): " + resourceNames;
						        LOG_ERROR_WITH_CONTEXT(pool.logger, taskId + ": Exception", context);
						        if (isDebug)
							        LOG_STACK_TRACE_ERROR(pool.logger, e.what());
					        }
					        catch (...)
					        {
						        LOG_ERROR(pool.logger, taskId + ": Unknown exception");
						        if (isDebug)
							        LOG_STACK_TRACE_ERROR(pool.logger, "Unknown exception in multi-resource write");
					        }

					        if (isDebug)
						        LOG_DEBUG(pool.logger, taskId + ": Completed");
				        });
			}
			pool.condition.notify_one();
			return future;
		}

	private:
		template<typename R>
		static std::size_t typeidToKey(size_t index, const std::string& name)
		{
			return std::hash<std::string>()(name) ^ std::hash<std::string>()(typeid(R).name()) ^ std::hash<std::size_t>()(index);
		}

		std::vector<std::string> names;
	};

	template<typename... Rs>
	static Resources<Rs...> resources(std::initializer_list<std::string> names)
	{
		return Resources<Rs...>(std::vector<std::string>(names));
	}

	// Wait for all tasks to complete
	void wait()
	{
		if (debugMode)
			logger.debug("Waiting for all tasks to complete");

		std::unique_lock<std::mutex> lock(mutex);
		done.wait(lock, [this] { return tasks.empty() && activeThreads == 0; });

		logger.info("All tasks completed");
	}

	// Get statistics
	std::string getStats() const
	{
		std::stringstream ss;

		ss << "Thread Pool Statistics:\n";
		ss << "  Threads: " << workers.size() << "\n";
		ss << "  Active threads: " << activeThreads << "\n";
		ss << "  Queued tasks: " << tasks.size() << "\n";
		ss << "  Completed tasks: " << completedTasks << "\n";

		if (debugMode)
		{
			std::lock_guard<std::mutex> lock(statsMutex);
			if (!resourceStats.empty())
			{
				ss << "Resource Statistics:\n";
				for (const auto& [resource, stats]: resourceStats)
				{
					auto reads = stats.readAccesses.load();
					auto writes = stats.writeAccesses.load();
					ss << "  " << resource << ": " << reads << " reads, " << writes << " writes, " << (reads + writes) << " total\n";
				}
			}
		}

		return ss.str();
	}

private:
	// Generate a unique key for a resource
	template<typename R>
	static std::size_t makeKey(const std::string& name)
	{
		return std::hash<std::string>()(name) ^ std::hash<std::string>()(typeid(R).name());
	}

	// Get type name for debugging
	template<typename R>
	static std::string typeName()
	{
		return typeid(R).name();
	}

	// Format resource names for a parameter pack
	template<typename... Rs>
	std::string formatResourceNames(const std::vector<std::string>& names)
	{
		std::string result;
		formatResourceNamesImpl<0, Rs...>(names, result);
		return result;
	}

	// Helper for formatResourceNames
	template<size_t I, typename R, typename... Rest>
	void formatResourceNamesImpl(const std::vector<std::string>& names, std::string& result)
	{
		if (I < names.size())
		{
			if (!result.empty())
				result += ", ";
			result += typeName<R>() + ":" + names[I];
		}
		if constexpr (sizeof...(Rest) > 0)
		{
			formatResourceNamesImpl<I + 1, Rest...>(names, result);
		}
	}

	// End of recursion for formatResourceNames
	template<size_t I>
	void formatResourceNamesImpl(const std::vector<std::string>&, std::string&)
	{
	}

	// Get or create a mutex for a resource
	std::shared_mutex& getMutex(std::size_t key)
	{
		auto it = mutexes.find(key);
		if (it == mutexes.end())
		{
			auto [iter, _] = mutexes.emplace(key, std::make_unique<std::shared_mutex>());
			return *iter->second;
		}
		return *it->second;
	}

	// Resource stats tracking
	struct ResourceStats
	{
		std::atomic<int> readAccesses{ 0 };
		std::atomic<int> writeAccesses{ 0 };
	};

	// Record resource usage for statistics
	void recordResourceUsage(const std::string& resourceName, bool isWrite)
	{
		if (!debugMode)
			return;

		std::lock_guard<std::mutex> lock(statsMutex);
		auto& stats = resourceStats[resourceName];
		isWrite ? stats.writeAccesses++ : stats.readAccesses++;
	}

	// Print resource statistics
	void printResourceStats() const
	{
		std::lock_guard<std::mutex> lock(statsMutex);
		if (resourceStats.empty())
			return;

		logger.info("Resource Usage Statistics:");
		for (const auto& [resource, stats]: resourceStats)
		{
			auto reads = stats.readAccesses.load();
			auto writes = stats.writeAccesses.load();
			logger.info("  " + resource + ": " + std::to_string(reads) + " reads, " + std::to_string(writes) + " writes, " + std::to_string(reads + writes) + " total");
		}
	}

	// Worker thread main loop
	void workerThread(int id)
	{
		std::string threadName = "Worker-" + std::to_string(id);
		logger.info(threadName + " started");

		while (true)
		{
			std::function<void()> task;

			{
				std::unique_lock<std::mutex> lock(mutex);
				condition.wait(lock, [this] { return stop || !tasks.empty(); });

				if (stop && tasks.empty())
				{
					logger.info(threadName + " shutting down");
					return;
				}

				task = std::move(tasks.front());
				tasks.pop();
			}

			activeThreads++;

			try
			{
				task();
			}
			catch (const std::exception& e)
			{
				LOG_ERROR_WITH_CONTEXT(logger, threadName + ": Caught exception", MAKE_ERROR_CONTEXT(.details = e.what()));
				if (debugMode)
					LOG_STACK_TRACE_ERROR(logger, "Exception in worker thread");
			}
			catch (...)
			{
				logger.error(threadName + ": Caught unknown exception");
				if (debugMode)
					LOG_STACK_TRACE_ERROR(logger, "Unknown exception in worker thread");
			}

			activeThreads--;
			completedTasks++;

			if (tasks.empty() && activeThreads == 0)
			{
				done.notify_all();
			}
		}
	}

	std::vector<std::thread> workers;
	std::queue<std::function<void()>> tasks;
	std::mutex mutex;
	std::condition_variable condition;
	std::condition_variable done;
	bool stop;
	std::atomic<int> activeThreads{ 0 };
	std::atomic<int> completedTasks{ 0 };
	std::atomic<int> nextTaskId{ 1 };
	std::atomic<bool> debugMode;

	std::unordered_map<std::size_t, std::unique_ptr<std::shared_mutex>> mutexes;
	mutable std::mutex statsMutex;
	std::unordered_map<std::string, ResourceStats> resourceStats;

	Logger& logger;
};
