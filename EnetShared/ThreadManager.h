#pragma once

#include "ThreadPool/thread_pool.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

/**
 * Enum for defining task priorities
 */
enum class TaskPriority {
    Low,
    Normal,
    High,
    Critical
};

/**
 * A wrapper around dp::thread_pool to manage threading in the game client
 */
class ThreadManager {
public:
    /**
     * Constructor creates a thread pool with the specified number of threads
     * @param numThreads Number of worker threads (default = hardware concurrency - 1)
     */
    ThreadManager(size_t numThreads = max(1u, std::thread::hardware_concurrency() - 1))
        : pool(numThreads), numThreads(numThreads) {
    }

    /**
     * Get the number of threads in the pool
     * @return Number of threads
     */
    size_t getThreadCount() const {
        return numThreads;
    }

    /**
     * Schedule a task without caring about the result
     * @param func The function to execute
     * @param args The arguments to pass to the function
     */
    template<typename Func, typename... Args>
    void scheduleTask(Func&& func, Args&&... args) {
        pool.enqueue_detach(std::forward<Func>(func), std::forward<Args>(args)...);
        
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
    auto scheduleTaskWithResult(Func&& func, Args&&... args) {
        auto future = pool.enqueue(std::forward<Func>(func), std::forward<Args>(args)...);
        
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
    void scheduleUITask(Func&& func, Args&&... args) {
        // Use a specific tag for UI tasks
        std::lock_guard<std::mutex> lock(uiMutex);
        pool.enqueue_detach(std::forward<Func>(func), std::forward<Args>(args)...);
        
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
    void scheduleNetworkTask(Func&& func, Args&&... args) {
        pool.enqueue_detach(std::forward<Func>(func), std::forward<Args>(args)...);
        
        // Update stats
        std::lock_guard<std::mutex> lock(statsMutex);
        tasksSubmitted++;
        networkTasksSubmitted++;
    }

    /**
     * Wait for all currently submitted tasks to complete
     */
    void waitForTasks() {
        pool.wait_for_tasks();
    }

    /**
     * Get statistics about task processing
     * @return A formatted string with task statistics
     */
    std::string getStatistics() const {
        std::lock_guard<std::mutex> lock(statsMutex);
        
        std::stringstream ss;
        ss << "Thread Pool Statistics:" << std::endl;
        ss << "  Threads: " << numThreads << std::endl;
        ss << "  Tasks submitted: " << tasksSubmitted << std::endl;
        ss << "  UI tasks: " << uiTasksSubmitted << std::endl;
        ss << "  Network tasks: " << networkTasksSubmitted << std::endl;
		ss << "  Active tasks: " << pool.size() << std::endl;
        return ss.str();
    }

private:
    dp::thread_pool<> pool;
    size_t numThreads;
    
    // Statistics tracking
    mutable std::mutex statsMutex;
    mutable std::mutex uiMutex;
    std::atomic<uint64_t> tasksSubmitted{0};
    std::atomic<uint64_t> uiTasksSubmitted{0};
    std::atomic<uint64_t> networkTasksSubmitted{0};
};