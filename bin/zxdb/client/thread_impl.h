// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/thread.h"

namespace zxdb {

class ProcessImpl;

class ThreadImpl : public Thread {
 public:
  ThreadImpl(ProcessImpl* process, const debug_ipc::ThreadRecord& record);
  ~ThreadImpl() override;

  // Thread implementation:
  Process* GetProcess() const override;
  uint64_t GetKoid() const override;
  const std::string& GetName() const override;
  debug_ipc::ThreadRecord::State GetState() const override;
  void Continue() override;

  // Updates the thread metadata with new state from the agent.
  void SetMetadata(const debug_ipc::ThreadRecord& record);

  // Notification from the agent of an exception.
  void OnException(const debug_ipc::NotifyException& notify);

 private:
  ProcessImpl* const process_;
  uint64_t koid_;
  std::string name_;
  debug_ipc::ThreadRecord::State state_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadImpl);
};

}  // namespace zxdb
