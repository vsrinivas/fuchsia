// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_UTILS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_UTILS_H_

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/profile.h>
#include <stdint.h>
#include <zircon/device/audio.h>
#include <zircon/types.h>

#include <atomic>
#include <vector>

#include <fbl/ref_counted.h>

#include "src/media/audio/audio_core/shared/mixer/constants.h"
#include "src/media/audio/audio_core/v1/mix_profile_config.h"

namespace media::audio {

class GenerationId {
 public:
  uint32_t get() const { return id_; }
  uint32_t Next() {
    uint32_t ret;
    do {
      ret = ++id_;
    } while (ret == kInvalidGenerationId);
    return ret;
  }

 private:
  uint32_t id_ = kInvalidGenerationId + 1;
};

class AtomicGenerationId {
 public:
  AtomicGenerationId() : id_(kInvalidGenerationId + 1) {}

  uint32_t get() const { return id_.load(); }
  uint32_t Next() {
    uint32_t ret;
    do {
      ret = id_.fetch_add(1);
    } while (ret == kInvalidGenerationId);
    return ret;
  }

 private:
  std::atomic<uint32_t> id_;
};

// Given a preferred format and a list of driver supported formats, select
// the "best" form and update the in/out parameters, then return ZX_OK.  If no
// formats exist, or all format ranges get completely rejected, return an error
// and leave the in/out params as they were.
zx_status_t SelectBestFormat(const std::vector<fuchsia::hardware::audio::PcmSupportedFormats>& fmts,
                             uint32_t* frames_per_second_inout, uint32_t* channels_inout,
                             fuchsia::media::AudioSampleFormat* sample_format_inout);
zx_status_t SelectBestFormat(const std::vector<audio_stream_format_range_t>& fmts,
                             uint32_t* frames_per_second_inout, uint32_t* channels_inout,
                             fuchsia::media::AudioSampleFormat* sample_format_inout);

// Given a format and a list of driver supported formats, if the format is found in
// the driver supported list then return true, otherwise return false.
bool IsFormatInSupported(
    const fuchsia::media::AudioStreamType& stream_type,
    const std::vector<fuchsia::hardware::audio::PcmSupportedFormats>& supported_formats);

// A simple extension to the libfzl VmoMapper which mixes in ref counting state
// to allow for shared VmoMapper semantics.
class RefCountedVmoMapper : public fzl::VmoMapper, public fbl::RefCounted<fzl::VmoMapper> {};

zx_status_t AcquireHighPriorityProfile(const MixProfileConfig& mix_profile_config,
                                       zx::profile* profile);

void AcquireAudioCoreImplProfile(sys::ComponentContext* context,
                                 fit::function<void(zx_status_t, zx::profile)> callback);

void AcquireRelativePriorityProfile(uint32_t priority, sys::ComponentContext* context,
                                    fit::function<void(zx_status_t, zx::profile)> callback);

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_UTILS_H_
