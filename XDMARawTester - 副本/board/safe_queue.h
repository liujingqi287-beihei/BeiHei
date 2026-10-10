#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <utility>

template <typename T>
class SafeQueue {
public:
    explicit SafeQueue(size_t capacity = 1000)
        : maxCapacity(capacity), stopped(false), totalPoppedBytes(0), totalPoppedItems(0) {}

    // 完美转发，杜绝入参不必要的深拷贝
    template <typename U>
    void pushData(U&& item, const std::atomic<bool>& isRunning) {
        std::unique_lock<std::mutex> lock(mtx);
        notFullCv.wait(lock, [this, &isRunning]() {
            return queue.size() < maxCapacity || stopped || !isRunning.load();
            });

        if (stopped || !isRunning.load()) return;

        queue.push(std::forward<U>(item));
        notEmptyCv.notify_one();
    }

    bool popData(T& item, const std::atomic<bool>& isRunning, int timeoutMs = 50) {
        std::unique_lock<std::mutex> lock(mtx);
        if (!notEmptyCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this, &isRunning]() {
            return !queue.empty() || stopped || !isRunning.load();
            })) {
            return false;
        }

        if (queue.empty()) return false;

        item = std::move(queue.front());
        queue.pop();

        totalPoppedItems++;
        notFullCv.notify_one();
        return true;
    }

    size_t getQueueSize() const {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.size();
    }

    uint64_t getTotalPoppedItems() const { return totalPoppedItems.load(); }

    void notifyAllWaiters() {
        std::lock_guard<std::mutex> lock(mtx);
        stopped = true;
        notEmptyCv.notify_all();
        notFullCv.notify_all();
    }

private:
    std::queue<T> queue;
    mutable std::mutex mtx;
    std::condition_variable notEmptyCv;
    std::condition_variable notFullCv;
    size_t maxCapacity;
    bool stopped;

    std::atomic<uint64_t> totalPoppedBytes;
    std::atomic<uint64_t> totalPoppedItems;
};