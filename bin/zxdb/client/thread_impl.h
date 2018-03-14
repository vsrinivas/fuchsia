// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/thread.h"

namespace zxdb {

class ProcessImpl;

class ThreadImpl : public Thread {
 public:
  ThreadImpl(ProcessImpl* process, uint64_t koid, const std::string& name);
  ~ThreadImpl() override;

  // Updates the name. See GetName (implemented from Thread) for getter.
  void set_name(const std::string& name) { name_ = name; }

  // Thread implementation:
  Process* GetProcess() const override;
  uint64_t GetKoid() const override;
  const std::string& GetName() const override;

 private:
  ProcessImpl* const process_;
  uint64_t koid_;
  std::string name_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadImpl);
};

}  // namespace zxdb
