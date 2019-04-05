// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CONTAINERS_CPP_TEST_OBJECTS_H_
#define LIB_CONTAINERS_CPP_TEST_OBJECTS_H_

// Test object to verify that the right constructors are called.
// Excerpts taken from:
//
// - base/test/copy_only_int.h
// - base/test/move_only_int.h

namespace containers {

// A copy-only (not moveable) class that holds an integer. This is designed for
// testing containers. See also MoveOnlyInt.
class CopyOnlyInt {
 public:
  explicit CopyOnlyInt(int data = 1) : data_(data) {}
  CopyOnlyInt(const CopyOnlyInt& other) : data_(other.data_) { }
  ~CopyOnlyInt() { data_ = 0; }

  friend bool operator==(const CopyOnlyInt& lhs, const CopyOnlyInt& rhs) {
    return lhs.data_ == rhs.data_;
  }

  friend bool operator!=(const CopyOnlyInt& lhs, const CopyOnlyInt& rhs) {
    return !operator==(lhs, rhs);
  }

  friend bool operator<(const CopyOnlyInt& lhs, const CopyOnlyInt& rhs) {
    return lhs.data_ < rhs.data_;
  }

  friend bool operator>(const CopyOnlyInt& lhs, const CopyOnlyInt& rhs) {
    return rhs < lhs;
  }

  friend bool operator<=(const CopyOnlyInt& lhs, const CopyOnlyInt& rhs) {
    return !(rhs < lhs);
  }

  friend bool operator>=(const CopyOnlyInt& lhs, const CopyOnlyInt& rhs) {
    return !(lhs < rhs);
  }

  int data() const { return data_; }

 private:
  volatile int data_;

  CopyOnlyInt(CopyOnlyInt&&) = delete;
  CopyOnlyInt& operator=(CopyOnlyInt&) = delete;
};

// A move-only class that holds an integer. This is designed for testing
// containers. See also CopyOnlyInt.
class MoveOnlyInt {
 public:
  explicit MoveOnlyInt(int data = 1) : data_(data) {}
  MoveOnlyInt(MoveOnlyInt&& other) : data_(other.data_) { other.data_ = 0; }
  ~MoveOnlyInt() { data_ = 0; }

  MoveOnlyInt& operator=(MoveOnlyInt&& other) {
    data_ = other.data_;
    other.data_ = 0;
    return *this;
  }

  friend bool operator==(const MoveOnlyInt& lhs, const MoveOnlyInt& rhs) {
    return lhs.data_ == rhs.data_;
  }

  friend bool operator!=(const MoveOnlyInt& lhs, const MoveOnlyInt& rhs) {
    return !operator==(lhs, rhs);
  }

  friend bool operator<(const MoveOnlyInt& lhs, int rhs) {
    return lhs.data_ < rhs;
  }

  friend bool operator<(int lhs, const MoveOnlyInt& rhs) {
    return lhs < rhs.data_;
  }

  friend bool operator<(const MoveOnlyInt& lhs, const MoveOnlyInt& rhs) {
    return lhs.data_ < rhs.data_;
  }

  friend bool operator>(const MoveOnlyInt& lhs, const MoveOnlyInt& rhs) {
    return rhs < lhs;
  }

  friend bool operator<=(const MoveOnlyInt& lhs, const MoveOnlyInt& rhs) {
    return !(rhs < lhs);
  }

  friend bool operator>=(const MoveOnlyInt& lhs, const MoveOnlyInt& rhs) {
    return !(lhs < rhs);
  }

  int data() const { return data_; }

 private:
  volatile int data_;

  MoveOnlyInt(const MoveOnlyInt&) = delete;
  MoveOnlyInt& operator=(const MoveOnlyInt&) = delete;
};

}  // namespace containers

#endif  // LIB_CONTAINERS_CPP_TEST_OBJECTS_H_
