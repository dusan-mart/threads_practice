#include <thread>
#include <iostream>
#include <vector>
#include <mutex>
#include <condition_variable>

void hello(){
  std::cout << "hello from thread" << std::this_thread::get_id() << '\n';
}

//using function
void execute_V1(){
  //usually all active threads are saved in vector
  std::vector<std::thread> vec;

  for(int i = 0; i < 10; ++i)
  {
    //param-A pointer to function, pointer to member, or any kind of move-constructible function object
    // (i.e., an object whose class defines operator(), including closures and function objects).
    //The return value (if any) is ignored.
    vec.push_back(std::thread(hello));
  }
  for(auto &a : vec)
  {
    a.join();
  }
}

//using lambda
void execute_V2(){
  std::vector<std::thread> vec;

  for(int i = 0; i < 10; ++i)
  {
    vec.push_back(std::thread([](){
          std::cout << "hello from thread" << std::this_thread::get_id() << '\n';
    }));
  }
  for(auto &a : vec)
  {
    a.join();
  }
}

//multiple threads accesing 1 object without synchronization
void execute_V3()
{
  int counter = 0;
  std::vector<std::thread> threads;

  for(int i = 0; i < 5; ++i)
  {
    threads.push_back(std::thread([&counter](){
      for(int j = 0; j < 10000; ++j)
      {
        counter++;
      }
    }));
  }

  for(auto &a : threads)
  {
    a.join();
  }
  std::cout << counter << '\n';
}

//multiple threads accesing 1 object with synchronization
//using semaphors
void execute_V4()
{

  struct Counter{
    Counter(){counter = 0;}

    void increment(){
      mutex.lock();
      counter++;
      mutex.unlock();
    }

    int get(){return counter;}
  private:
    int counter;
    //semaphor which gives access to only 1 thread
    //semaphors are slow, when you use locks you make sections of the code sequential
    //problematic if multiple exits in program, object could be left locked
    std::mutex mutex;
  };
  Counter c;
  std::vector<std::thread> threads;
  for(int i = 0; i < 5; ++i)
  {
    threads.push_back(std::thread([&c](){
      for(int j = 0; j < 10000; ++j)
      {
          c.increment();
      }
    }));
  }

  for(auto &a : threads)
  {
    a.join();
  }
  std::cout << c.get() << '\n';
}

//using lock_guard
void execute_V5()
{

  struct Counter{
    Counter(){counter = 0;}

    //lock_guard automaticly calls lock on mutex, and releases when gets destructed
    void increment(){
      std::lock_guard<std::mutex> lock(mutex);
      counter++;
    }

    int get(){return counter;}
  private:
    int counter;
    std::mutex mutex;
    //std::recursive_mutex
    //allows SAME thread to acquire mutex several times
  };
  Counter c;
  std::vector<std::thread> threads;
  for(int i = 0; i < 5; ++i)
  {
    threads.push_back(std::thread([&c](){
      for(int j = 0; j < 10000; ++j)
      {
          c.increment();
      }
    }));
  }

  for(auto &a : threads)
  {
    a.join();
  }
  std::cout << c.get() << '\n';
}
//std::timed_mutex and std::recursive_timed_mutex
//access to the same functions as a std::mutex: lock() and unlock(),
//but you have also two new functions: try_lock_for() and try_lock_until().
//set a timeout, function automatically returns even if the lock was not acquired
//function returns true if the lock has been acquired, false otherwis
//ex:   std::chrono::milliseconds timeout(100);
//if(mutex.try_lock_for(timeout)){...

//-------------------------------------------------------------------------------
//Condition variables

#define CONSUMED 5
#define PRODUCED 20
struct bounded_buffer {
  int* buffer;
  int capacity;

  int front;
  int rear;
  int count;

  std::mutex lock;
  //A condition variable manages a list of threads
  // waiting until another thread notify them
  //Each thread that wants to wait on the condition variable
  // has to acquire a lock first.
  //The lock is then released when the thread starts to wait on the condition
  // and the lock is acquired again when the thread is awakened.

  std::condition_variable not_full;
  std::condition_variable not_empty;

  bounded_buffer(int capacity) : capacity(capacity), front(0), rear(0), count(0) {
      buffer = new int[capacity];
  }

  ~bounded_buffer(){
      delete[] buffer;
  }


  void deposit(int data, int id){
    std::unique_lock<std::mutex> l(lock);
    //arg2: A callable object or function that takes no arguments and returns a value that can be evaluated as a bool.
    //This is called repeatedly until it evaluates to true.
    not_full.wait(l, [this](){return count != capacity; });

    buffer[rear] = data;
    rear = (rear + 1) % capacity;
    ++count;
    std::cout << "Producer: " << id << " produced amount: " << data << std::endl;
    l.unlock();
    not_empty.notify_one();
  }

  int fetch(int id){
    std::unique_lock<std::mutex> l(lock);

    not_empty.wait(l, [this](){return count != 0; });

    int result = buffer[front];
    front = (front + 1) % capacity;
    --count;
    std::cout << "Consumer: " << id << " fetched amount: " << result << std::endl;
    l.unlock();
    not_full.notify_one();

    return result;
  }
};
void consumer(int id, bounded_buffer& buffer){
  for(int i = 0; i < CONSUMED; ++i){
      int value = buffer.fetch(id);
      //std::cout << "Consumer " << id << " fetched " << value << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
}

void producer(int id, bounded_buffer& buffer){
  for(int i = 0; i < PRODUCED; ++i){
      buffer.deposit(i, id);
      //std::cout << "Produced " << id << " produced " << i << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void execute_cond_variable(){

  bounded_buffer buffer(200);

   std::thread c1(consumer, 0, std::ref(buffer));
   std::thread c2(consumer, 1, std::ref(buffer));
   std::thread c3(consumer, 2, std::ref(buffer));
   std::thread p1(producer, 0, std::ref(buffer));
   std::thread p2(producer, 1, std::ref(buffer));

   c1.join();
   c2.join();
   c3.join();
   p1.join();
   p2.join();
}

int main(){

  //execute_V1();
  //execute_V2();
  //execute_V3();
  //execute_V4();
  //execute_V5();
  execute_cond_variable();


  return 0;
}
