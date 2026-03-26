/* Start Header ***************************************************************/
/*!
\file taskqueue.hpp

\author TODO

\par email: TODO

\date 10 Mar, 2026

\brief Copyright (C) 2026 DigiPen Institute of Technology.

Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited. */
/* End Header *****************************************************************/
#ifndef TASKQUEUE_HPP
#define TASKQUEUE_HPP

#include "taskqueue.h"

inline TaskQueue::TaskQueue() : stop_(false) {}

inline TaskQueue::~TaskQueue() {
    Shutdown();
}

inline void TaskQueue::Init(int numThreads) {
    stop_ = false;
    for (int i = 0; i < numThreads; ++i) {
        workers_.emplace_back(&TaskQueue::WorkerThread, this);
    }
}

inline void TaskQueue::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
}

inline void TaskQueue::Submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

inline void TaskQueue::WorkerThread() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

#endif // TASKQUEUE_HPP
