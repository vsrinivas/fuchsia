// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FORCEWAKE_H
#define FORCEWAKE_H

#include <optional>

#include "magma_util/macros.h"
#include "msd_intel_register_io.h"
#include "registers.h"

class ForceWake {
 public:
  ForceWake(MsdIntelRegisterIo* register_io, uint32_t device_id);

  registers::ForceWakeStatus* get_status_register(ForceWakeDomain domain) {
    switch (domain) {
      case ForceWakeDomain::RENDER:
        return status_render_ ? &(*status_render_) : nullptr;
      case ForceWakeDomain::GEN9_MEDIA:
        return status_gen9_media_ ? &(*status_gen9_media_) : nullptr;
      case ForceWakeDomain::GEN12_VDBOX0:
        return status_gen12_vdbox0_ ? &(*status_gen12_vdbox0_) : nullptr;
    }
  }

  bool is_active_cached(ForceWakeDomain domain) {
    return (get_status_register(domain)->status() & (1 << kThreadShift)) != 0;
  }

  static uint32_t get_request_offset(ForceWakeDomain domain) {
    switch (domain) {
      case ForceWakeDomain::RENDER:
        return registers::ForceWakeRequest::kRenderOffset;
      case ForceWakeDomain::GEN9_MEDIA:
        return registers::ForceWakeRequest::kGen9MediaOffset;
      case ForceWakeDomain::GEN12_VDBOX0:
        return registers::ForceWakeRequest::kGen12Vdbox0Offset;
    }
  }

  bool IsActive(MsdIntelRegisterIo* reg_io, ForceWakeDomain domain);
  bool Reset(MsdIntelRegisterIo* reg_io, ForceWakeDomain domain);
  bool Request(MsdIntelRegisterIo* reg_io, ForceWakeDomain domain);
  bool Release(MsdIntelRegisterIo* reg_io, ForceWakeDomain domain);

  static constexpr uint32_t kThreadShift = 0;
  static constexpr uint32_t kMaxRetries = 20;
  static constexpr uint32_t kRetryDelayUs = 10;
  static constexpr uint32_t kRetryMaxUs = kMaxRetries * kRetryDelayUs;

 private:
  bool Wait(MsdIntelRegisterIo* register_io, ForceWakeDomain domain, bool set);

  std::optional<registers::ForceWakeStatus> status_render_;
  std::optional<registers::ForceWakeStatus> status_gen9_media_;
  std::optional<registers::ForceWakeStatus> status_gen12_vdbox0_;
};

#endif  // FORCEWAKE_H
