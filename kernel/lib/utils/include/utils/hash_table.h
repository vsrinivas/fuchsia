// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <utils/intrusive_single_list.h>

namespace utils {

// The number of buckets should be a nice prime such as 37, 211, 389 unless
// The hash function is really good. Lots of cheap hash functions have hidden
// periods for which the mod with prime above 'mostly' fixes.

template <typename Key, typename T, typename HashFn, size_t num_buckets = 37,
          typename ListTraits = DefaultSinglyLinkedListTraits<T*>>
class HashTable {
public:
    using BucketType = SinglyLinkedList<T*, ListTraits>;

    constexpr HashTable() {}
    ~HashTable() {
        DEBUG_ASSERT(count_ == 0);
    }

    void add(Key key, T* entry) {
        ++count_;
        SetHashTableKey(entry, key);
        GetBucket(key).push_front(entry);
    }

    T* get(Key key) {
        BucketType& bucket = GetBucket(key);
        for (auto& obj : bucket) {
            if (key == GetHashTableKey(&obj))
                return &obj;
        }

        return nullptr;
    }

    T* remove(Key key) {
        BucketType& bucket = GetBucket(key);
        T* found = bucket.erase_if([key](const T& entry) {
            return key == GetHashTableKey(&entry);
        });
        if (found) --count_;
        return found;
    }

    void clear() {
        for (auto& e : buckets_)
            e.clear();
        count_ = 0;
    }

    size_t size() const {
        return count_;
    }
    bool is_empty() const {
        return count_ == 0;
    }

    template <typename UnaryFn>
    void for_each(UnaryFn fn) {
        for (auto& bucket : buckets_) {
            for (auto& obj : bucket) {
                fn(&obj);
            }
        }
    }

private:
    BucketType& GetBucket(Key key) {
        return buckets_[hashfn_(key) % num_buckets];
    }

    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    size_t count_ = 0UL;
    BucketType buckets_[num_buckets];
    HashFn hashfn_;
};

}  // namespace utils
