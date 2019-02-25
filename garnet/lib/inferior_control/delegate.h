// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Delegate interface for processing Process life-time events.
// TODO(PT-105): Passing of |eport| will need to change when exception
// handling changes to include an "exception token". It is currently passed
// because it is needed as an argument to |zx_task_resume_from_exception()|,
// that is the only reason for passing it and its only intended use.

#ifndef GARNET_LIB_INFERIOR_CONTROL_DELEGATE_H_
#define GARNET_LIB_INFERIOR_CONTROL_DELEGATE_H_

#include <zircon/syscalls/exception.h>

namespace inferior_control {

class Server;
class Process;
class Thread;

class Delegate {
 public:
  Delegate(Server* server) : server_(server) {}
  virtual ~Delegate() = default;

  // Called when a new thread that is part of this process has been started.
  // This is indicated by ZX_EXCP_THREAD_STARTING.
  virtual void OnThreadStarting(Process* process, Thread* thread,
                                zx_handle_t eport,
                                const zx_exception_context_t& context);

  // Called when |thread| has exited (ZX_EXCP_THREAD_EXITING).
  virtual void OnThreadExiting(Process* process, Thread* thread,
                               zx_handle_t eport,
                               const zx_exception_context_t& context);

  // Called when |thread| suspends, resumes, and terminates.
  // Some apps don't need to do anything with these so they're not
  // pure-virtual.
  virtual void OnThreadSuspension(Thread* thread);
  virtual void OnThreadResumption(Thread* thread);
  virtual void OnThreadTermination(Thread* thread);

  // Called when |process| has exited.
  virtual void OnProcessTermination(Process* process);

  // Called when the kernel reports an architectural exception.
  virtual void OnArchitecturalException(
      Process* process, Thread* thread, zx_handle_t eport,
      zx_excp_type_t type, const zx_exception_context_t& context);

  // Called when |thread| has gets a synthetic exception
  // (e.g., ZX_EXCP_POLICY_ERROR) that is akin to an architectural
  // exception: the program got an error and by default crashes.
  virtual void OnSyntheticException(
      Process* process, Thread* thread, zx_handle_t eport,
      zx_excp_type_t type, const zx_exception_context_t& context);

 protected:
  // Non-owning.
  Server* server_;
};

}  // namespace inferior_control

#endif  // GARNET_LIB_INFERIOR_CONTROL_DELEGATE_H_
