// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_register_io.h"

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "platform_trace.h"
#include "registers.h"

MsdIntelRegisterIo::MsdIntelRegisterIo(Owner* owner, std::unique_ptr<magma::PlatformMmio> mmio,
                                       uint32_t device_id)
    : owner_(owner), register_io_(std::move(mmio)) {
  if (DeviceId::is_gen12(device_id)) {
    forcewake_map_ = &forcewake_map_gen12_;
  }
}

std::shared_ptr<ForceWakeDomain> MsdIntelRegisterIo::GetForceWakeToken(ForceWakeDomain domain) {
  // Ensure forcewake has been activated before we offer the first token.
  if (forcewake_token_count(domain) == 0) {
    DASSERT(owner_);
    bool enabled = owner_->IsForceWakeDomainActive(domain);
    DASSERT(enabled);
  }

  DASSERT(static_cast<size_t>(domain) < per_forcewake_.size());

  per_forcewake_[static_cast<int>(domain)].last_request_time = std::chrono::steady_clock::now();

  return per_forcewake_[static_cast<int>(domain)].token;
}

std::chrono::steady_clock::duration MsdIntelRegisterIo::GetForceWakeReleaseTimeout(
    ForceWakeDomain forcewake_domain, uint64_t max_release_timeout_ms,
    std::chrono::steady_clock::time_point now) {
  // Don't timeout if a forcewake token is still held
  if (forcewake_token_count(forcewake_domain) > 0)
    return std::chrono::steady_clock::duration::max();

  if (!owner_->IsForceWakeDomainActive(forcewake_domain))
    return std::chrono::steady_clock::duration::max();

  DASSERT(static_cast<size_t>(forcewake_domain) < per_forcewake_.size());

  auto last_request_time = per_forcewake_[static_cast<int>(forcewake_domain)].last_request_time;
  DASSERT(last_request_time != std::chrono::steady_clock::time_point::max());

  return last_request_time + std::chrono::milliseconds(max_release_timeout_ms) - now;
}

void MsdIntelRegisterIo::CheckForcewake(uint32_t register_offset) {
  // Skip the forcewake registers
  switch (register_offset) {
    case registers::ForceWakeRequest::kRenderOffset:
    case registers::ForceWakeRequest::kGen9MediaOffset:
    case registers::ForceWakeRequest::kGen12Vdbox0Offset:
    case registers::ForceWakeStatus::kRenderStatusOffset:
    case registers::ForceWakeStatus::kGen9MediaStatusOffset:
    case registers::ForceWakeStatus::kGen12Vdbox0StatusOffset:
      return;
  }

  if (!forcewake_map_ || forcewake_map_->empty())
    return;

  TRACE_DURATION("magma", "CheckForcewake");

  auto iter = forcewake_map_->upper_bound(register_offset);
  if (iter == forcewake_map_->begin())
    return;

  iter = std::prev(iter);
  DASSERT(iter->first == iter->second.start_offset);

  if (const Range& range = iter->second;
      register_offset >= range.start_offset && register_offset <= range.end_offset) {
    CheckForcewakeForRange(range, register_offset);
  }
}

void MsdIntelRegisterIo::CheckForcewakeForRange(const Range& range, uint32_t register_offset) {
  if (forcewake_active_check_for_test_) {
    owner_->IsForceWakeDomainActive(range.forcewake_domain);
  }
  if (forcewake_token_count(range.forcewake_domain) == 0) {
    MAGMA_LOG(WARNING, "Access missing forcewake: register 0x%x domain %d range 0x%x - 0x%x",
              register_offset, range.forcewake_domain, range.start_offset, range.end_offset);
    DASSERT(false);
  }
}

// static
constexpr std::pair<uint32_t, MsdIntelRegisterIo::Range> Entry(uint32_t start, uint32_t end,
                                                               ForceWakeDomain domain) {
  return {start, {start, end, domain}};
}

// From:
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol13-generalassets.pdf
// p.1
// Does not include GT or empty regions.
// Commented lines refer to engines that may be supported eventually.
// clang-format off
const std::map<uint32_t, MsdIntelRegisterIo::Range> MsdIntelRegisterIo::forcewake_map_gen12_ = {
    Entry(0x2000, 0x26FF, ForceWakeDomain::RENDER),
    Entry(0x2800, 0x2AFF, ForceWakeDomain::RENDER),
    Entry(0x3000, 0x3FFF, ForceWakeDomain::RENDER),
    Entry(0x5200, 0x52FF, ForceWakeDomain::RENDER),
    Entry(0x5300, 0x53FF, ForceWakeDomain::RENDER),
    Entry(0x5500, 0x55FF, ForceWakeDomain::RENDER),
    Entry(0x6000, 0x6FFF, ForceWakeDomain::RENDER),
    Entry(0x7000, 0x7FFF, ForceWakeDomain::RENDER),
    Entry(0x8140, 0x814F, ForceWakeDomain::RENDER),
    Entry(0x8150, 0x815F, ForceWakeDomain::RENDER),
    Entry(0x8300, 0x84FF, ForceWakeDomain::RENDER),
    Entry(0x94D0, 0x951F, ForceWakeDomain::RENDER),
    Entry(0x9520, 0x955F, ForceWakeDomain::RENDER),
    Entry(0xB000, 0xB0FF, ForceWakeDomain::RENDER),
    Entry(0xB100, 0xB3FF, ForceWakeDomain::RENDER),
    Entry(0xD800, 0xD8FF, ForceWakeDomain::RENDER),
    Entry(0xDC00, 0xDDFF, ForceWakeDomain::RENDER),
    Entry(0xDE80, 0xDEFF, ForceWakeDomain::RENDER),
    Entry(0xDF00, 0xDFFF, ForceWakeDomain::RENDER),
    Entry(0xE000, 0xE0FF, ForceWakeDomain::RENDER),
    Entry(0xE100, 0xE1FF, ForceWakeDomain::RENDER),
    Entry(0xE200, 0xE3FF, ForceWakeDomain::RENDER),
    Entry(0xE400, 0xE7FF, ForceWakeDomain::RENDER),
    Entry(0xE800, 0xE8FF, ForceWakeDomain::RENDER),
    Entry(0x14800, 0x14FFF, ForceWakeDomain::RENDER),
    Entry(0x16E00, 0x16FFF, ForceWakeDomain::RENDER),
    Entry(0x17000, 0x17FFF, ForceWakeDomain::RENDER),
    Entry(0x18000, 0x19FFF, ForceWakeDomain::RENDER),
    Entry(0x1A000, 0x1BFFF, ForceWakeDomain::RENDER),
    Entry(0x20000, 0x20FFF, ForceWakeDomain::GEN12_VDBOX0),
    // Entry(0x21000, 0x21FFF, ForceWakeDomain::GEN12_VDBOX2),
    Entry(0x24A00, 0x24A7F, ForceWakeDomain::RENDER),
    Entry(0x25600, 0x2567F, ForceWakeDomain::GEN12_VDBOX0),
    // Entry(0x25680, 0x256FF, ForceWakeDomain::GEN12_VDBOX2),
    Entry(0x25A00, 0x25A7F, ForceWakeDomain::GEN12_VDBOX0),
    // Entry(0x25A80, 0x25AFF, ForceWakeDomain::GEN12_VDBOX2),
    Entry(0x1C0000, 0x1C07FF, ForceWakeDomain::GEN12_VDBOX0),
    Entry(0x1C0800, 0x1C0FFF, ForceWakeDomain::GEN12_VDBOX0),
    Entry(0x1C1000, 0x1C1FFF, ForceWakeDomain::GEN12_VDBOX0),
    Entry(0x1C2000, 0x1C27FF, ForceWakeDomain::GEN12_VDBOX0),
    Entry(0x1C2800, 0x1C2AFF, ForceWakeDomain::GEN12_VDBOX0),
    Entry(0x1C2B00, 0x1C2BFF, ForceWakeDomain::GEN12_VDBOX0),
    Entry(0x1C2D00, 0x1C2DFF, ForceWakeDomain::GEN12_VDBOX0),
    Entry(0x1C3F00, 0x1C3FFF, ForceWakeDomain::GEN12_VDBOX0),
    // Entry(0x1C8000, 0x1C9FFF, ForceWakeDomain::GEN12_VEBOX0),
    // Entry(0x1CA000, 0x1CA0FF, ForceWakeDomain::GEN12_VEBOX0),
    // Entry(0x1CBF00, 0x1CBFFF, ForceWakeDomain::GEN12_VEBOX0),
    Entry(0x1CC000, 0x1CCFFF, ForceWakeDomain::GEN12_VDBOX0),
    // Entry(0x1D0000, 0x1D07FF, ForceWakeDomain::GEN12_VDBOX2),
    // Entry(0x1D0800, 0x1D0FFF, ForceWakeDomain::GEN12_VDBOX2),
    // Entry(0x1D1000, 0x1D1FFF, ForceWakeDomain::GEN12_VDBOX2),
    // Entry(0x1D2000, 0x1D27FF, ForceWakeDomain::GEN12_VDBOX2),
    // Entry(0x1D2800, 0x1D2AFF, ForceWakeDomain::GEN12_VDBOX2),
    // Entry(0x1D2B00, 0x1D2BFF, ForceWakeDomain::GEN12_VDBOX2),
    // Entry(0x1D2D00, 0x1D2DFF, ForceWakeDomain::GEN12_VDBOX2),
    // Entry(0x1D3F00, 0x1D3FFF, ForceWakeDomain::GEN12_VDBOX2),
};
// clang-format on
