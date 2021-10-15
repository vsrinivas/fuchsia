// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTER_IO_H
#define REGISTER_IO_H

#include <memory>

#include "magma_util/macros.h"
#include "platform_mmio.h"

namespace magma {

// RegisterIo wraps mmio access.
class RegisterIo {
 public:
  RegisterIo(std::unique_ptr<magma::PlatformMmio> mmio);

  // TODO(fxbug.dev/86716): Swap argument order and remove "Flipped" from name.
  void Write32Flipped(uint32_t offset, uint32_t val) {
    mmio_->Write32Flipped(offset, val);
    if (hook_)
      hook_->Write32Flipped(offset, val);
  }

  uint32_t Read32(uint32_t offset) {
    uint32_t val = mmio_->Read32(offset);
    if (hook_)
      hook_->Read32Flipped(offset, val);
    return val;
  }

  uint64_t Read64(uint32_t offset) {
    uint64_t val = mmio_->Read64(offset);
    if (hook_)
      hook_->Read64Flipped(offset, val);
    return val;
  }

  magma::PlatformMmio* mmio() { return mmio_.get(); }

  class Hook {
   public:
    virtual ~Hook() = 0;
    // TODO(fxbug.dev/86716): Swap argument order and remove "Flipped" from name.
    virtual void Write32Flipped(uint32_t offset, uint32_t val) = 0;
    virtual void Read32Flipped(uint32_t offset, uint32_t val) = 0;
    virtual void Read64Flipped(uint32_t offset, uint64_t val) = 0;
  };

  void InstallHook(std::unique_ptr<Hook> hook) {
    DASSERT(!hook_);
    hook_ = std::move(hook);
  }

  Hook* hook() { return hook_.get(); }

 private:
  std::unique_ptr<magma::PlatformMmio> mmio_;
  std::unique_ptr<Hook> hook_;
};

}  // namespace magma

#endif  // REGISTER_IO_H
