// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_EFFECTS_STAGE_V2_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_EFFECTS_STAGE_V2_H_

#include <fidl/fuchsia.audio.effects/cpp/wire.h>
#include <lib/fpromise/result.h>
#include <lib/service/llcpp/service.h>

#include <memory>

#include "src/media/audio/audio_core/cached_readable_stream_buffer.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/utils.h"
#include "src/media/audio/lib/clock/audio_clock.h"

namespace media::audio {

// EffectsStageV2 produces frames by passing a source stream through a FIDL effects processor.
class EffectsStageV2 : public ReadableStream {
 public:
  static fpromise::result<std::shared_ptr<EffectsStageV2>, zx_status_t> Create(
      fuchsia_audio_effects::wire::ProcessorConfiguration config,
      std::shared_ptr<ReadableStream> source);

  // |media::audio::ReadableStream|
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  AudioClock& reference_clock() override { return source_->reference_clock(); }
  std::optional<ReadableStream::Buffer> ReadLock(ReadLockContext& ctx, Fixed dest_frame,
                                                 int64_t frame_count) override;
  void Trim(Fixed dest_frame) override { source_->Trim(dest_frame); }

  void SetPresentationDelay(zx::duration external_delay) override;
  void ReportUnderflow(Fixed frac_source_start, Fixed frac_source_mix_point,
                       zx::duration underflow_duration) override {
    source_->ReportUnderflow(frac_source_start, frac_source_mix_point, underflow_duration);
  }
  void ReportPartialUnderflow(Fixed frac_source_offset, int64_t dest_mix_offset) override {
    source_->ReportPartialUnderflow(frac_source_offset, dest_mix_offset);
  }

  // Manages buffers for the FIDL connection.
  // Exported so it can be tested and used in tests.
  struct FidlBuffers {
    // Will crash if the VMOs are not R+W mappable.
    static FidlBuffers Create(const fuchsia_mem::wire::Range& input_range,
                              const fuchsia_mem::wire::Range& output_range);

    void* input;
    void* output;
    size_t input_size;
    size_t output_size;

    // This will have one entry if the input and output buffers share the same VMO,
    // else it will have two entries.
    std::vector<fbl::RefPtr<RefCountedVmoMapper>> mappers;
  };

 private:
  EffectsStageV2(fuchsia_audio_effects::wire::ProcessorConfiguration config,
                 std::shared_ptr<ReadableStream> source);

  void CallProcess(ReadLockContext& ctx, int64_t num_frames, float total_applied_gain_db,
                   uint32_t usage_mask);
  zx::duration ComputeIntrinsicMinLeadTime() const;

  std::shared_ptr<ReadableStream> source_;
  fidl::WireSyncClient<fuchsia_audio_effects::Processor> processor_;
  FidlBuffers fidl_buffers_;

  const int64_t max_frames_per_call_;
  const int64_t block_size_frames_;
  const int64_t latency_frames_;

  // The last buffer returned from ReadLock, saved to prevent recomputing frames on
  // consecutive calls to ReadLock. This is reset once the caller has unlocked the buffer,
  // signifying that the buffer is no longer needed.
  CachedReadableStreamBuffer cached_buffer_;

  const int64_t ringout_total_frames_;  // total frames of ring out
  int64_t next_ringout_frame_ = 0;      // start of the next ringout period

  // Buffer holding one pair of encoded FIDL Process request and response message.
  fidl::SyncClientBuffer<fuchsia_audio_effects::Processor::Process> process_buffer_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_EFFECTS_STAGE_V2_H_
