#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <tuple>
#include <queue>
#include <array>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>

namespace internal {
    class TaskI 
    {
        public:
            virtual ~TaskI() = default;
            virtual void execute() = 0;
    };

    template <typename Fn, typename ...Args>
    class PackagedTask : public TaskI {
        using ret_type = std::result_of_t<Fn(Args...)>;

        public:
            PackagedTask(Fn&& fn, Args&& ...args)
              : fn_(std::forward<Fn>(fn)), args_(std::forward<Args>(args)...)
            {}

            virtual void execute() override
            {
                prom_.set_value(std::apply(fn_, std::move(args_)));
            }

            std::future<ret_type> get_future()
            {
                return prom_.get_future();
            }

        private:
            Fn fn_;
            std::promise<ret_type> prom_;
            std::tuple<Args...> args_;
    };

    template<typename Lambda, std::size_t... I>
    std::array<std::thread, sizeof...(I)> make_workers_impl(const Lambda& value, std::index_sequence<I...>)
    {
        return {(static_cast<void>(I), std::thread(value))...};
    }

    template <std::size_t N, typename Lambda>
    std::array<std::thread, N> make_workers(const Lambda& value)
    {
        return make_workers_impl(value, std::make_index_sequence<N>());
    }
}

template <size_t N>
class ThreadPool {
public:
    ThreadPool()
      :
        workers_(internal::make_workers<N>([this]() {
            while (true)
            {
                std::unique_lock<std::mutex> lck(mtx_);
                while (!exit_ and tasks_.empty())
                    condition_.wait(lck);
                if (exit_ and tasks_.empty())
                    return;
                auto task = std::move(tasks_.front());
                tasks_.pop();
                lck.unlock();
                task->execute();
            }
        }))
    {}

    template<class Fn, class... Args>
    auto enqueue(Fn&& fn, Args&&... args) 
        -> std::future<typename std::result_of_t<Fn(Args...)>>
    {
        using ret_type = typename  std::result_of_t<Fn(Args...)>;
        auto task = std::make_unique<internal::PackagedTask<Fn, Args...>>(std::forward<Fn>(fn), std::forward<Args>(args)...);
        std::future<ret_type> ret = task->get_future();
        {
            std::lock_guard<std::mutex> lck(mtx_);
            tasks_.push(std::move(task));
        }
        condition_.notify_one();
        return ret;
    }
    
    ~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lck(mtx_);
            exit_ = true;
        }
        condition_.notify_all();
        for (auto &thr : workers_)
            thr.join();
    }

private:
    std::mutex mtx_;
    std::queue<std::unique_ptr<internal::TaskI>> tasks_;
    std::condition_variable condition_;
    bool exit_ = false;

    std::array<std::thread, N> workers_;
};


#endif