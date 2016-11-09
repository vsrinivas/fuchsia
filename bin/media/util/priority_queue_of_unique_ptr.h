// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>

namespace media {

// Compares the referents of two pointers.
template <class T>
struct priority_queue_of_unique_ptr_compare {
  bool operator()(T a, T b) { return *a < *b; }
};

// A priority_queue that (effectively) holds unique_ptrs to type T. We actually
// store the elements as raw pointers, but preserve move semantics coming and
// going. The destructor deletes the remaining elements.
//
// priority_queue doesn't work with move semantics, because the only way to
// access an element is using top(), which returns a const reference.
//
// TODO(dalesat): If this issue with prioity_queue is ever fixed, delete this.
// https://cplusplus.github.io/LWG/lwg-closed.html#2552
template <class T,
          class Container = std::vector<T*>,
          class Compare = priority_queue_of_unique_ptr_compare<
              typename Container::value_type>>
class priority_queue_of_unique_ptr {
 public:
  ~priority_queue_of_unique_ptr() { DeleteAllElements(); }

  priority_queue_of_unique_ptr& operator=(
      priority_queue_of_unique_ptr&& other) {
    DeleteAllElements();
    internal_ = std::move(other.internal_);
    return *this;
  }

  // Checks whether the queue is empty.
  bool empty() const { return internal_.empty(); }

  // Returns the number of elements.
  size_t size() const { return internal_.size(); }

  // Returns a const reference to the top element. Note that this is not a
  // reference to a unique_ptr but rather a reference to the T itself.
  const T& top() const { return *internal_.top(); }

  // Gets a raw pointer to the top element. Use with caution, as you would
  // unique_ptr::get. If you follow this with pop() or otherwise delete the
  // top element, you have a dangling pointer.
  T* get_top() const { return internal_.top(); }

  // Pushes and takes ownership of t.
  void push(std::unique_ptr<T> t) { internal_.push(t.release()); }

  // Deletes the top element.
  void pop() {
    delete internal_.top();
    internal_.pop();
  }

  // Pops the top element and returns a unique_ptr to it in one operation.
  std::unique_ptr<T> pop_and_move() {
    std::unique_ptr<T> result = std::unique_ptr<T>(internal_.top());
    internal_.pop();
    return result;
  }

  // Swaps the contents.
  void swap(priority_queue_of_unique_ptr& other) {
    internal_.swap(other.internal_);
  }

 private:
  void DeleteAllElements() {
    while (!empty()) {
      pop();
    }
  }

  std::priority_queue<T*, Container, Compare> internal_;
};

}  // namespace media
