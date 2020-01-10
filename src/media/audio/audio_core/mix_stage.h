// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIX_STAGE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIX_STAGE_H_

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/media/cpp/timeline_function.h>
#include <lib/zx/time.h>

#include "src/media/audio/audio_core/format.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/stream.h"

namespace media::audio {

class MixStage : public Stream {
 public:
  MixStage(const Format& output_format, uint32_t block_size,
           TimelineFunction reference_clock_to_output_frame);

  struct FrameSpan {
    int64_t start;
    uint32_t length;
  };

  // |media::audio::Stream|
  std::optional<Stream::Buffer> LockBuffer(zx::time ref_time, int64_t frame,
                                           uint32_t frame_count) override;
  void UnlockBuffer(bool release_buffer) override {}
  void Trim(zx::time ref_time) override;
  std::pair<TimelineFunction, uint32_t> ReferenceClockToFractionalFrames() const override;

  std::unique_ptr<Mixer> AddInput(fbl::RefPtr<Stream> stream);
  void RemoveInput(const Stream& stream);

 private:
  void SetupMixBuffer(uint32_t max_mix_frames);

  struct MixJob {
    // Job state set up once by an output implementation, used by all renderers.
    float* buf;
    uint32_t buf_frames;
    int64_t start_pts_of;  // start PTS, expressed in output frames.
    uint32_t reference_clock_to_destination_frame_gen;
    bool accumulate;
    const TimelineFunction* reference_clock_to_destination_frame;

    // Per-stream job state, set up for each renderer during SetupMix.
    uint32_t frames_produced;
  };

  // TODO(13415): Integrate it into the Mixer class itself.
  void UpdateSourceTrans(const Stream& stream, Mixer::Bookkeeping* bk);
  void UpdateDestTrans(const MixJob& job, Mixer::Bookkeeping* bk);

  enum class TaskType { Mix, Trim };

  void ForEachSource(TaskType task_type, zx::time ref_time);

  void SetupMix(Mixer* mixer);
  bool ProcessMix(Stream* stream, Mixer* mixer, const Stream::Buffer& buffer);

  void MixStream(Stream* stream, Mixer* mixer, zx::time ref_time);

  struct StreamHolder {
    fbl::RefPtr<Stream> stream;
    Mixer* mixer;
  };

  std::mutex stream_lock_;
  std::vector<StreamHolder> streams_;

  // State for the internal buffer which holds intermediate mix results.
  std::unique_ptr<float[]> mix_buf_;
  uint32_t mix_buf_frames_ = 0;

  // State used by the mix task.
  MixJob cur_mix_job_;

  TimelineFunction reference_clock_to_output_frame_;
  uint32_t reference_clock_to_output_frame_generation_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIX_STAGE_H_
