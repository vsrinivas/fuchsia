// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/public/lib/fxl/macros.h"
#include "garnet/public/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Location;
class Thread;

class Frame : public ClientObject {
 public:
  explicit Frame(Session* session);
  virtual ~Frame();

  fxl::WeakPtr<Frame> GetWeakPtr();

  // Guaranteed non-null.
  virtual Thread* GetThread() const = 0;

  // Returns the location of the stack frame code. This will be symbolized.
  virtual const Location& GetLocation() const = 0;

  // Returns the program counter of this frame. This may be faster than
  // GetLocation().address() since it doesn't need to be symbolized.
  virtual uint64_t GetAddress() const = 0;

  // Returns the stack pointer at this location.
  virtual uint64_t GetStackPointer() const = 0;

 private:
  fxl::WeakPtrFactory<Frame> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Frame);
};

}  // namespace zxdb
