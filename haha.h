#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    ThreadPool(size_t);
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<decltype(f(args...))>;
    ~ThreadPool();
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue
    std::queue< std::function<void()> > tasks;
    
    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};
 
// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    :   stop(false)
{
   for(int i=0;i<threads;++i){
      workers.emplace_back([this]{
        while(true){
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(queue_mutex);
                condition.wait(lk,[this](){return !tasks.empty() || stop;});
                if(stop){
                    return;
                }
                task = std::move(tasks.front());
                tasks.pop();
            }
            task();
        }
      });
   }
}

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<decltype(f(args...))>
{
    // construct a function variable
    auto task = std::make_shared< std::packaged_task<decltype(f(args...))()> >(
        std::bind(f,args...)
    );

    // get the future from task
    auto res = task->get_future();

    {
        std::unique_lock<std::mutex> lk(queue_mutex);
        tasks.emplace([task](){(*task)();});
    }
    condition.notify_one();

    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lk(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}

#endif