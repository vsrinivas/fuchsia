// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V2_REFERENCE_CLOCK_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V2_REFERENCE_CLOCK_H_

#include <fidl/fuchsia.audio.mixer/cpp/wire.h>
#include <fidl/fuchsia.audio/cpp/wire.h>
#include <lib/zx/clock.h>

#include <string>

namespace media_audio {

// Wraps a fuchsia.audio.mixer.ReferenceClock object.
struct ReferenceClock {
  std::string name;
  zx::clock handle;
  uint32_t domain;

  // Construct from a FIDL RingBuffer.
  static ReferenceClock FromFidlRingBuffer(fuchsia_audio::wire::RingBuffer ring_buffer);

  // Duplicates this object.
  ReferenceClock Dup();

  // Duplicates `handle`.
  zx::clock DupHandle();

  // Converts to a FIDL ReferenceClock. Duplicates (does not consume) `handle`.
  fuchsia_audio_mixer::wire::ReferenceClock ToFidl(fidl::AnyArena& arena);
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V2_REFERENCE_CLOCK_H_
