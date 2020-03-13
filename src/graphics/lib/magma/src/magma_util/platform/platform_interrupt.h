// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_INTERRUPT_H
#define PLATFORM_INTERRUPT_H

#include "magma_util/macros.h"

namespace magma {

// Created from a PlatformPciDevice.
class PlatformInterrupt {
 public:
  PlatformInterrupt() {}

  virtual ~PlatformInterrupt() {}

  virtual void Signal() = 0;
  virtual bool Wait() = 0;
  virtual void Complete() = 0;

  virtual uint64_t GetMicrosecondsSinceLastInterrupt() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(PlatformInterrupt);
};

}  // namespace magma

#endif  // PLATFORM_INTERRUPT_H
