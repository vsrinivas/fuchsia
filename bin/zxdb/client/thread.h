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
  explicit Thread(Session* session);
  ~Thread() override;

  // Guaranteed non-null.
  virtual Process* GetProcess() const = 0;

  virtual uint64_t GetKoid() const = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace zxdb
