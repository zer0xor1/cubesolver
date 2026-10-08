// Thread-safe least-recently-used cache.
//
// get() and put() are O(1): a hash map points into a linked list that keeps
// entries in use order. When the cache is full, the oldest entry is dropped.
#pragma once

#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace cube::server {

template <typename Key, typename Value>
class LruCache {
public:
    explicit LruCache(size_t capacity) : capacity_(capacity) {}

    std::optional<Value> get(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = index_.find(key);
        if (it == index_.end()) return std::nullopt;
        order_.splice(order_.begin(), order_, it->second);  // mark as most recent
        return it->second->second;
    }

    void put(const Key& key, Value value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (capacity_ == 0) return;
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = std::move(value);
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        if (order_.size() >= capacity_) {
            index_.erase(order_.back().first);
            order_.pop_back();
        }
        order_.emplace_front(key, std::move(value));
        index_[key] = order_.begin();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return order_.size();
    }

private:
    using Entry = std::pair<Key, Value>;
    const size_t capacity_;
    mutable std::mutex mutex_;
    std::list<Entry> order_;  // front = most recently used
    std::unordered_map<Key, typename std::list<Entry>::iterator> index_;
};

}  // namespace cube::server
