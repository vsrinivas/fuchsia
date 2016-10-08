// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls-types.h>
#include <magenta/types.h>

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace debugserver {

class Process;

// Represents a thread that is owned by a Process instance.
class Thread final {
 public:
  Thread(Process* process, mx_handle_t debug_handle, mx_koid_t thread_id);
  ~Thread();

  mx_handle_t debug_handle() const { return debug_handle_; }
  mx_koid_t thread_id() const { return thread_id_; }

  // Returns a weak pointer to this Thread instance.
  ftl::WeakPtr<Thread> AsWeakPtr();

  // TODO(armansito): Bind an exception port to this thread.

 private:
  // The owning process.
  Process* process_;  // weak

  // The debug-capable handle that we use to invoke mx_debug_* syscalls.
  mx_handle_t debug_handle_;

  // The thread ID (also the kernel object ID) associated with this thread.
  mx_koid_t thread_id_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  ftl::WeakPtrFactory<Thread> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace debugserver
