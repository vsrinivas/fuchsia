// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/process.h>

#include "garnet/public/lib/fxl/macros.h"

class DebuggedProcess {
 public:
  DebuggedProcess(zx_koid_t koid, zx::process proc);
  ~DebuggedProcess();

  zx_koid_t koid() const { return koid_; }
  const zx::process& process() const { return process_; }

 private:
  zx_koid_t koid_;
  zx::process process_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedProcess);
};
