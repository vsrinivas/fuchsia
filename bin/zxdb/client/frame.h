// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class Location;
class Thread;

class Frame : public ClientObject {
 public:
  explicit Frame(Session* session);
  virtual ~Frame();

  // Guaranteed non-null.
  virtual Thread* GetThread() const = 0;

  // Returns the location of the stack frame code.
  virtual const Location& GetLocation() const = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Frame);
};

}  // namespace zxdb
