// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_REGISTER_IO_H
#define MSD_INTEL_REGISTER_IO_H

#include <map>

#include "device_id.h"
#include "magma_util/register_io.h"
#include "types.h"

// Wraps the common magma::RegisterIo so we can intercept reads and writes and perform forcewake
// checks.
class MsdIntelRegisterIo {
 public:
  class Owner {
   public:
    virtual bool IsForceWakeDomainActive(ForceWakeDomain domain) = 0;
  };

  struct Range {
    uint32_t start_offset;
    uint32_t end_offset;  // inclusive
    ForceWakeDomain forcewake_domain;
  };

  MsdIntelRegisterIo(Owner* owner, std::unique_ptr<magma::PlatformMmio> mmio, uint32_t device_id);

  // Should only be used for unit testing.
  explicit MsdIntelRegisterIo(std::unique_ptr<magma::PlatformMmio> mmio)
      : MsdIntelRegisterIo(nullptr, std::move(mmio), /*device_id=*/0) {}

  magma::PlatformMmio* mmio() { return register_io_.mmio(); }

  void Write32(uint32_t val, uint32_t offset) {
    CheckForcewake(offset);
    return register_io_.Write32(val, offset);
  }

  uint32_t Read32(uint32_t offset) {
    CheckForcewake(offset);
    return register_io_.Read32(offset);
  }

  uint64_t Read64(uint32_t offset) {
    CheckForcewake(offset);
    return register_io_.Read64(offset);
  }

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

  void CheckForcewake(uint32_t register_offset);
  void CheckForcewakeForRange(const Range& range, uint32_t register_offset);

 private:
  Owner* owner_;
  magma::RegisterIo register_io_;
  const std::map<uint32_t, Range>* forcewake_map_ = nullptr;

  static const std::map<uint32_t, Range> forcewake_map_gen12_;
};

#endif  // MSD_INTEL_REGISTER_IO_H
