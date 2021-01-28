// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_factory_hw_policy_astro.h"

#include <lib/zx/eventpair.h>
#include <zircon/types.h>

#include <sdk/lib/syslog/cpp/macros.h>

namespace {

constexpr uint32_t kContiguousMemorySizeDecodersMax = 1;

bool IsSwDecoderAvailableInPlaceOfHwAllocatingOutputFromContiguousMemorySize(
    const fuchsia::mediacodec::CreateDecoder_Params& params) {
  ZX_DEBUG_ASSERT(params.has_input_details());
  ZX_DEBUG_ASSERT(params.input_details().has_mime_type());
  bool have_sw_decoder = (params.input_details().mime_type() == "video/h264");
  if (!have_sw_decoder) {
    return false;
  }
  bool allocating_output_from_contiguous_memory_size =
      !params.has_secure_output_mode() ||
      params.secure_output_mode() != fuchsia::mediacodec::SecureMemoryMode::ON;
  if (!allocating_output_from_contiguous_memory_size) {
    return false;
  }
  return true;
}

}  // namespace

CodecFactoryHwPolicyAstro::CodecFactoryHwPolicyAstro(Owner* owner) : CodecFactoryHwPolicy(owner) {}

bool CodecFactoryHwPolicyAstro::AdmitHwDecoder(
    const fuchsia::mediacodec::CreateDecoder_Params& params) {
  if (IsSwDecoderAvailableInPlaceOfHwAllocatingOutputFromContiguousMemorySize(params)) {
    // if the decoder will allocate buffers from contiguous_memory_size
    return contiguous_memory_size_decoder_count_ < kContiguousMemorySizeDecodersMax;
  }
  return true;
}

bool CodecFactoryHwPolicyAstro::AdmitHwEncoder(
    const fuchsia::mediacodec::CreateEncoder_Params& params) {
  // There aren't any on astro, so this doesn't actually run.
  return true;
}

zx::eventpair CodecFactoryHwPolicyAstro::TrackHwDecoder(
    const fuchsia::mediacodec::CreateDecoder_Params& params) {
  if (IsSwDecoderAvailableInPlaceOfHwAllocatingOutputFromContiguousMemorySize(params)) {
    ZX_DEBUG_ASSERT(AdmitHwDecoder(params));
    zx::eventpair lifetime_factory_end;
    zx::eventpair lifetime_codec_end;
    zx::eventpair::create(0, &lifetime_factory_end, &lifetime_codec_end);
    auto lifetime_wait =
        std::make_unique<async::WaitOnce>(lifetime_factory_end.get(), ZX_EVENTPAIR_PEER_CLOSED);
    auto lifetime_wait_ptr = lifetime_wait.get();
    // The part before invoking the Begin() is guaranteed (by current C++) to execute before the
    // move of lifetime_wait.
    zx_status_t status = lifetime_wait->Begin(
        dispatcher(),
        [this, lifetime_wait_ptr, lifetime_factory_end = std::move(lifetime_factory_end)](
            async_dispatcher_t* dispatcher, async::WaitOnce* wait, zx_status_t status,
            const zx_packet_signal_t* signal) {
          FX_LOGS(INFO) << "decoder lifetime over";
          // Regardless of whether status is ZX_OK or ZX_ERR_CANCELLED, the wait is over.  If
          // ZX_ERR_CANCELLED (only in tests, for now), we're about to delete "this" soon anyway, so
          // no harm in handling the same as ZX_OK.
          //
          // The present handler was moved to the stack before running, so this doesn't delete the
          // present handler.
          auto num_removed = all_waits_.erase(lifetime_wait_ptr);
          ZX_DEBUG_ASSERT(num_removed == 1);
          --contiguous_memory_size_decoder_count_;
          // ~lifetime_factory_end
        });
    // There is no reason for this to fail short of memory allocation failure, which would terminate
    // the process anyway.
    ZX_ASSERT(status == ZX_OK);
    all_waits_.emplace(lifetime_wait_ptr, std::move(lifetime_wait));
    ++contiguous_memory_size_decoder_count_;
    return lifetime_codec_end;
  } else {
    // No point in using a std::optional<zx::eventpair> since zx::eventpair can already indicate
    // empty.
    return zx::eventpair{};
  }
}

zx::eventpair CodecFactoryHwPolicyAstro::TrackHwEncoder(
    const fuchsia::mediacodec::CreateEncoder_Params& params) {
  // There aren't any on astro; nothing to track; this won't get called.
  ZX_PANIC("Not yet implemented.");
  return zx::eventpair{};
}
