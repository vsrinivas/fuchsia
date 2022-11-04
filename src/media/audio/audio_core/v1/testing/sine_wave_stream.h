// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_SINE_WAVE_STREAM_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_SINE_WAVE_STREAM_H_

#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/stream.h"
#include "src/media/audio/audio_core/v1/stream_usage.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/format/audio_buffer.h"

namespace media::audio::testing {

// A stream that contains an infinitely-repeating sine wave with the given format and period.
template <fuchsia::media::AudioSampleFormat SampleFormat>
class SineWaveStream : public ReadableStream {
 public:
  SineWaveStream(TypedFormat<SampleFormat> format, int64_t period_frames, StreamUsage usage,
                 std::shared_ptr<Clock> clock)
      : ReadableStream("SineWaveStream", format),
        usage_mask_({usage}),
        clock_(std::move(clock)),
        buffer_(GenerateCosineAudio(format,              //
                                    period_frames * 10,  // num_frames
                                    10,                  // number of periods within num_frames
                                    1.0,                 // amplitude
                                    -M_PI_2)),           // phase (generate sine instead of cosine)
        // zx::time(0) == frame 0
        timeline_function_(fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
            TimelineRate(Fixed(format.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())))) {}

  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override {
    auto [timeline_function, generation] = timeline_function_->get();
    return {
        .timeline_function = timeline_function,
        .generation = generation,
    };
  }

  std::shared_ptr<Clock> reference_clock() override { return clock_; }

 private:
  std::optional<Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed frame,
                                     int64_t frame_count) override {
    int64_t frame_index = frame.Floor() % buffer_.NumFrames();
    int64_t sample_index = buffer_.SampleIndex(frame_index, 0);
    frame_count = std::min(frame_count, buffer_.NumFrames() - frame_index);

    return MakeUncachedBuffer(Fixed(frame), frame_count, &buffer_.samples()[sample_index],
                              usage_mask_, 0.0f);
  }

  void TrimImpl(Fixed frame) override {}

  StreamUsageMask usage_mask_;
  std::shared_ptr<Clock> clock_;
  AudioBuffer<SampleFormat> buffer_;
  fbl::RefPtr<VersionedTimelineFunction> timeline_function_;
};

}  // namespace media::audio::testing

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_TESTING_SINE_WAVE_STREAM_H_
