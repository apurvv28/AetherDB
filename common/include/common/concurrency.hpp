#pragma once

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <process.h>
#else
    #include <pthread.h>
    #include <unistd.h>
#endif

#include <functional>
#include <memory>

namespace aether {

class Mutex {
public:
    Mutex() {
#ifdef _WIN32
        InitializeCriticalSection(&cs_);
#else
        pthread_mutex_init(&mutex_, nullptr);
#endif
    }
    
    ~Mutex() {
#ifdef _WIN32
        DeleteCriticalSection(&cs_);
#else
        pthread_mutex_destroy(&mutex_);
#endif
    }
    
    void Lock() {
#ifdef _WIN32
        EnterCriticalSection(&cs_);
#else
        pthread_mutex_lock(&mutex_);
#endif
    }
    
    void Unlock() {
#ifdef _WIN32
        LeaveCriticalSection(&cs_);
#else
        pthread_mutex_unlock(&mutex_);
#endif
    }

private:
#ifdef _WIN32
    CRITICAL_SECTION cs_;
#else
    pthread_mutex_t mutex_;
#endif
};

class LockGuard {
public:
    explicit LockGuard(Mutex& mtx) : mtx_(mtx) { mtx_.Lock(); }
    ~LockGuard() { mtx_.Unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
private:
    Mutex& mtx_;
};

class Thread {
public:
    Thread() : 
#ifdef _WIN32
    handle_(nullptr), thread_id_(0)
#else
    thread_(0)
#endif
    {}
    
    explicit Thread(std::function<void()> task) {
        auto ptr = new std::function<void()>(std::move(task));
        
#ifdef _WIN32
        handle_ = CreateThread(
            nullptr,
            0,
            &Thread::Win32Runner,
            ptr,
            0,
            &thread_id_
        );
#else
        pthread_create(&thread_, nullptr, &Thread::PosixRunner, ptr);
#endif
    }
    
    ~Thread() {
        #ifdef _WIN32
        if (handle_) {
            CloseHandle(handle_);
        }
        #endif
    }
    
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    
    Thread(Thread&& other) noexcept {
#ifdef _WIN32
        handle_ = other.handle_;
        thread_id_ = other.thread_id_;
        other.handle_ = nullptr;
        other.thread_id_ = 0;
#else
        thread_ = other.thread_;
        other.thread_ = 0;
#endif
    }
    
    Thread& operator=(Thread&& other) noexcept {
        if (this != &other) {
#ifdef _WIN32
            if (handle_) CloseHandle(handle_);
            handle_ = other.handle_;
            thread_id_ = other.thread_id_;
            other.handle_ = nullptr;
            other.thread_id_ = 0;
#else
            thread_ = other.thread_;
            other.thread_ = 0;
#endif
        }
        return *this;
    }
    
    void Join() {
#ifdef _WIN32
        if (handle_) {
            WaitForSingleObject(handle_, INFINITE);
            CloseHandle(handle_);
            handle_ = nullptr;
        }
#else
        if (thread_) {
            pthread_join(thread_, nullptr);
            thread_ = 0;
        }
#endif
    }
    
    void Detach() {
#ifdef _WIN32
        if (handle_) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
#else
        if (thread_) {
            pthread_detach(thread_);
            thread_ = 0;
        }
#endif
    }

private:
#ifdef _WIN32
    HANDLE handle_;
    DWORD thread_id_;
    
    static DWORD WINAPI Win32Runner(LPVOID param) {
        std::unique_ptr<std::function<void()>> task(static_cast<std::function<void()>*>(param));
        if (task && *task) {
            (*task)();
        }
        return 0;
    }
#else
    pthread_t thread_;
    
    static void* PosixRunner(void* param) {
        std::unique_ptr<std::function<void()>> task(static_cast<std::function<void()>*>(param));
        if (task && *task) {
            (*task)();
        }
        return nullptr;
    }
#endif
};

} // namespace aether
