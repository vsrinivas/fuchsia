// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_CUSTOM_STAGE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_CUSTOM_STAGE_H_

#include <fidl/fuchsia.audio.effects/cpp/markers.h>
#include <fidl/fuchsia.audio.effects/cpp/wire.h>
#include <lib/fidl/cpp/wire/sync_call.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>

#include <cstddef>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/reusable_buffer.h"
#include "src/media/audio/services/mixer/mix/silence_padding_stage.h"

namespace media_audio {

// Custom effect stage that has a single source stream and a single destination stream.
// TODO(fxbug.dev/114246): Generalize this for all M sources x N destinations use cases.
class CustomStage : public PipelineStage {
 public:
  struct Args {
    // Name of this stage.
    std::string_view name;

    // Reference clock of this stage's output stream.
    UnreadableClock reference_clock;

    // Source stream config.
    Format source_format;
    fuchsia_mem::wire::Range source_buffer;

    // Destination stream config.
    Format dest_format;
    fuchsia_mem::wire::Range dest_buffer;

    // Processor block size in frames.
    int64_t block_size_frames;

    // Processor latency in frames.
    int64_t latency_frames;

    // Maximum frames to process per FIDL process call.
    int64_t max_frames_per_call;

    // Processor ring out in frames.
    int64_t ring_out_frames;

    // FIDL processor.
    fidl::WireSyncClient<fuchsia_audio_effects::Processor> processor;
  };
  explicit CustomStage(Args args);

  // Implements `PipelineStage`.
  void AddSource(PipelineStagePtr source, AddSourceOptions options) final {
    source_.AddSource(std::move(source), std::move(options));
    set_presentation_time_to_frac_frame(source_.presentation_time_to_frac_frame());
  }
  void RemoveSource(PipelineStagePtr source) final {
    source_.RemoveSource(std::move(source));
    set_presentation_time_to_frac_frame(source_.presentation_time_to_frac_frame());
  }
  void UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) final {
    set_presentation_time_to_frac_frame(f);
    source_.UpdatePresentationTimeToFracFrame(f);
  }

 protected:
  void AdvanceSelfImpl(Fixed frame) final;
  void AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) final;
  std::optional<Packet> ReadImpl(MixJobContext& ctx, Fixed start_frame, int64_t frame_count) final;

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

  // Processes next `frame_count` frames.
  int64_t Process(MixJobContext& ctx, int64_t frame_count);

  // Calls FIDL `Process`.
  void CallFidlProcess(MixJobContext& ctx);

  const int64_t block_size_frames_;
  const int64_t latency_frames_;
  const int64_t max_frames_per_call_;

  FidlBuffers fidl_buffers_;
  fidl::WireSyncClient<fuchsia_audio_effects::Processor> fidl_processor_;

  // Silence padding source stage to compensate for "ring out" frames.
  SilencePaddingStage source_;

  // Custom stage frames are processed in batches that are multiples of `block_size_frames_`. It is
  // done by accumulating data from the input `source_` into `source_buffer_`, also compensating for
  // `latency_frames_`, until we have buffered at least one full batch of frames. At which point we
  // call `Process` to fill the next buffer into `fidl_buffers_.output`. Then, we update
  // `latency_frames_processed_`, and set `output_` with a corresponding offset to compensate for
  // the processed latency frames. After each process, we set `next_frame_to_process` to the first
  // output frame that needs to be processed in the next call, so that, `output` will remain valid
  // until we `Advance` past `next_frame_to_process`.
  //
  // For example:
  //
  //   +------------------------+
  //   |    `source_buffer_`    |
  //   +------------------------+
  //   ^       ^        ^       ^      ^
  //   A       B        C       D      E
  //
  // 1. Caller asks for frames [A,B). Assume D = A + block_size. We read frames [A,D) from `source_`
  //    into `source_buffer_`, then process those frames, which will fill the processed data into
  //    `output_`, and return processed frames [A,B).
  //
  // 2. Caller asks for frames [B,C). This intersects `source_buffer_`, so we return processed
  //    frames [B,C).
  //
  // 3. Caller asks for frames [C,E). This intersects `source_buffer_`, so we return processed
  //    frames [C,D). When the caller is done with those frames, we receive an `Advance(D)` call
  //    (via `PipelineStage::Packet::~Packet`), which invalidates the output buffer by setting
  //    `output_` to nullptr.
  //
  // 4. Caller asks for frames [D,E). The above process repeats.
  void* output_ = nullptr;
  int64_t latency_frames_processed_ = 0;
  int64_t next_frame_to_process_ = 0;

  // Buffer holding one pair of encoded FIDL `Process` request and response message.
  fidl::SyncClientBuffer<fuchsia_audio_effects::Processor::Process> fidl_process_buffer_;

  // This is non-empty while `output_` is valid.
  ReusableBuffer source_buffer_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_CUSTOM_STAGE_H_
