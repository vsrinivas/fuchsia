// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_CUSTOM_STAGE_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_CUSTOM_STAGE_H_

#include <fidl/fuchsia.audio.effects/cpp/markers.h>
#include <fidl/fuchsia.audio.effects/cpp/wire.h>
#include <lib/fidl/llcpp/sync_call.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>

#include <cstddef>
#include <utility>

#include "src/media/audio/mixer_service/common/basic_types.h"
#include "src/media/audio/mixer_service/mix/pipeline_stage.h"
#include "src/media/audio/mixer_service/mix/ptr_decls.h"
#include "src/media/audio/mixer_service/mix/reusable_buffer.h"

namespace media_audio_mixer_service {

// Custom effect stage that has a single input and produces a single output.
// TODO(fxbug.dev/87651): Generalize this for all N inputs K outputs use cases.
class CustomStage : public PipelineStage {
 public:
  explicit CustomStage(fuchsia_audio_effects::wire::ProcessorConfiguration config);

  // Implements `PipelineStage`.
  void AddSource(PipelineStagePtr src) final {
    FX_CHECK(!source_) << "CustomStage does not currently support multiple input sources";
    FX_CHECK(src->format() == format())
        << "CustomStage format does not match with input source format";
    source_ = std::move(src);
  }
  void RemoveSource(PipelineStagePtr src) final {
    FX_CHECK(source_) << "CustomStage input source was not found";
    FX_CHECK(source_ == src) << "CustomStage input source does not match with: " << src->name();
    source_ = nullptr;
  }

 protected:
  void AdvanceImpl(Fixed frame) final;
  std::optional<Packet> ReadImpl(Fixed start_frame, int64_t frame_count) final;

 private:
  friend class CustomStageTestProcessor;

  // Manages input and output buffers for the FIDL connection.
  struct FidlBuffers {
    // Will crash if the VMOs are not R+W mappable.
    FidlBuffers(const fuchsia_mem::wire::Range& input_range,
                const fuchsia_mem::wire::Range& output_range);

    void* input;
    void* output;
    size_t input_size;
    size_t output_size;

    // This will have one entry if the input and output buffers share the same VMO, else it will
    // have two entries.
    std::vector<fzl::VmoMapper> mappers;
  };

  // Calls FIDL `Process`.
  void CallFidlProcess();

  // Processes the next buffer at `start_frame` with `frame_count`.
  int64_t ProcessBuffer(Fixed start_frame, int64_t frame_count);

  const uint64_t block_size_frames_;
  const uint64_t max_frames_per_call_;

  FidlBuffers fidl_buffers_;
  fidl::WireSyncClient<fuchsia_audio_effects::Processor> fidl_processor_;

  PipelineStagePtr source_ = nullptr;

  // This will be true while the output buffer is valid for use.
  //
  // We must process frames in batches that are multiples of `block_size_frames_`. This is done by
  // accumulating data from `source_` into `source_buffer_` until we have buffered at least one full
  // batch of frames, at which point we call `ProcessBuffer` to fill the next buffer into
  // `fidl_buffers_.output`. This output buffer will remain valid until we `Advance` past
  // `source_buffer_.end()`.
  //
  // For example:
  //
  //   +------------------------+
  //   |    `source_buffer_`    |
  //   +------------------------+
  //   ^       ^        ^       ^      ^
  //   A       B        C       D      E
  //
  // 1. Caller asks for frames [A,B). Assume D = A+block_size. We read frames [A,D) from `source_`
  //    into `source_buffer_`, then process those frames, which will fill the processed data into
  //    `fidl_buffers_.output`. Then, we set `has_valid_output_` to true, and return processed
  //    frames [A,B).
  //
  // 2. Caller asks for frames [B,C). This intersects `source_buffer_`, so we return processed
  //    frames [B,C).
  //
  // 3. Caller asks for frames [C,E). This intersects `source_buffer_`, so we return processed
  //    frames [C,D). When the caller is done with those frames, we receive an `Advance(D)` call
  //    (via `PipelineStage::Packet::~Packet`), which invalidates the output buffer by setting
  //    `has_valid_output_` to false.
  //
  // 4. Caller asks for frames [D,E). The above process repeats.
  bool has_valid_output_ = false;

  // Buffer holding one pair of encoded FIDL `Process` request and response message.
  fidl::SyncClientBuffer<fuchsia_audio_effects::Processor::Process> fidl_process_buffer_;

  // This is non-empty while `has_valid_output_` is true.
  ReusableBuffer source_buffer_;
};

}  // namespace media_audio_mixer_service

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_CUSTOM_STAGE_H_
