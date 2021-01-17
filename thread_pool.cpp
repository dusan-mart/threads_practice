#include <atomic>
#include <mutex>
#include <thread>
#include <future>
#include <vector>
#include <functional>
#include <condition_variable>
#include <queue>
#include <iterator>
#include <algorithm>
#include <iostream>


class thread_pool
{
public:
  using thread_id = std::vector<std::thread::id>;

  thread_pool(std::size_t num_threads = 0);

  //pool should be non-copyable but movable
  thread_pool(const thread_pool&) = delete;
  thread_pool& operator=(const thread_pool&) = delete;

  thread_pool(thread_pool&&) = default;
  thread_pool& operator=(thread_pool&&) = default;

  ~thread_pool();

  // add a function to be executed, along with any arguments for it
  template<typename Func, typename... Args>
  auto add(Func&& func, Args&&... args)->std::future<typename std::result_of<Func(Args...)>::type>;

  std::size_t threadCount() const;
  std::size_t waitingJobs() const;
  thread_id get_thread_id() const;
  void clear();
  void pause(bool state);
  void wait();

private:
  using job = std::function<void()>;

  // function each thread performs
  static void thread_task(thread_pool* pool);

  std::queue<job> jobs;
  mutable std::mutex jobs_mutex;

  // notification variable for waiting threads
  std::condition_variable jobs_available;

  std::vector<std::thread> threads;
  std::atomic<std::size_t> threads_waiting;

  std::atomic<bool> terminate;
  std::atomic<bool> paused;
};

template<typename Func, typename... Args>
auto thread_pool::add(Func&& func, Args&&... args) -> std::future<typename std::result_of<Func(Args...)>::type>
{
  //A packaged_task wraps a callable element and allows its result to be retrieved asynchronously.
  //It is similar to std::function, but transferring its result automatically to a future object.
  using pckd_task = std::packaged_task<typename std::result_of<Func(Args...)>::type()>;

  auto task = std::make_shared<pckd_task>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

  // get the future to return later
  auto ret = task->get_future();
  {
      std::lock_guard<std::mutex> lock{ jobs_mutex };
      jobs.emplace([task]() { (*task)(); });
  }

  // let a waiting thread know there is an available job
  jobs_available.notify_one();

  return ret;
}


void helloV1()
{
    std::cout << "hello world  thread:" << std::this_thread::get_id() << '\n';
}

void helloV2()
{
    std::cout << "HELLO WORLD  thread:" << std::this_thread::get_id() << '\n';
}

int main(){

  thread_pool t_pool;
  t_pool.pause(true);

  for(int i = 0; i < 10; ++i)
  {
    t_pool.add(helloV1);
    t_pool.add(helloV2);
  }
  t_pool.pause(false);
  t_pool.wait();

  std::cout << "END" << '\n';



  return 0;
}

//start desired number of threads
thread_pool::thread_pool(std::size_t num_threads)
  : threads_waiting(0), terminate(false), paused(false)
{
  if (num_threads == 0)
  {
    //Number of concurrent threads supported. If the value is not well defined or not computable, returns ​0​.
    num_threads = std::thread::hardware_concurrency();
  }
  // prevent potential reallocation
  threads.reserve(num_threads);
  std::generate_n(std::back_inserter(threads), num_threads, [this]() { return std::thread{ thread_task, this }; });
}

thread_pool::~thread_pool()
{
  clear();
  // tell threads to stop when they can
  terminate = true;
  jobs_available.notify_all();

  // wait for all threads to finish
  for (auto& t : threads)
  {
    if (t.joinable())
    {
      t.join();
    }
  }
}

std::size_t thread_pool::threadCount() const
{
  return threads.size();
}

std::size_t thread_pool::waitingJobs() const
{
  std::lock_guard<std::mutex> jobsLock(jobs_mutex);
  return jobs.size();
}

thread_pool::thread_id thread_pool::get_thread_id() const
{
  thread_id ret(threads.size());
  std::transform(threads.begin(), threads.end(), ret.begin(), [](auto& t) { return t.get_id(); });

  return ret;
}

void thread_pool::clear()
{
  std::lock_guard<std::mutex> lock{ jobs_mutex };

  while (!jobs.empty())
  {
    jobs.pop();
  }
}

void thread_pool::pause(bool state)
{
  paused = state;

  if (!paused)
  {
    jobs_available.notify_all();
  }
}

void thread_pool::wait()
{
  // we're done waiting once all threads are waiting
  while (threads_waiting != threads.size());
}

void thread_pool::thread_task(thread_pool* pool)
{
  // loop until we break (to keep thread alive)
  while (true)
  {
    // if we need to finish, let's do it before we get into
    // all the expensive synchronization stuff
    if (pool->terminate)
      break;

    std::unique_lock<std::mutex> jobsLock{ pool->jobs_mutex };

    // if there are no more jobs, or we're paused, go into waiting mode
    if (pool->jobs.empty() || pool->paused)
    {
      ++pool->threads_waiting;
      pool->jobs_available.wait(jobsLock, [&]()
      {
        return pool->terminate || !(pool->jobs.empty() || pool->paused);
      });

      --pool->threads_waiting;
    }

    // check once more before grabbing a job, since we want to stop ASAP
    if (pool->terminate)
      break;

    // take next job
    auto job = std::move(pool->jobs.front());
    pool->jobs.pop();

    jobsLock.unlock();

    job();
   }
}
