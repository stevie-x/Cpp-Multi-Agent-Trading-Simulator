#pragma once
#include <array>
#include <cstddef>
#include <optional>

// RingBuffer<T, N> — fixed-size circular array, zero heap allocation.
//
// Replaces std::deque<Order> at each price level.
// std::deque allocates heap chunks on every push_back().
// RingBuffer allocates all N slots upfront, inline — no heap after construction.
// push_back() = single array write + integer increment.
// pop_front() = single integer increment, zero deallocation.
//
// Capacity 64: we never see more than 3-4 orders per level in this sim.
// In production: size to p99.99 queue depth.

template<typename T, size_t N>
class RingBuffer {
public:
    bool push_back(const T& item) {
        if (full()) return false;
        buffer_[tail_] = item;
        tail_ = (tail_ + 1) % N;
        size_++;
        return true;
    }

    T& front()             { return buffer_[head_]; }
    const T& front() const { return buffer_[head_]; }

    void pop_front() {
        if (empty()) return;
        head_ = (head_ + 1) % N;
        size_--;
    }

    bool   empty() const { return size_ == 0; }
    bool   full()  const { return size_ == N - 1; }
    size_t size()  const { return size_; }

    // Stable pointer to most recently pushed element.
    // Used by orderIndex after push_back().
    // Safe: slot doesn't move until pop_front() is called,
    // which only happens after orderIndex removes the pointer.
    T* back_ptr() {
        size_t pos = (tail_ + N - 1) % N;
        return &buffer_[pos];
    }

    // Iterator for scanning all orders at a price level
    struct Iterator {
        RingBuffer* buf;
        size_t idx;
        size_t count;
        T& operator*()  { return buf->buffer_[idx]; }
        T* operator->() { return &buf->buffer_[idx]; }
        Iterator& operator++() { idx = (idx + 1) % N; count++; return *this; }
        bool operator!=(const Iterator& o) const { return count != o.count; }
    };

    struct ConstIterator {
        const RingBuffer* buf;
        size_t idx;
        size_t count;
        const T& operator*()  const { return buf->buffer_[idx]; }
        const T* operator->() const { return &buf->buffer_[idx]; }
        ConstIterator& operator++() { idx = (idx + 1) % N; count++; return *this; }
        bool operator!=(const ConstIterator& o) const { return count != o.count; }
    };

    Iterator begin() { return {this, head_, 0}; }
    Iterator end()   { return {this, tail_, size_}; }
    ConstIterator begin() const { return {this, head_, 0}; }
    ConstIterator end()   const { return {this, tail_, size_}; }

private:
    std::array<T, N> buffer_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t size_ = 0;
};