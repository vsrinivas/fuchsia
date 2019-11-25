// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_TARGET_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_TARGET_H_

#include "src/developer/debug/zxdb/client/target.h"

namespace zxdb {

class MockTarget : public Target {
 public:
  MockTarget(Session* session) : Target(session) {}
  ~MockTarget() override {}

  // Sets the state to running and saves the process pointer. Does not take ownership of the
  // pointer.
  void SetRunningProcess(Process* process);

  // Sets the value returned by GetSymbols(). Does not take ownership.
  void set_symbols(TargetSymbols* symbols) { symbols_ = symbols; }

  // Target implementation.
  State GetState() const override { return state_; }
  Process* GetProcess() const override { return process_; }
  const TargetSymbols* GetSymbols() const override { return symbols_; }
  const std::vector<std::string>& GetArgs() const override { return args_; }
  void SetArgs(std::vector<std::string> args) override { args_ = args; }
  void Launch(Callback callback) override;
  void Kill(Callback callback) override;
  void Attach(uint64_t koid, Callback callback) override;
  void Detach(Callback callback) override;
  void OnProcessExiting(int return_code) override;

 private:
  State state_ = kNone;
  Process* process_ = nullptr;

  TargetSymbols* symbols_ = nullptr;

  std::vector<std::string> args_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_MOCK_TARGET_H_
