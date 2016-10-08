// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <launchpad/launchpad.h>
#include <magenta/types.h>

#include "lib/ftl/macros.h"

#include "thread.h"

namespace debugserver {

// Represents an inferior process that the stub is currently attached to.
class Process final {
 public:
  // TODO(armansito): Add a different constructor later for attaching to an
  // already running process.
  explicit Process(const std::vector<std::string>& argv);
  ~Process();

  // Creates and initializes the inferior process but does not start it. Returns
  // false if there is an error.
  bool Initialize();

  // Binds an exception port for receiving exceptions from the inferior process.
  // Returns true on success, or false in the case of an error. Initialize MUST
  // be called successfully before calling Attach().
  bool Attach();

  // Starts running the process. Returns false in case of an error. Initialize()
  // and Attach() MUST be called successfully before calling Start().
  bool Start();

  // Returns true if the process has been started via a call to Start();
  bool started() const { return started_; }

  // Returns the thread with the thread ID |thread_id| that's owned by this
  // process. Returns nullptr if no such thread exists. The returned pointer is
  // owned and managed by this Process instance.
  Thread* FindThreadById(mx_koid_t thread_id);

  // Returns an arbitrary thread that is owned by this process. This picks the
  // first thread that is returned from mx_object_get_info for the
  // MX_INFO_PROCESS_THREADS topic or the first thread from previously
  // initialized threads.
  Thread* PickOneThread();

 private:
  Process() = default;

  // The argv that this process was initialized with.
  std::vector<std::string> argv_;

  // The launchpad_t instance used to bootstrap and run the process. The Process
  // owns this instance and holds on to it until it gets destroyed.
  launchpad_t* launchpad_;

  // The debug-capable handle that we use to invoke mx_debug_* syscalls.
  mx_handle_t debug_handle_;

  // True, if the inferior has been run via a call to Start().
  bool started_;

  // The threads owned by this process. This is map is populated lazily when
  // threads are requested through the corresponding public methods (e.g.
  // FindThreadById).
  // TODO(armansito): We need to watch the handles somehow to find out when a
  // thread goes away so it can be removed from this list.
  std::unordered_map<mx_koid_t, std::unique_ptr<Thread>> threads_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Process);
};

}  // namespace debugserver
