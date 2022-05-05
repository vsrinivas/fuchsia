// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ASYNC_CPP_SEQUENCE_CHECKER_H_
#define LIB_ASYNC_CPP_SEQUENCE_CHECKER_H_

#include <assert.h>
#include <lib/async/sequence_id.h>
#include <lib/stdcompat/variant.h>
#include <zircon/compiler.h>

#include <thread>

namespace async {

// A simple class that records the identity of the sequence that it was created
// on, and at later points can tell if the current sequence is the same as its
// creation sequence. This class is thread-safe.
//
// In addition to providing an explicit check of the current sequence,
// |sequence_checker| complies with BasicLockable, checking the current sequence
// when |lock| is called. This allows static thread safety analysis to be used
// to ensure that resources are accessed in a context that is checked (at debug
// runtime) to ensure that it's running on the correct sequence:
//
//   class MyClass {
//    public:
//     MyClass(async_dispatcher_t* dispatcher) : sequence_checker_(dispatcher) {}
//     void Foo() {
//       std::lock_guard<sequence::sequence_checker> locker(sequence_checker_);
//       resource_ = 0;
//     }
//    private:
//     sequence::sequence_checker sequence_checker_;
//     int resource_ __TA_GUARDED(sequence_checker_);
//   };
//
// Note: |lock| checks the sequence in debug builds only.
//
// This class is useful for code that works exclusively with asynchronous
// runtimes that support sequences.
class __TA_CAPABILITY("mutex") sequence_checker final {
 public:
  // Constructs a sequence checker bound to the currently running sequence.
  // Panics if the current thread is not associated with a sequence.
  explicit sequence_checker(async_dispatcher_t* dispatcher);

  ~sequence_checker() = default;

  // Returns true if the current sequence is the sequence this object was
  // created on and false otherwise.
  bool is_sequence_valid() const;

  // Implementation of the BaseLockable requirement
  void lock() const __TA_ACQUIRE() { assert(is_sequence_valid()); }

  void unlock() const __TA_RELEASE() {}

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  async_sequence_id_t self_ = {};
};

// A generalized |sequence_checker| that checks for synchronized access:
//
// - If the underlying asynchronous runtime supports sequences, performs the
//   same checks as |sequence_checker|.
// - If the underlying asynchronous runtime does not support sequences, performs
//   the same checks as a thread checker.
//
// This class is useful for code that must work with many asynchronous runtimes.
class __TA_CAPABILITY("mutex") synchronization_checker final {
 public:
  // Constructs a synchronization checker bound to the currently running
  // sequence. If |dispatcher| does not support sequences, fallback to thread
  // ID.
  explicit synchronization_checker(async_dispatcher_t* dispatcher);

  ~synchronization_checker() = default;

  // Returns true if the caller has synchronized access to the objects guarded
  // by this synchronization checker.
  bool is_synchronized() const;

  // Implementation of the BaseLockable requirement
  void lock() const __TA_ACQUIRE() { assert(is_synchronized()); }

  void unlock() const __TA_RELEASE() {}

 private:
  async_dispatcher_t* dispatcher_ = nullptr;
  cpp17::variant<std::thread::id, async_sequence_id_t> self_;
};

}  // namespace async

#endif  // LIB_ASYNC_CPP_SEQUENCE_CHECKER_H_
