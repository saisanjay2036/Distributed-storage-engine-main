#pragma once

#include <shared_mutex>
#include <mutex>

namespace dse {

class ReadWriteLock {
public:
    void lock_read() { mutex_.lock_shared(); }
    void unlock_read() { mutex_.unlock_shared(); }
    void lock_write() { mutex_.lock(); }
    void unlock_write() { mutex_.unlock(); }

private:
    std::shared_mutex mutex_;
};

class ReadLockGuard {
public:
    explicit ReadLockGuard(ReadWriteLock& rw) : rw_(rw) { rw_.lock_read(); }
    ~ReadLockGuard() { rw_.unlock_read(); }
private:
    ReadWriteLock& rw_;
};

class WriteLockGuard {
public:
    explicit WriteLockGuard(ReadWriteLock& rw) : rw_(rw) { rw_.lock_write(); }
    ~WriteLockGuard() { rw_.unlock_write(); }
private:
    ReadWriteLock& rw_;
};

}  // namespace dse
