// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>

namespace utils {

template <typename T>
struct DoublyLinkedListTraits {
    static T* prev(T* obj) {
        return obj->list_prev();
    }
    static const T* prev(const T* obj) {
        return obj->list_prev();
    }
    static T* next(T* obj) {
        return obj->list_next();
    }
    static const T* next(const T* obj) {
        return obj->list_next();
    }

    static void set_prev(T* obj, T* prev) {
        return obj->list_set_prev(prev);
    }
    static void set_next(T* obj, T* next) {
        return obj->list_set_next(next);
    }
};

template <typename T, typename Traits = DoublyLinkedListTraits<T>>
class DoublyLinkedList {
public:
    using ValueType = T;

    static void reset(ValueType* obj) {
        Traits::set_prev(obj, obj);
        Traits::set_next(obj, obj);
    }

    constexpr DoublyLinkedList() : head_(nullptr) {}
    ~DoublyLinkedList() {
        DEBUG_ASSERT(is_empty());
    }

    bool is_empty() const {
        return (head_ == nullptr);
    }

    void clear() {
        head_ = nullptr;
    }

    void push_front(ValueType* obj) {
        if (!head_) {
            head_ = obj;
            reset(head_);
            return;
        }

        auto tail = Traits::prev(head_);

        Traits::set_next(obj, head_);
        Traits::set_prev(obj, tail);
        Traits::set_prev(head_, obj);
        Traits::set_next(tail, obj);

        head_ = obj;
    }

    void push_back(ValueType* obj) {
        if (!head_) {
            head_ = obj;
            reset(head_);
            return;
        }

        auto tail = Traits::prev(head_);

        Traits::set_prev(obj, tail);
        Traits::set_next(obj, head_);
        Traits::set_next(tail, obj);
        Traits::set_prev(head_, obj);
    }

    void add_after(ValueType *after, ValueType* obj) {
        auto next = Traits::next(after);

        Traits::set_prev(obj, after);
        Traits::set_next(obj, next);
        Traits::set_next(after, obj);
        Traits::set_prev(next, obj);
    }

    void add_before(ValueType *before, ValueType* obj) {
        auto prev = Traits::prev(before);

        Traits::set_prev(obj, prev);
        Traits::set_next(obj, before);
        Traits::set_prev(before, obj);
        Traits::set_next(prev, obj);

        if (head_ == before) {
            head_ = obj;
        }
    }

    ValueType* pop_front() {
        return is_empty() ? nullptr : pop_front_unsafe();
    }

    ValueType* pop_back() {
        return is_empty() ? nullptr : remove(Traits::prev(head_));
    }

    ValueType* remove(ValueType* obj) {
        return (obj == head_) ? pop_front_unsafe() : remove_unsafe(obj);
    }

    ValueType* first() {
        return head_;
    }
    const ValueType* first() const {
        return head_;
    }

    ValueType* last() {
        return is_empty() ? nullptr : Traits::prev(head_);
    }

    const ValueType* last() const {
        return is_empty() ? nullptr : Traits::prev(head_);
    }

    ValueType* prev(ValueType* obj) const {
        return (head_ == obj) ? nullptr : Traits::prev(obj);
    }

    const ValueType* prev(const ValueType* obj) const {
        return (head_ == obj) ? nullptr : Traits::prev(obj);
    }

    ValueType* next(ValueType* obj) const {
        auto next = Traits::next(obj);
        return (head_ == next) ? nullptr : next;
    }

    const ValueType* next(const ValueType* obj) const {
        auto next = Traits::next(obj);
        return (head_ == next) ? nullptr : next;
    }

    size_t size_slow() const {
        size_t count = 0;
        const ValueType* t = head_;
        while (t) {
            ++count;
            t = next(t);
        }
        return count;
    }

private:
    static ValueType* remove_unsafe(ValueType* obj) {
        auto next = Traits::next(obj);
        auto prev = Traits::prev(obj);
        Traits::set_prev(next, prev);
        Traits::set_next(prev, next);
        Traits::set_prev(obj, nullptr);
        Traits::set_next(obj, nullptr);
        return obj;
    }

    ValueType* pop_front_unsafe() {
        auto list_next = Traits::next(head_);
        if (list_next == head_) {
            head_ = nullptr;
            Traits::set_prev(list_next, nullptr);
            Traits::set_next(list_next, nullptr);
            return list_next;
        }
        ValueType* t = head_;
        head_ = list_next;
        return remove_unsafe(t);
    }

    ValueType* head_;
};

}  // namespace utils
