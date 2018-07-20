// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/symbols/target_symbols_impl.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class ProcessImpl;
class SystemImpl;

class TargetImpl : public Target {
 public:
  // The system owns this object and will outlive it.
  explicit TargetImpl(SystemImpl* system);
  ~TargetImpl() override;

  SystemImpl* system() { return system_; }
  ProcessImpl* process() { return process_.get(); }
  TargetSymbolsImpl* symbols() { return &symbols_; }

  // Allocates a new target with the same settings as this one. This isn't
  // a real copy, because any process information is not cloned.
  std::unique_ptr<TargetImpl> Clone(SystemImpl* system);

  // Tests can use this to create a target for mocking purposes without making
  // any IPC. To destroy call ImplicitlyDetach().
  void CreateProcessForTesting(uint64_t koid, const std::string& process_name);

  // Removes the process from this target without making any IPC calls. This
  // can be used to clean up after a CreateProcessForTesting(), and during
  // final shutdown. In final shutdown, we assume anything still left running
  // will continue running as-is and just clean up local references.
  //
  // If the process is not running, this will do nothing.
  void ImplicitlyDetach();

  // Target implementation:
  State GetState() const override;
  Process* GetProcess() const override;
  const TargetSymbols* GetSymbols() const override;
  const std::vector<std::string>& GetArgs() const override;
  void SetArgs(std::vector<std::string> args) override;
  void Launch(Callback callback) override;
  void Kill(Callback callback) override;
  void Attach(uint64_t koid, Callback callback) override;
  void Detach(Callback callback) override;
  void OnProcessExiting(int return_code) override;

 private:
  static void OnLaunchOrAttachReplyThunk(fxl::WeakPtr<TargetImpl> target,
                                         Callback callback, const Err& err,
                                         uint64_t koid, uint32_t status,
                                         const std::string& process_name);
  void OnLaunchOrAttachReply(Callback callback, const Err& err, uint64_t koid,
                             uint32_t status, const std::string& process_name);

  void OnKillOrDetachReply(const Err& err, uint32_t status, Callback callback);

  SystemImpl* system_;  // Owns |this|.

  State state_ = kNone;

  std::vector<std::string> args_;

  // Associated process if there is one.
  std::unique_ptr<ProcessImpl> process_;

  TargetSymbolsImpl symbols_;

  fxl::WeakPtrFactory<TargetImpl> impl_weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TargetImpl);
};

}  // namespace zxdb
