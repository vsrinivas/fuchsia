// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <map>
#include <memory>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/bin/zxdb/client/process_observer.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/observer_list.h"

namespace zxdb {

class Target;
class Thread;

class Process : public ClientObject {
 public:
  Process(Session* session);
  ~Process() override;

  void AddObserver(ProcessObserver* observer);
  void RemoveObserver(ProcessObserver* observer);

  // Returns the target associated with this process. Guaranteed non-null.
  virtual Target* GetTarget() const = 0;

  // The Process koid is guaranteed non-null.
  virtual uint64_t GetKoid() const = 0;

  // Returns all threads in the process. This is a as of the last update from
  // the system. If the program is currently running, the actual threads may be
  // different since it can be asynchonously creating and destroying them. The
  // pointers will only be valid until you return to the message loop.
  virtual std::vector<Thread*> GetThreads() const = 0;

  // Notifications from the agent that a thread has started or exited.
  virtual void OnThreadStarting(uint64_t thread_koid) = 0;
  virtual void OnThreadExiting(uint64_t thread_koid) = 0;

 protected:
  fxl::ObserverList<ProcessObserver>& observers() { return observers_; }

 private:
  fxl::ObserverList<ProcessObserver> observers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Process);
};

}  // namespace zxdb
