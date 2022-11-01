// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/media/codec_impl/codec_adapter.h>
#include <zircon/assert.h>

#include <limits>
#include <memory>

namespace {
constexpr uint64_t kInputBufferConstraintsVersionOrdinal = 1;

}  // namespace

CodecAdapter::CodecAdapter(std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
    : lock_(lock),
      events_(codec_adapter_events),
      random_device_(),
      not_for_security_prng_(random_device_()) {
  ZX_DEBUG_ASSERT(events_);
  // nothing else to do here
}

CodecAdapter::~CodecAdapter() {
  // nothing to do here
}

void SetCodecMetrics(CodecMetrics* codec_metrics);

void CodecAdapter::SetCodecDiagnostics(CodecDiagnostics* codec_diagnostics) {
  // Default implementation does nothing with diagnostic data
}

std::optional<media_metrics::StreamProcessorEvents2MigratedMetricDimensionImplementation>
CodecAdapter::CoreCodecMetricsImplementation() {
  // This will cause a ZX_PANIC() if LogEvent() is being used by a sub-class, in which case the
  // sub-class must override CoreCodecMetricsImplementation().
  return std::nullopt;
}

void CodecAdapter::CoreCodecSetSecureMemoryMode(
    CodecPort port, fuchsia::mediacodec::SecureMemoryMode secure_memory_mode) {
  if (secure_memory_mode != fuchsia::mediacodec::SecureMemoryMode::OFF) {
    events_->onCoreCodecFailCodec(
        "In CodecAdapter::CoreCodecSetSecureMemoryMode(), secure_memory_mode != OFF");
    return;
  }
  // CodecImpl will enforce that BufferCollection constraints and BufferCollectionInfo_2 are
  // consistent with OFF.
  return;
}

std::unique_ptr<const fuchsia::media::StreamBufferConstraints>
CodecAdapter::CoreCodecBuildNewInputConstraints() {
  auto constraints = std::make_unique<fuchsia::media::StreamBufferConstraints>();
  constraints->set_buffer_constraints_version_ordinal(kInputBufferConstraintsVersionOrdinal);
  return constraints;
}

void CodecAdapter::CoreCodecResetStreamAfterCurrentFrame() {
  ZX_PANIC(
      "onCoreCodecResetStreamAfterCurrentFrame() triggered by a CodecAdapter that doesn't override "
      "CoreCodecResetStreamAfterCurrentFrame()");
}
