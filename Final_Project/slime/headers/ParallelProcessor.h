#pragma once
#include <vector>
#include <thread>
#include <future>
#include <functional>
#include <type_traits>
#include "Agent.h"

// TODO: restructure to change this forward declaration
class TrailMap;
class OptimizedTrailMap;
class SimulationSettings;

/**
 * high performance parallel processor for multithreaded agent updates
 * uses all available cpu cores with optimal work distribution
 */
class ParallelProcessor
{
public:
    // work distribution strategies
    enum class SchedulingPolicy
    {
        Static,  // equal chunks per thread
        Dynamic, // work stealing for load balancing
        Guided   // decreasing chunk sizes
    };

private:
    size_t numThreads_;
    std::vector<std::thread> threads_;
    SchedulingPolicy policy_;

    // thread pool management
    static size_t getOptimalThreadCount();

    template <typename Iterator, typename Function>
    void parallelForImpl(Iterator begin, Iterator end, Function &&func, SchedulingPolicy policy);

public:
    ParallelProcessor(size_t numThreads = 0, SchedulingPolicy policy = SchedulingPolicy::Static);
    ~ParallelProcessor() = default;

    // core parallel operations
    template <typename Container, typename Function>
    void parallelFor(Container &container, Function &&func);

    template <typename Iterator, typename Function>
    void parallelFor(Iterator begin, Iterator end, Function &&func);

    // specialized agent operations
    void parallelAgentUpdate(std::vector<Agent> &agents, const SimulationSettings &settings);
    void parallelAgentSensing(std::vector<Agent> &agents, TrailMap &trailMap, const SimulationSettings &settings);
    void parallelAgentMovement(std::vector<Agent> &agents, const SimulationSettings &settings);
    void parallelAgentDeposition(std::vector<Agent> &agents, TrailMap &trailMap, const SimulationSettings &settings);

    // new optimized methods for high performance systems
    template <typename Function>
    void processAgentsParallel(std::vector<Agent> &agents, Function &&func)
    {
        if (agents.empty())
            return;

        const size_t agentCount = agents.size();
        const size_t chunkSize = calculateChunkSize(agentCount, policy_);

        auto start = std::chrono::high_resolution_clock::now();

        // create futures for parallel execution
        std::vector<std::future<void>> futures;
        futures.reserve(numThreads_);

        for (size_t threadId = 0; threadId < numThreads_; ++threadId)
        {
            size_t startIdx = threadId * chunkSize;
            size_t endIdx = std::min(startIdx + chunkSize, agentCount);

            if (startIdx >= agentCount)
                break;

            futures.emplace_back(std::async(std::launch::async, [&agents, func, startIdx, endIdx]()
                                            {
                for (size_t i = startIdx; i < endIdx; ++i)
                {
                    func(agents[i]);
                } }));
        }

        // waits for all threads to complete
        for (auto &future : futures)
        {
            future.wait();
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration<double, std::milli>(end - start);

        // update performance metrics
        metrics_.totalOperations++;
        double execTime = duration.count();
        metrics_.avgExecutionTime = (metrics_.avgExecutionTime * (metrics_.totalOperations - 1) + execTime) / metrics_.totalOperations;
        metrics_.maxExecutionTime = std::max(metrics_.maxExecutionTime, execTime);
        metrics_.minExecutionTime = (metrics_.minExecutionTime == 0.0) ? execTime : std::min(metrics_.minExecutionTime, execTime);
    }
    void processTrailsParallel(class OptimizedTrailMap &trailMap, float diffuseRate, float decayRate);

    // trail processing
    void parallelTrailDiffusion(TrailMap &trailMap, float diffuseRate);
    void parallelTrailDecay(TrailMap &trailMap, float decayRate);

    // performance and configs
    size_t getThreadCount() const { return numThreads_; }
    void setSchedulingPolicy(SchedulingPolicy policy) { policy_ = policy; }
    SchedulingPolicy getSchedulingPolicy() const { return policy_; }

    // performance monitoring
    struct PerformanceMetrics
    {
        double avgExecutionTime = 0.0;
        double maxExecutionTime = 0.0;
        double minExecutionTime = 0.0;
        size_t totalOperations = 0;
        double loadBalance = 1.0; // 1.0 = perfect, lower = imbalanced
    };

    PerformanceMetrics getMetrics() const { return metrics_; }
    void resetMetrics() { metrics_ = PerformanceMetrics{}; }

private:
    mutable PerformanceMetrics metrics_;

    // chunk size calculation for different policies
    size_t calculateChunkSize(size_t totalWork, SchedulingPolicy policy) const;
};
