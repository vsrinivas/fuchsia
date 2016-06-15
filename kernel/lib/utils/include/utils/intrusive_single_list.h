// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>

namespace utils {

template <typename T>
struct SinglyLinkedListTraits {
    static T* next(T* node) {
        return node->list_next();
    }
    static const T* next(const T* node) {
        return node->list_next();
    }
    static void set_next(T* node, T* next) {
        node->list_set_next(next);
    }
};

template <typename T, typename Traits = SinglyLinkedListTraits<T>>
class SinglyLinkedList {
public:
    using ValueType = T;

    constexpr SinglyLinkedList() {}
    ~SinglyLinkedList() {
        DEBUG_ASSERT(is_empty());
    }

    ValueType* first() {
        return head_;
    }
    const ValueType* first() const {
        return head_;
    }

    ValueType* next(ValueType* node) {
        return Traits::next(node);
    }
    const ValueType* next(const ValueType* node) const {
        return Traits::next(node);
    }

    bool is_empty() const {
        return head_ == nullptr;
    }

    void push_front(ValueType* node) {
        Traits::set_next(node, head_);
        head_ = node;
    }

    void add_after(ValueType *after, ValueType* node) {
        auto next = Traits::next(after);

        Traits::set_next(node, next);
        Traits::set_next(after, node);
    }

    ValueType* pop_front() {
        if (is_empty()) return nullptr;
        auto t = head_;
        head_ = Traits::next(head_);
        Traits::set_next(t, nullptr);
        return t;
    }

    ValueType* pop_next(ValueType* prev) {
        auto node = Traits::next(prev);
        auto next = Traits::next(node);
        Traits::set_next(prev, next);
        Traits::set_next(node, nullptr);
        return node;
    }

    size_t size_slow() const {
        size_t count = 0;
        const ValueType* t = head_;
        while (t) {
            ++count;
            t = Traits::next(t);
        }
        return count;
    }

    void clear() {
        head_ = nullptr;
    }

private:
    SinglyLinkedList(const SinglyLinkedList&) = delete;
    SinglyLinkedList& operator=(const SinglyLinkedList&) = delete;

    ValueType* head_ = nullptr;
};

// TODO: support C++11 range iterator.

}  // namespace utils
