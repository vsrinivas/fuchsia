// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Lock Dependency Tracking Runtime API
//
// Systems must provide an implementation of the following functions to support
// integration of the lock validator into the runtime environment.
//

#pragma once

#include <stdint.h>

namespace lockdep {

// Forward declarations.
class AcquiredLockEntry;
class ThreadLockState;
class LockClassState;
enum class LockResult : uint8_t;

// System-defined hook to report detected lock validation failures.
extern void SystemLockValidationError(AcquiredLockEntry* lock_entry,
                                      AcquiredLockEntry* conflicting_entry,
                                      ThreadLockState* state,
                                      void* caller_address,
                                      void* caller_frame,
                                      LockResult result);

// System-defined hook to abort the program due to a fatal lock violation.
extern void SystemLockValidationFatal(AcquiredLockEntry* lock_entry,
                                      ThreadLockState* state,
                                      void* caller_address,
                                      void* caller_frame,
                                      LockResult result);

// System-defined hook to report detection of a circular lock dependency.
extern void SystemCircularLockDependencyDetected(LockClassState* connected_set_root);

// System-defined hook that returns the ThreadLockState instance for the current
// thread.
extern ThreadLockState* SystemGetThreadLockState();

// System-defined hook that initializes the ThreadLockState for the current thread.
extern void SystemInitThreadLockState(ThreadLockState* state);

// System-defined hook that triggers a loop detection pass. In response to this
// event the implementation must trigger a call lockdep::LoopDetectionPass() on
// a separate, dedicated or non-reentrant worker thread. Non-reentrancy is a
// hard requirement as lockdep::LoopDetectionPass() mutates non-thread safe
// state. An implementation may add hysteresis to prevent too many passes in a
// given time interval.
extern void SystemTriggerLoopDetection();

} // namespace lockdep
