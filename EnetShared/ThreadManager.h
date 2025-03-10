#pragma once

// Debug macro - define this to disable multithreading and run all tasks synchronously
// #define THREAD_MANAGER_DEBUG

#include <any>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>
#include <Windows.h>

#include "ThreadPool/thread_pool.h"

/**
 * Resource identifier struct - used to identify resources for synchronization
 */
struct ResourceId
{
	std::string name;
	std::type_index type;

	ResourceId(const std::string& resourceName, const std::type_index& resourceType)
	      : name(resourceName), type(resourceType)
	{
	}

	bool operator==(const ResourceId& other) const
	{
		return name == other.name && type == other.type;
	}
};

// Hash function for ResourceId
namespace std
{
	template<>
	struct hash<ResourceId>
	{
		size_t operator()(const ResourceId& id) const
		{
			return hash<string>()(id.name) ^ hash<type_index>()(id.type);
		}
	};
} // namespace std

/**
 * A wrapper around dp::thread_pool to manage threading with resource-based synchronization
 */
class ThreadManager
{
public:
	/**
     * Constructor creates a thread pool with the specified number of threads
     * @param numThreads Number of worker threads (default = hardware concurrency - 1)
     */
	ThreadManager(size_t numThreads = max(1u, 4))
#ifndef THREAD_MANAGER_DEBUG
	      : pool((unsigned int) numThreads), numThreads(numThreads)
	{
		// Name all threads for easier debugging
		pool.name_all_threads("ThreadManager");
	}
#else
	      : numThreads(1) // In debug mode, we use just one thread (the calling thread)
	{
		// No thread pool to initialize in debug mode
	}
#endif

	/**
     * Get the number of threads in the pool
     * @return Number of threads
     */
	size_t getThreadCount() const
	{
		return numThreads;
	}

	/**
     * Schedule a task without caring about the result
     * @param func The function to execute
     * @param args The arguments to pass to the function
     */
	template<typename Func, typename... Args>
	void scheduleTask(Func&& func, Args&&... args)
	{
#ifndef THREAD_MANAGER_DEBUG
		pool.enqueue_detach(std::forward<Func>(func), std::forward<Args>(args)...);
#else
		// In debug mode, execute the task directly in the current thread
		std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
#endif

		// Update stats
		std::lock_guard<std::mutex> lock(statsMutex);
		tasksSubmitted++;
	}

	/**
     * Schedule a task and get a future for its result
     * @param func The function to execute
     * @param args The arguments to pass to the function
     * @return A future for the function's return value
     */
	template<typename Func, typename... Args>
	auto scheduleTaskWithResult(Func&& func, Args&&... args)
	{
#ifndef THREAD_MANAGER_DEBUG
		auto future = pool.enqueue(std::forward<Func>(func), std::forward<Args>(args)...);
#else
		// In debug mode, execute the task directly and return a pre-completed future
		auto future = std::async(std::launch::deferred, std::forward<Func>(func), std::forward<Args>(args)...);
#endif

		// Update stats
		std::lock_guard<std::mutex> lock(statsMutex);
		tasksSubmitted++;

		return future;
	}

	/**
     * Schedule a UI-related task
     * @param func The function to execute
     * @param args The arguments to pass to the function
     */
	template<typename Func, typename... Args>
	void scheduleUITask(Func&& func, Args&&... args)
	{
#ifndef THREAD_MANAGER_DEBUG
		// Use a specific tag for UI tasks
		std::lock_guard<std::mutex> lock(uiMutex);
		pool.enqueue_detach(std::forward<Func>(func), std::forward<Args>(args)...);
#else
		// In debug mode, execute the task directly in the current thread
		std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
#endif

		// Update stats
		std::lock_guard<std::mutex> statsLock(statsMutex);
		tasksSubmitted++;
		uiTasksSubmitted++;
	}

	/**
     * Schedule a network-related task
     * @param func The function to execute
     * @param args The arguments to pass to the function
     */
	template<typename Func, typename... Args>
	void scheduleNetworkTask(Func&& func, Args&&... args)
	{
#ifndef THREAD_MANAGER_DEBUG
		pool.enqueue_detach(std::forward<Func>(func), std::forward<Args>(args)...);
#else
		// In debug mode, execute the task directly in the current thread
		std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
#endif

		// Update stats
		std::lock_guard<std::mutex> lock(statsMutex);
		tasksSubmitted++;
		networkTasksSubmitted++;
	}

	/**
     * Schedule a task that accesses specific resources, ensuring synchronized access
     * @param resources List of resource identifiers this task will access
     * @param func The function to execute
     * @param args The arguments to pass to the function
     */
	template<typename Func>
	void scheduleResourceTask(const std::vector<ResourceId>& resources, Func&& func)
	{
#ifndef THREAD_MANAGER_DEBUG
		// Create a task wrapper that will handle resource locking
		// Note: use std::function with a shared_ptr to wrap the callable
		auto sharedFunc = std::make_shared<std::decay_t<Func>>(std::forward<Func>(func));

		auto taskWrapper = [this, resources, funcPtr = std::move(sharedFunc)]() mutable
		{
			// Collect mutex pointers first
			std::vector<std::shared_ptr<std::shared_mutex>> mutexPtrs;
			{
				std::lock_guard<std::mutex> guard(resourceMutex);

				// Sort resources by name and type to ensure consistent lock order
				std::vector<ResourceId> sortedResources = resources;
				std::sort(sortedResources.begin(),
				        sortedResources.end(),
				        [](const ResourceId& a, const ResourceId& b)
				        {
					        if (a.name != b.name)
						        return a.name < b.name;
					        return a.type.hash_code() < b.type.hash_code();
				        });

				// Collect mutex pointers
				for (const auto& resource: sortedResources)
				{
					auto& resourceLock = resourceLocks[resource];
					if (!resourceLock)
					{
						resourceLock = std::make_shared<std::shared_mutex>();
					}
					mutexPtrs.push_back(resourceLock);
				}
			}

			// Now lock them manually in order (exclusive locks for writing)
			for (auto& mutex: mutexPtrs)
			{
				mutex->lock();
			}

			// Create a RAII-style cleanup to release locks in the right order (reverse)
			struct ExclusiveLockCleanup
			{
				std::vector<std::shared_ptr<std::shared_mutex>>& mutexes;

				ExclusiveLockCleanup(std::vector<std::shared_ptr<std::shared_mutex>>& m)
				      : mutexes(m)
				{
				}

				~ExclusiveLockCleanup()
				{
					// Release in reverse order
					for (auto it = mutexes.rbegin(); it != mutexes.rend(); ++it)
					{
						(*it)->unlock();
					}
				}
			};

			// Use the cleanup
			ExclusiveLockCleanup cleanup(mutexPtrs);

			// Execute the task with acquired locks
			(*funcPtr)();
			// Locks released automatically when cleanup goes out of scope
		};

		// Submit the wrapped task to the pool
		pool.enqueue_detach(std::move(taskWrapper));
#else
		// In debug mode, acquire resource locks directly and execute the function
		// Sort resources by name and type to ensure consistent lock order
		std::vector<ResourceId> sortedResources = resources;
		std::sort(sortedResources.begin(),
		        sortedResources.end(),
		        [](const ResourceId& a, const ResourceId& b)
		        {
			        if (a.name != b.name)
				        return a.name < b.name;
			        return a.type.hash_code() < b.type.hash_code();
		        });

		// Collect and lock mutexes
		std::vector<std::shared_ptr<std::shared_mutex>> mutexPtrs;
		{
			std::lock_guard<std::mutex> guard(resourceMutex);
			for (const auto& resource: sortedResources)
			{
				auto& resourceLock = resourceLocks[resource];
				if (!resourceLock)
				{
					resourceLock = std::make_shared<std::shared_mutex>();
				}
				mutexPtrs.push_back(resourceLock);
			}
		}

		// Lock mutexes in order
		for (auto& mutex: mutexPtrs)
		{
			mutex->lock();
		}

		// Execute the function
		try
		{
			func();
		}
		catch (...)
		{
			// Unlock in reverse order in case of exception
			for (auto it = mutexPtrs.rbegin(); it != mutexPtrs.rend(); ++it)
			{
				(*it)->unlock();
			}
			throw; // Re-throw the exception
		}

		// Unlock in reverse order
		for (auto it = mutexPtrs.rbegin(); it != mutexPtrs.rend(); ++it)
		{
			(*it)->unlock();
		}
#endif

		// Update stats
		std::lock_guard<std::mutex> lock(statsMutex);
		tasksSubmitted++;
		resourceTasksSubmitted++;
	}

	// Overload that also takes additional arguments
	template<typename Func, typename... Args>
	void scheduleResourceTask(const std::vector<ResourceId>& resources, Func&& func, Args&&... args)
	{
		// Create a lambda that captures the function and arguments
		auto task = [func = std::forward<Func>(func), argsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable { return std::apply(func, argsTuple); };

		// Use the other overload
		scheduleResourceTask(resources, std::move(task));
	}

	/**
     * Schedule a resource task and get a future for its result
     * @param resources List of resource identifiers this task will access
     * @param func The function to execute
     * @param args The arguments to pass to the function
     * @return A future for the function's return value
     */
	template<typename ReturnType, typename Func, typename... Args>
	auto scheduleResourceTaskWithResult(const std::vector<ResourceId>& resources, Func&& func, Args&&... args)
	{
#ifndef THREAD_MANAGER_DEBUG
		// Create the task wrapper - explicitly using ReturnType
		auto taskWrapper = [this, resources, func = std::forward<Func>(func), args = std::make_tuple(std::forward<Args>(args)...)]() mutable -> ReturnType
		{
			// Acquire all resource locks in a consistent order to prevent deadlocks
			std::vector<std::unique_lock<std::shared_mutex>> locks;
			{
				std::lock_guard<std::mutex> guard(resourceMutex);

				// Sort resources by name and type to ensure consistent lock order
				std::vector<ResourceId> sortedResources = resources;
				std::sort(sortedResources.begin(),
				        sortedResources.end(),
				        [](const ResourceId& a, const ResourceId& b)
				        {
					        if (a.name != b.name)
						        return a.name < b.name;
					        return a.type.hash_code() < b.type.hash_code();
				        });

				// Create or get locks for each resource
				for (const auto& resource: sortedResources)
				{
					auto& resourceLock = resourceLocks[resource];
					if (!resourceLock)
					{
						resourceLock = std::make_shared<std::shared_mutex>();
					}
					locks.emplace_back(*resourceLock, std::defer_lock);
				}
			}

			// Lock all resources at once (prevents deadlocks)
			for (auto& lock: locks)
			{
				lock.lock();
			}

			// Execute the task with the acquired locks
			return std::apply(func, args);
		};

		// Submit the wrapped task to the pool
		auto future = pool.enqueue(taskWrapper);
#else
		// In debug mode, we execute directly and return a pre-completed future
		// Sort resources by name and type to ensure consistent lock order
		std::vector<ResourceId> sortedResources = resources;
		std::sort(sortedResources.begin(),
		        sortedResources.end(),
		        [](const ResourceId& a, const ResourceId& b)
		        {
			        if (a.name != b.name)
				        return a.name < b.name;
			        return a.type.hash_code() < b.type.hash_code();
		        });

		// Collect and lock mutexes
		std::vector<std::shared_ptr<std::shared_mutex>> mutexPtrs;
		{
			std::lock_guard<std::mutex> guard(resourceMutex);
			for (const auto& resource: sortedResources)
			{
				auto& resourceLock = resourceLocks[resource];
				if (!resourceLock)
				{
					resourceLock = std::make_shared<std::shared_mutex>();
				}
				mutexPtrs.push_back(resourceLock);
			}
		}

		// Lock mutexes in order
		for (auto& mutex: mutexPtrs)
		{
			mutex->lock();
		}

		// Create a future that will hold our result
		auto future = std::async(std::launch::deferred,
		        [&]() -> ReturnType
		        {
			        try
			        {
				        ReturnType result = std::apply(func, std::forward_as_tuple(std::forward<Args>(args)...));

				        // Unlock in reverse order
				        for (auto it = mutexPtrs.rbegin(); it != mutexPtrs.rend(); ++it)
				        {
					        (*it)->unlock();
				        }

				        return result;
			        }
			        catch (...)
			        {
				        // Unlock in reverse order in case of exception
				        for (auto it = mutexPtrs.rbegin(); it != mutexPtrs.rend(); ++it)
				        {
					        (*it)->unlock();
				        }
				        throw; // Re-throw the exception
			        }
		        });
#endif

		// Update stats
		std::lock_guard<std::mutex> lock(statsMutex);
		tasksSubmitted++;
		resourceTasksSubmitted++;

		return future;
	}

	template<typename Func, typename... Args>
	auto scheduleResourceTaskWithResult(const std::vector<ResourceId>& resources, Func&& func, Args&&... args)
	{
		// Deduce return type from function
		using ReturnType = std::invoke_result_t<std::decay_t<Func>, std::decay_t<Args>...>;

		// Call the explicit version
		return scheduleResourceTaskWithResult<ReturnType>(resources, std::forward<Func>(func), std::forward<Args>(args)...);
	}

	/**
     * Schedule a task that reads (but doesn't modify) specific resources
     * Multiple read tasks can run concurrently, but will block if a write task is running
     * @param resources List of resource identifiers this task will read
     * @param func The function to execute
     * @param args The arguments to pass to the function
     */
	template<typename Func, typename... Args>
	void scheduleReadTask(const std::vector<ResourceId>& resources, Func&& func, Args&&... args)
	{
#ifndef THREAD_MANAGER_DEBUG
		// Create a task wrapper that will handle resource locking
		auto taskWrapper = [this, resources, func = std::forward<Func>(func), args = std::make_tuple(std::forward<Args>(args)...)]() mutable
		{
			// Collect mutex pointers first
			std::vector<std::shared_ptr<std::shared_mutex>> mutexPtrs;
			{
				std::lock_guard<std::mutex> guard(resourceMutex);

				// Sort resources by name and type to ensure consistent lock order
				std::vector<ResourceId> sortedResources = resources;
				std::sort(sortedResources.begin(),
				        sortedResources.end(),
				        [](const ResourceId& a, const ResourceId& b)
				        {
					        if (a.name != b.name)
						        return a.name < b.name;
					        return a.type.hash_code() < b.type.hash_code();
				        });

				// Collect mutex pointers
				for (const auto& resource: sortedResources)
				{
					auto& resourceLock = resourceLocks[resource];
					if (!resourceLock)
					{
						resourceLock = std::make_shared<std::shared_mutex>();
					}
					mutexPtrs.push_back(resourceLock);
				}
			}

			// Now lock them manually in order (shared locks for reading)
			for (auto& mutex: mutexPtrs)
			{
				mutex->lock_shared();
			}

			// Create a RAII-style cleanup to release locks in the right order (reverse)
			struct SharedLockCleanup
			{
				std::vector<std::shared_ptr<std::shared_mutex>>& mutexes;

				SharedLockCleanup(std::vector<std::shared_ptr<std::shared_mutex>>& m)
				      : mutexes(m)
				{
				}

				~SharedLockCleanup()
				{
					// Release in reverse order
					for (auto it = mutexes.rbegin(); it != mutexes.rend(); ++it)
					{
						(*it)->unlock_shared();
					}
				}
			};

			// Use the cleanup
			SharedLockCleanup cleanup(mutexPtrs);

			// Execute the task with acquired locks
			return std::apply(func, args);
			// Locks released automatically when cleanup goes out of scope
		};

		// Submit the wrapped task to the pool
		pool.enqueue_detach(taskWrapper);
#else
		// In debug mode, acquire shared resource locks directly and execute the function
		// Sort resources by name and type to ensure consistent lock order
		std::vector<ResourceId> sortedResources = resources;
		std::sort(sortedResources.begin(),
		        sortedResources.end(),
		        [](const ResourceId& a, const ResourceId& b)
		        {
			        if (a.name != b.name)
				        return a.name < b.name;
			        return a.type.hash_code() < b.type.hash_code();
		        });

		// Collect and lock mutexes
		std::vector<std::shared_ptr<std::shared_mutex>> mutexPtrs;
		{
			std::lock_guard<std::mutex> guard(resourceMutex);
			for (const auto& resource: sortedResources)
			{
				auto& resourceLock = resourceLocks[resource];
				if (!resourceLock)
				{
					resourceLock = std::make_shared<std::shared_mutex>();
				}
				mutexPtrs.push_back(resourceLock);
			}
		}

		// Lock mutexes in shared mode
		for (auto& mutex: mutexPtrs)
		{
			mutex->lock_shared();
		}

		// Execute the function
		try
		{
			std::apply(func, std::forward_as_tuple(std::forward<Args>(args)...));
		}
		catch (...)
		{
			// Unlock in reverse order in case of exception
			for (auto it = mutexPtrs.rbegin(); it != mutexPtrs.rend(); ++it)
			{
				(*it)->unlock_shared();
			}
			throw; // Re-throw the exception
		}

		// Unlock in reverse order
		for (auto it = mutexPtrs.rbegin(); it != mutexPtrs.rend(); ++it)
		{
			(*it)->unlock_shared();
		}
#endif

		// Update stats
		std::lock_guard<std::mutex> lock(statsMutex);
		tasksSubmitted++;
		readTasksSubmitted++;
	}

	/**
     * Schedule a read task and get a future for its result
     * @param resources List of resource identifiers this task will read
     * @param func The function to execute
     * @param args The arguments to pass to the function
     * @return A future for the function's return value
     */
	template<typename Func, typename... Args>
	auto scheduleReadTaskWithResult(const std::vector<ResourceId>& resources, Func&& func, Args&&... args)
	{
		// Get the return type of the function
		using ReturnType = std::invoke_result_t<Func, Args...>;

#ifndef THREAD_MANAGER_DEBUG
		// Create the task wrapper
		auto taskWrapper = [this, resources, func = std::forward<Func>(func), args = std::make_tuple(std::forward<Args>(args)...)]() mutable -> ReturnType
		{
			// Acquire all resource locks in a consistent order to prevent deadlocks
			std::vector<std::shared_lock<std::shared_mutex>> locks;
			{
				std::lock_guard<std::mutex> guard(resourceMutex);

				// Sort resources by name and type to ensure consistent lock order
				std::vector<ResourceId> sortedResources = resources;
				std::sort(sortedResources.begin(),
				        sortedResources.end(),
				        [](const ResourceId& a, const ResourceId& b)
				        {
					        if (a.name != b.name)
						        return a.name < b.name;
					        return a.type.hash_code() < b.type.hash_code();
				        });

				// Create or get locks for each resource
				for (const auto& resource: sortedResources)
				{
					auto& resourceLock = resourceLocks[resource];
					if (!resourceLock)
					{
						resourceLock = std::make_shared<std::shared_mutex>();
					}
					locks.emplace_back(*resourceLock, std::defer_lock);
				}
			}

			// Lock all resources at once (prevents deadlocks)
			for (auto& lock: locks)
			{
				lock.lock();
			}

			// Execute the task with the acquired locks
			return std::apply(func, args);
		};

		// Submit the wrapped task to the pool
		auto future = pool.enqueue(taskWrapper);
#else
		// In debug mode, we execute directly and return a pre-completed future
		// Sort resources by name and type to ensure consistent lock order
		std::vector<ResourceId> sortedResources = resources;
		std::sort(sortedResources.begin(),
		        sortedResources.end(),
		        [](const ResourceId& a, const ResourceId& b)
		        {
			        if (a.name != b.name)
				        return a.name < b.name;
			        return a.type.hash_code() < b.type.hash_code();
		        });

		// Collect and lock mutexes
		std::vector<std::shared_ptr<std::shared_mutex>> mutexPtrs;
		{
			std::lock_guard<std::mutex> guard(resourceMutex);
			for (const auto& resource: sortedResources)
			{
				auto& resourceLock = resourceLocks[resource];
				if (!resourceLock)
				{
					resourceLock = std::make_shared<std::shared_mutex>();
				}
				mutexPtrs.push_back(resourceLock);
			}
		}

		// Create a future that will hold our result
		auto future = std::async(std::launch::deferred,
		        [&]() -> ReturnType
		        {
			        // Lock mutexes in shared mode
			        for (auto& mutex: mutexPtrs)
			        {
				        mutex->lock_shared();
			        }

			        try
			        {
				        ReturnType result = std::apply(func, std::forward_as_tuple(std::forward<Args>(args)...));

				        // Unlock in reverse order
				        for (auto it = mutexPtrs.rbegin(); it != mutexPtrs.rend(); ++it)
				        {
					        (*it)->unlock_shared();
				        }

				        return result;
			        }
			        catch (...)
			        {
				        // Unlock in reverse order in case of exception
				        for (auto it = mutexPtrs.rbegin(); it != mutexPtrs.rend(); ++it)
				        {
					        (*it)->unlock_shared();
				        }
				        throw; // Re-throw the exception
			        }
		        });
#endif

		// Update stats
		std::lock_guard<std::mutex> lock(statsMutex);
		tasksSubmitted++;
		readTasksSubmitted++;

		return future;
	}

	/**
     * Wait for all currently submitted tasks to complete
     */
	void waitForTasks()
	{
#ifndef THREAD_MANAGER_DEBUG
		pool.wait_for_tasks();
#else
		// In debug mode, all tasks are already completed since they run synchronously
		// No waiting needed
#endif
	}

	/**
     * Get statistics about task processing
     * @return A formatted string with task statistics
     */
	std::string getStatistics() const
	{
		std::lock_guard<std::mutex> lock(statsMutex);

		std::stringstream ss;
		ss << "Thread Pool Statistics:" << std::endl;
		ss << "  Threads: " << numThreads << std::endl;
		ss << "  Tasks submitted: " << tasksSubmitted << std::endl;
		ss << "  UI tasks: " << uiTasksSubmitted << std::endl;
		ss << "  Network tasks: " << networkTasksSubmitted << std::endl;
		ss << "  Resource tasks: " << resourceTasksSubmitted << std::endl;
		ss << "  Read tasks: " << readTasksSubmitted << std::endl;
#ifndef THREAD_MANAGER_DEBUG
		ss << "  Active tasks: " << pool.size() << std::endl;
#else
		ss << "  Active tasks: 0 (Debug Mode - Synchronous Execution)" << std::endl;
#endif
		ss << "  Active resources: " << resourceLocks.size() << std::endl;
		return ss.str();
	}

	/**
     * Helper function to create a resource ID from a type and name
     * @param name Resource name
     * @return ResourceId for the given resource
     */
	template<typename T>
	static ResourceId createResourceId(const std::string& name)
	{
		return ResourceId(name, std::type_index(typeid(T)));
	}

private:
#ifndef THREAD_MANAGER_DEBUG
	dp::thread_pool<> pool;
#endif
	size_t numThreads;

	// Statistics tracking
	mutable std::mutex statsMutex;
	mutable std::mutex uiMutex;
	std::atomic<uint64_t> tasksSubmitted{ 0 };
	std::atomic<uint64_t> uiTasksSubmitted{ 0 };
	std::atomic<uint64_t> networkTasksSubmitted{ 0 };
	std::atomic<uint64_t> resourceTasksSubmitted{ 0 };
	std::atomic<uint64_t> readTasksSubmitted{ 0 };

	// Resource synchronization
	std::mutex resourceMutex;
	std::unordered_map<ResourceId, std::shared_ptr<std::shared_mutex>> resourceLocks;
};
