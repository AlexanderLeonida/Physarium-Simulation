#include "ParallelProcessor.h"
#include "Agent.h"
#include "TrailMap.h"
#include "OptimizedTrailMap.h"
#include "SimulationSettings.h"
#include "SpatialGrid.h"
#include <algorithm>
#include <chrono>
#include <iostream>

ParallelProcessor::ParallelProcessor(size_t numThreads, SchedulingPolicy policy)
    : policy_(policy)
{
    numThreads_ = (numThreads == 0) ? getOptimalThreadCount() : numThreads;
    std::cout << "ParallelProcessor initialized with " << numThreads_ << " threads" << std::endl;
}

size_t ParallelProcessor::getOptimalThreadCount()
{
    size_t hwThreads = std::thread::hardware_concurrency();
    if (hwThreads == 0)
        hwThreads = 4; // fallback...

    // reserve one thread for main/rendering, use rest for compute
    return std::max(static_cast<size_t>(1), hwThreads - 1);
}

template <typename Container, typename Function>
void ParallelProcessor::parallelFor(Container &container, Function &&func)
{
    parallelFor(container.begin(), container.end(), std::forward<Function>(func));
}

template <typename Iterator, typename Function>
void ParallelProcessor::parallelFor(Iterator begin, Iterator end, Function &&func)
{
    parallelForImpl(begin, end, std::forward<Function>(func), policy_);
}

template <typename Iterator, typename Function>
void ParallelProcessor::parallelForImpl(Iterator begin, Iterator end, Function &&func, SchedulingPolicy policy)
{
    const size_t totalWork = std::distance(begin, end);
    if (totalWork == 0)
        return;

    if (totalWork < numThreads_ * 2)
    {
        // too little work for parallelization overhead
        for (auto it = begin; it != end; ++it)
        {
            func(*it);
        }
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    const size_t chunkSize = calculateChunkSize(totalWork, policy);
    std::vector<std::future<void>> futures;
    futures.reserve(numThreads_);

    // distribute work across threads
    for (size_t threadId = 0; threadId < numThreads_; ++threadId)
    {
        size_t start = threadId * chunkSize;
        size_t end = std::min(start + chunkSize, totalWork);

        if (start >= totalWork)
            break;

        futures.emplace_back(std::async(std::launch::async, [=, &func]()
                                        {
            auto threadBegin = std::next(begin, start);
            auto threadEnd = std::next(begin, end);
            
            for (auto it = threadBegin; it != threadEnd; ++it) {
                func(*it);
            } }));
    }

    // wait for all threads to complete
    for (auto &future : futures)
    {
        future.wait();
    }

    // update performance metrics
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    metrics_.totalOperations++;
    metrics_.avgExecutionTime = (metrics_.avgExecutionTime * (metrics_.totalOperations - 1) + duration) / metrics_.totalOperations;
    metrics_.maxExecutionTime = std::max(metrics_.maxExecutionTime, duration);
    metrics_.minExecutionTime = (metrics_.minExecutionTime == 0.0) ? duration : std::min(metrics_.minExecutionTime, duration);
}

void ParallelProcessor::parallelAgentUpdate(std::vector<Agent> &agents, const SimulationSettings &settings)
{
    parallelFor(agents, [&settings](Agent &agent)
                { agent.move(settings); });
}

void ParallelProcessor::parallelAgentSensing(std::vector<Agent> &agents, TrailMap &trailMap, const SimulationSettings &settings)
{
    parallelFor(agents, [&trailMap, &settings](Agent &agent)
                { agent.senseMultiSpecies(trailMap, settings); });
}

void ParallelProcessor::parallelAgentMovement(std::vector<Agent> &agents, const SimulationSettings &settings)
{
    parallelFor(agents, [&settings](Agent &agent)
                {
        agent.move(settings);
        agent.wrapPosition(settings.width, settings.height); });
}

void ParallelProcessor::parallelAgentDeposition(std::vector<Agent> &agents, TrailMap &trailMap, const SimulationSettings &settings)
{
    // NOTE: trail deposition needs synchronization - this is just a simplified version for now
    // in practice need to actually set u[] atomic operations or per thread buffers?
    parallelFor(agents, [&trailMap, &settings](Agent &agent)
                { agent.depositMultiSpecies(trailMap, settings); });
}

void ParallelProcessor::parallelTrailDiffusion(TrailMap &trailMap, float diffuseRate)
{
    // implement parallel diffusion - this would need to be coordinated with trailmap internals
    trailMap.diffuse(diffuseRate);
}

void ParallelProcessor::parallelTrailDecay(TrailMap &trailMap, float decayRate)
{
    // implement parallel decay
    trailMap.decay(decayRate);
}

size_t ParallelProcessor::calculateChunkSize(size_t totalWork, SchedulingPolicy policy) const
{
    switch (policy)
    {
    case SchedulingPolicy::Static:
        return (totalWork + numThreads_ - 1) / numThreads_; // ceiling division

    case SchedulingPolicy::Dynamic:
        return std::max(static_cast<size_t>(1), totalWork / (numThreads_ * 4)); // smaller chunks for work stealing

    case SchedulingPolicy::Guided:
        return std::max(static_cast<size_t>(1), totalWork / (numThreads_ * 2)); // medium chunks

    default:
        return (totalWork + numThreads_ - 1) / numThreads_;
    }
}

// explicit template instantiations for common use cases
template void ParallelProcessor::parallelFor<std::vector<Agent>>(std::vector<Agent> &, std::function<void(Agent &)> &&);
template void ParallelProcessor::parallelFor<std::vector<Agent>::iterator>(std::vector<Agent>::iterator, std::vector<Agent>::iterator, std::function<void(Agent &)> &&);


void ParallelProcessor::processTrailsParallel(OptimizedTrailMap &trailMap, float diffuseRate, float decayRate)
{
    auto start = std::chrono::high_resolution_clock::now();

    const int numSpecies = trailMap.getNumSpecies();
    const size_t chunkSize = calculateChunkSize(numSpecies, policy_);

    // parallel diffusion
    std::vector<std::future<void>> diffuseFutures;
    diffuseFutures.reserve(numThreads_);

    for (size_t threadId = 0; threadId < numThreads_; ++threadId)
    {
        size_t startSpecies = threadId * chunkSize;
        size_t endSpecies = std::min(startSpecies + chunkSize, static_cast<size_t>(numSpecies));

        if (startSpecies >= static_cast<size_t>(numSpecies))
            break;

        diffuseFutures.emplace_back(std::async(std::launch::async, [&trailMap, diffuseRate, startSpecies, endSpecies]()
                                               {
            for (size_t species = startSpecies; species < endSpecies; ++species)
            {
                // each thread processes different species independently
                // this avoids race conditions and maximizes cache efficiency
                trailMap.diffuseOptimized(diffuseRate);
            } }));
    }

    // wait for diffusion to complete
    for (auto &future : diffuseFutures)
    {
        future.wait();
    }

    // parallel decay (can be done simultaneously as its like a per pixel operation)
    std::vector<std::future<void>> decayFutures;
    decayFutures.reserve(numThreads_);

    for (size_t threadId = 0; threadId < numThreads_; ++threadId)
    {
        size_t startSpecies = threadId * chunkSize;
        size_t endSpecies = std::min(startSpecies + chunkSize, static_cast<size_t>(numSpecies));

        if (startSpecies >= static_cast<size_t>(numSpecies))
            break;

        decayFutures.emplace_back(std::async(std::launch::async, [&trailMap, decayRate, startSpecies, endSpecies]()
                                             {
            for (size_t species = startSpecies; species < endSpecies; ++species)
            {
                trailMap.decayOptimized(decayRate);
            } }));
    }

    // wait for decay to complete here
    for (auto &future : decayFutures)
    {
        future.wait();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double, std::milli>(end - start);

    // then update performance metrics
    metrics_.totalOperations++;
    double execTime = duration.count();
    metrics_.avgExecutionTime = (metrics_.avgExecutionTime * (metrics_.totalOperations - 1) + execTime) / metrics_.totalOperations;
    metrics_.maxExecutionTime = std::max(metrics_.maxExecutionTime, execTime);
    metrics_.minExecutionTime = (metrics_.minExecutionTime == 0.0) ? execTime : std::min(metrics_.minExecutionTime, execTime);
}
