// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/target.h"

namespace zxdb {

class TargetObserver {
 public:
  // Reason for destroying a process object.
  enum class DestroyReason {
    kExit,
    kDetach,
    kKill
  };

  // The process could have been newly launched or attached to an existing
  // process.
  virtual void DidCreateProcess(Target* target, Process* process) {}

  // Called after detaching from or destroying a process. The Process object
  // will no longer exist. The exit code will only have meaning when reason ==
  // kExit, otherwise it will be 0.
  virtual void DidDestroyProcess(Target* target, DestroyReason reason,
                                 int exit_code) {}
};

}  // namespace zxdb
