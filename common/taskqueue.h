/* Start Header ***************************************************************/
/*!
\file taskqueue.h

\author TODO

\par email: TODO

\date 10 Mar, 2026

\brief Copyright (C) 2026 DigiPen Institute of Technology.

Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited. */
/* End Header *****************************************************************/
#ifndef TASKQUEUE_H
#define TASKQUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <vector>
#include <atomic>

class TaskQueue {
public:
    TaskQueue();
    ~TaskQueue();

    // Initialise the thread pool with the given number of threads
    void Init(int numThreads);

    // Shutdown the thread pool
    void Shutdown();

    // Submit a task (a socket fd) and the function that processes it
    void Submit(std::function<void()> task);

private:
    void WorkerThread();

    std::vector<std::thread>            workers_;
    std::queue<std::function<void()>>   tasks_;
    std::mutex                          mutex_;
    std::condition_variable             cv_;
    std::atomic<bool>                   stop_;
};

#endif // TASKQUEUE_H
