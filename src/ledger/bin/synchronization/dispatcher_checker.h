// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_SYNCHRONIZATION_DISPATCHER_CHECKER_H_
#define SRC_LEDGER_BIN_SYNCHRONIZATION_DISPATCHER_CHECKER_H_

#include <lib/async/default.h>


namespace ledger {

// A simple class that records the identity of the default dispatcher for the thread that it was
// created on, and at later points can tell if the current thread's default dispatcher is the same
// as its creation thread's. This class is thread-safe.
//
// Note that this class is not checking the "current dispatcher", because this information is not
// available in general. It is still useful when the following conditions are met:
// - each dispatcher is bound to at most one thread,
// - every thread (except at most one) has a default dispatcher set,
// - the default dispatcher for a thread does not change after creation.
// Under those assumptions (which hold in Ledger), this class can be used to detect concurrency
// issues in addition to ThreadChecker. It has the benefit of finding issues even in unit tests,
// where all dispatchers are run on the same thread to emulate deterministic multithreading (thus
// making ThreadChecker useless).
class DispatcherChecker final {
 public:
  DispatcherChecker() : self_(async_get_default_dispatcher()) {}

  // Not copyable or movable.
  DispatcherChecker(const DispatcherChecker&) = delete;
  DispatcherChecker& operator=(const DispatcherChecker&) = delete;

  // Returns true if the current default dispatcher is the same as the default dispatcher when this
  // object was created and false otherwise.
  bool IsCreationDispatcherCurrent() const;

 private:
  async_dispatcher_t* const self_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_SYNCHRONIZATION_DISPATCHER_CHECKER_H_
