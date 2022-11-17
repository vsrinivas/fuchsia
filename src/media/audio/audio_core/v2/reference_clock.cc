// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v2/reference_clock.h"

#include <fidl/fuchsia.hardware.audio/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace media_audio {

namespace {

zx::clock DupZxClockHandle(const zx::clock& in) {
  zx::clock out;
  if (auto status = in.duplicate(ZX_RIGHT_SAME_RIGHTS, &out); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "zx::clock::duplicate failed";
  }
  return out;
}

}  // namespace

// static
ReferenceClock ReferenceClock::FromFidlRingBuffer(fuchsia_audio::wire::RingBuffer ring_buffer) {
  return {
      .handle = DupZxClockHandle(ring_buffer.reference_clock()),
      .domain = ring_buffer.has_reference_clock_domain()
                    ? ring_buffer.reference_clock_domain()
                    : fuchsia_hardware_audio::kClockDomainExternal,
  };
}

ReferenceClock ReferenceClock::Dup() {
  return {
      .name = name,
      .handle = DupZxClockHandle(handle),
      .domain = domain,
  };
}

zx::clock ReferenceClock::DupHandle() { return DupZxClockHandle(handle); }

fuchsia_audio_mixer::wire::ReferenceClock ReferenceClock::ToFidl(fidl::AnyArena& arena) {
  auto builder = fuchsia_audio_mixer::wire::ReferenceClock::Builder(arena);
  if (!name.empty()) {
    builder.name(fidl::StringView(arena, name));
  }
  builder.handle(DupZxClockHandle(handle));
  builder.domain(domain);
  return builder.Build();
}

}  // namespace media_audio
