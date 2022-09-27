// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_EFFECTS_STAGE_V2_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_EFFECTS_STAGE_V2_H_

#include <fidl/fuchsia.audio.effects/cpp/wire.h>
#include <lib/fpromise/result.h>
#include <lib/sys/component/cpp/service_client.h>

#include <memory>

#include "src/media/audio/audio_core/clock.h"
#include "src/media/audio/audio_core/reusable_buffer.h"
#include "src/media/audio/audio_core/stream.h"
#include "src/media/audio/audio_core/utils.h"

namespace media::audio {

// EffectsStageV2 produces frames by passing a source stream through a FIDL effects processor.
class EffectsStageV2 : public ReadableStream {
 public:
  static fpromise::result<std::shared_ptr<EffectsStageV2>, zx_status_t> Create(
      fuchsia_audio_effects::wire::ProcessorConfiguration config,
      std::shared_ptr<ReadableStream> source);

  // |media::audio::ReadableStream|
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override;
  std::shared_ptr<Clock> reference_clock() override { return source_->reference_clock(); }
  void SetPresentationDelay(zx::duration external_delay) override;

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

  // |media::audio::ReadableStream|
  std::optional<ReadableStream::Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed dest_frame,
                                                     int64_t frame_count) override;
  void TrimImpl(Fixed dest_frame) override;

  int64_t FillCache(ReadLockContext& ctx, Fixed dest_frame, int64_t frame_count);
  void CallProcess(ReadLockContext& ctx, StreamUsageMask source_usage_mask,
                   float source_total_applied_gain_db);
  zx::duration ComputeIntrinsicMinLeadTime() const;

  std::shared_ptr<ReadableStream> source_;
  fidl::WireSyncClient<fuchsia_audio_effects::Processor> processor_;
  FidlBuffers fidl_buffers_;

  const int64_t max_frames_per_call_;  // guaranteed > 0
  const int64_t block_size_frames_;    // guaranteed > 0
  const int64_t output_shift_frames_;  // how much the output is shifted relative to the input

  // We must process frames in batches that are multiples of block_size_frames_. Our cache
  // accumulates data from source_ until we've buffered at least one full batch, at which
  // point we run the effect and store the output of the effect in cache.dest_buffer.
  // The cache lives until we Trim past source_buffer_.end().
  //
  // For example:
  //
  //   +------------------------+
  //   |     source_buffer_     |
  //   +------------------------+
  //   ^       ^        ^       ^      ^
  //   A       B        C       D      E
  //
  // 1. Caller asks for frames [A,B). Assume D = A+block_size. We read frames [A,D) from
  //    source_ into source_buffer_, then process those frames, leaving the processed
  //    data in cache_.dest_buffer. We return processed frames [A,B).
  //
  // 2. Caller asks for frames [B,C). This intersects source_buffer_, so we return
  //    processed frames [B,C).
  //
  // 3. Caller asks for frames [C,E). This intersects source_buffer_, so we return processed
  //    frames [C,D). When the caller is done with those frames, we receive a Trim(D) call
  //    (via ReadableStream::Buffer::~Buffer), which sets cache_ to std::nullopt.
  //
  // 4. Caller asks for frames [D,E). The above process repeats.
  struct Cache {
    // Properties of source_buffer_.
    StreamUsageMask source_usage_mask;
    float source_total_applied_gain_db;
    // Destination frames after processing. This refers to the same set of frames as source_buffer_,
    // and if the effect processes in-place, it points at source_buffer_.payload().
    void* dest_buffer;
  };
  std::optional<Cache> cache_;

  // This is non-empty iff cache_ != std::nullopt.
  ReusableBuffer source_buffer_;

  // Buffer holding one pair of encoded FIDL Process request and response message.
  fidl::SyncClientBuffer<fuchsia_audio_effects::Processor::Process> process_buffer_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_EFFECTS_STAGE_V2_H_
