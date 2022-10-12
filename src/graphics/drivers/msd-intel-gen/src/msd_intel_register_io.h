// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_REGISTER_IO_H
#define MSD_INTEL_REGISTER_IO_H

#include "magma_util/register_io.h"

// Wraps the common magma::RegisterIo so we can intercept reads and writes. Forcewake checks
// will be added here.
class MsdIntelRegisterIo {
 public:
  explicit MsdIntelRegisterIo(std::unique_ptr<magma::PlatformMmio> mmio)
      : register_io_(std::move(mmio)) {}

  magma::PlatformMmio* mmio() { return register_io_.mmio(); }

  void Write32(uint32_t val, uint32_t offset) { return register_io_.Write32(val, offset); }

  uint32_t Read32(uint32_t offset) { return register_io_.Read32(offset); }

  uint64_t Read64(uint32_t offset) { return register_io_.Read64(offset); }

  // For hwreg::RegisterBase::ReadFrom.
  template <class T>
  T Read(uint32_t offset) {
    if constexpr (sizeof(T) == sizeof(uint64_t)) {
      return Read64(offset);
    } else {
      static_assert(sizeof(T) == sizeof(uint32_t));
      return Read32(offset);
    }
  }

  template <class T>
  void Write(T val, uint32_t offset) {
    static_assert(sizeof(T) == sizeof(uint32_t));
    Write32(val, offset);
  }

  void InstallHook(std::unique_ptr<magma::RegisterIo::Hook> hook) {
    register_io_.InstallHook(std::move(hook));
  }

  magma::RegisterIo::Hook* hook() { return register_io_.hook(); }

 private:
  magma::RegisterIo register_io_;
};

#endif  // MSD_INTEL_REGISTER_IO_H
