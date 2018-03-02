// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class Process;

class Thread : public ClientObject {
 public:
  ~Thread() override;

  Process* process() const { return process_; }
  size_t thread_id() const { return thread_id_; }
  uint64_t koid() const { return koid_; }

 private:
  friend Process;

  // Only the Process can create this object.
  Thread(Process* process, size_t thread_id, uint64_t koid);

  Process* process_;  // Process that owns us.
  size_t thread_id_;
  uint64_t koid_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace zxdb
