#ifndef INC_CIRCULARBUFFER_HPP_
#define INC_CIRCULARBUFFER_HPP_

#include <array>
#include <cstddef>
#include <utility>
#include <atomic>
#include "BufferLikeConcept.hpp"

//
// ─────────────────────────────────────────────
//   LOW‑LEVEL: SPSCBuffer
// ─────────────────────────────────────────────
//

template <typename T, size_t capacity_>
class SPSCBuffer
{
    static constexpr size_t RealCapacity = capacity_ + 1;

public:
    SPSCBuffer() : head_(0), tail_(0) {}

    bool is_empty() const
    {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    bool is_full() const
    {
        size_t h = head_.load(std::memory_order_acquire);
        size_t t = tail_.load(std::memory_order_acquire);
        return ((h + 1) % RealCapacity) == t;
    }

    /**
     * Always safe to call: returns scratch slot data_[head_].
     * Producer NEVER modifies tail_.
     */
    T& begin_write()
    {
        return data_[head_.load(std::memory_order_relaxed)];
    }

    /**
     * Commits the slot written by begin_write().
     * Returns true if successfully queued, or false if dropped because full.
     */
    bool commit_write()
    {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t t = tail_.load(std::memory_order_acquire);

        // Check if full
        if (((h + 1) % RealCapacity) == t)
        {
            // QUEUE IS FULL!
            // Do NOT advance head_. The data written to data_[head_] 
            // is safely abandoned. Zero data corruption.
            return false; 
        }

        head_.store((h + 1) % RealCapacity, std::memory_order_release);
        return true;
    }
    
    T pop()
    {
        if (is_empty())
            return T{};

        size_t t = tail_.load(std::memory_order_relaxed);
        T out = std::move(data_[t]);
        tail_.store((t + 1) % RealCapacity, std::memory_order_release);
        return out;
    }

    size_t size() const
    {
        size_t h = head_.load(std::memory_order_acquire);
        size_t t = tail_.load(std::memory_order_acquire);
        if (h >= t) return h - t;
        return RealCapacity - (t - h);
    }

void clear()
{
    size_t t = tail_.load(std::memory_order_relaxed);
    size_t h = head_.load(std::memory_order_relaxed);

    while (t != h)
    {
        data_[t] = T{};
        t = (t + 1) % RealCapacity;
    }

    head_.store(0, std::memory_order_release);
    tail_.store(0, std::memory_order_release);
}

    static constexpr size_t capacity() { return capacity_; }

protected:
    // Safe front access for peek()
    T& front()
    {
        size_t t = tail_.load(std::memory_order_acquire);
        return data_[t];
    }

    const T& front() const
    {
        size_t t = tail_.load(std::memory_order_acquire);
        return data_[t];
    }

protected:
    std::array<T, RealCapacity> data_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};


//
// ─────────────────────────────────────────────
//   HIGH‑LEVEL: CircularBuffer (old API)
// ─────────────────────────────────────────────
//

template <typename T, size_t capacity_>
class CircularBuffer : private SPSCBuffer<T, capacity_>
{
    using Base = SPSCBuffer<T, capacity_>;

public:
    using Base::is_empty;
    using Base::is_full;
    using Base::size;
    using Base::capacity;
    using Base::clear;
    using Base::begin_write;
    using Base::commit_write;

    T& next()
    {
        T& ref = Base::begin_write();
        Base::commit_write();
        return ref;
    }

    void push(T value)
    {
        T& slot = Base::begin_write();
        slot = std::move(value);
        Base::commit_write();
    }

    T pop()
    {
        return Base::pop();
    }

    T& peek()
    {
        return Base::front();
    }

    const T& peek() const
    {
        return Base::front();
    }
};

static_assert(BufferLike<CircularBuffer<int, 8>, int>,
              "CircularBuffer must satisfy BufferLike concept");

#endif /* INC_CIRCULARBUFFER_HPP_ */
