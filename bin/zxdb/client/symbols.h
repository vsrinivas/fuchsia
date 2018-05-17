// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <functional>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class Symbols : public ClientObject {
 public:
  explicit Symbols(Session* session);
  ~Symbols() override;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Symbols);
};

}  // namespace zxdb
