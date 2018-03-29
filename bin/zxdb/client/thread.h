// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/bin/zxdb/client/thread_observer.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/observer_list.h"

namespace zxdb {

class Process;

class Thread : public ClientObject {
 public:
  explicit Thread(Session* session);
  ~Thread() override;

  void AddObserver(ThreadObserver* observer);
  void RemoveObserver(ThreadObserver* observer);

  // Guaranteed non-null.
  virtual Process* GetProcess() const = 0;

  virtual uint64_t GetKoid() const = 0;
  virtual const std::string& GetName() const = 0;
  virtual debug_ipc::ThreadRecord::State GetState() const = 0;

  // Applies only to this thread (other threads will continue to run or not run
  // as they were previously).
  virtual void Pause() = 0;
  virtual void Continue() = 0;
  virtual void StepInstruction() = 0;

 protected:
  fxl::ObserverList<ThreadObserver>& observers() { return observers_; }

 private:
  fxl::ObserverList<ThreadObserver> observers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace zxdb
