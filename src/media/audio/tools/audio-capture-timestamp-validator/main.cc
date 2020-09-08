// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <lib/zx/vmo.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/lib/analysis/analysis.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/logging/cli.h"
#include "src/media/audio/lib/wav/wav_writer.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace {
constexpr auto kChannelCount = 1;
constexpr auto kFrameRate = 96000;
constexpr auto kFramesPerCapturePacket = kFrameRate * 2 / 1000;  // 2ms
constexpr auto kImpulseFrames = 35;                              // ~0.4ms
constexpr auto kImpulseMagnitude = 0.75;
const auto kImpulseFormat =
    media::audio::Format::Create<ASF::FLOAT>(kChannelCount, kFrameRate).value();
const auto kCaptureFormat =
    media::audio::Format::Create<ASF::FLOAT>(kChannelCount, kFrameRate).value();

// Given perfect math and full-volume output, the impulse is a step function with
// magnitude kImpulseMagnitude. Due to quantization and internal scaling, we may
// see different values. Also, on some devices, the microphone picks up sounds at
// a much lower volume than the output. Empirically, the following threshold works
// well on an Astro device at full volume.
constexpr double kNoiseFloor = 0.01;

bool verbose = false;
zx::time global_start_time_mono;
async::Loop* loop;

void Shutdown() {
  async::PostTask(loop->dispatcher(), []() { loop->Quit(); });
}

zx::clock DupClock() {
  // Use the same clock for all renderers and capturers so everything is sync'd up.
  // Currently we're using the system monotonic clock.
  return media::audio::clock::CloneOfMonotonic();
}

double DurationToFrames(zx::duration d) {
  return static_cast<double>(d.to_nsecs()) * kFrameRate / 1e9;
}

std::string SprintDuration(zx::duration d) {
  return fxl::StringPrintf("%ld ns (%f frames)", d.to_nsecs(), DurationToFrames(d));
}

class Barrier {
 public:
  explicit Barrier(size_t size) : size_(size) {}

  void Wait(std::function<void()> ready_cb) {
    callbacks_.push_back(std::move(ready_cb));
    size_--;
    if (size_ == 0) {
      for (auto& cb : callbacks_) {
        cb();
      }
    }
  }

 private:
  size_t size_;
  std::vector<std::function<void()>> callbacks_;
};

class Capture {
 public:
  Capture(fuchsia::media::AudioPtr& audio, bool is_loopback, const std::string& filename,
          Barrier& barrier)
      : filename_(filename), format_(kCaptureFormat), barrier_(barrier), buffer_(format_, 0) {
    // Create the WAV file writer.
    CLI_CHECK(wav_writer_.Initialize(filename_.c_str(), format_.sample_format(), format_.channels(),
                                     format_.frames_per_second(), format_.bytes_per_sample() * 8),
              "Could not create " << filename);

    // Create the capturer.
    audio->CreateAudioCapturer(capturer_.NewRequest(), is_loopback);
    capturer_.set_error_handler([this](zx_status_t status) {
      printf("Capturer for %s failed with status %d.\n", filename_.c_str(), status);
      Shutdown();
    });
    capturer_->SetReferenceClock(DupClock());
    capturer_->SetPcmStreamType(format_.stream_type());
    SetupPayloadBuffer();
    capturer_->GetReferenceClock([this](zx::clock c) {
      clock_ = std::move(c);
      barrier_.Wait([this]() { Start(); });
    });
  }

  ~Capture() {
    printf("Closing %s (%lu frames, %lu bytes)\n", filename_.c_str(), buffer_.NumFrames(),
           buffer_.NumBytes());
    CLI_CHECK(wav_writer_.Close(), "Could not close " << filename_);
  }

  void Stop() {
    capturer_.events().OnPacketProduced = nullptr;
    capturer_->StopAsyncCaptureNoReply();
  }

  // Given a list of times where we expect to see signals, return a list of times where
  // signals are actually detected, using -1 when a signal cannot be detected.
  std::vector<zx::time> FindSounds(std::vector<zx::time> expected_times_mono) {
    std::vector<zx::time> out;

    for (auto expected_time_mono : expected_times_mono) {
      // If everything goes perfectly, we should find the signal at exactly expected_time_mono
      // for the loopback capture and slightly later for the microphone capture. Signals are
      // separated by 1s. To account for signals that might be way off, search +/- 250ms around
      // the expected time.
      auto search_time_start = expected_time_mono - zx::msec(250);
      int64_t search_frame_start =
          std::max(0l, frames_to_mono_time_.Inverse().Apply(search_time_start.get()));
      int64_t search_frame_end =
          std::min(search_frame_start + format_.frames_per_ns().Scale(ZX_MSEC(500)),
                   static_cast<int64_t>(buffer_.NumFrames()));

      auto slice = media::audio::AudioBufferSlice(&buffer_, search_frame_start, search_frame_end);
      auto max_frame = FindImpulseLeadingEdge(slice, kNoiseFloor);

      if (verbose) {
        for (auto f = search_frame_start; f < search_frame_end; f++) {
          auto val = buffer_.SampleAt(f, 0);
          if (val > kNoiseFloor) {
            size_t slice_index = f - search_frame_start;
            printf("[verbose] frame %lu, sample %f%s\n", f, val,
                   (max_frame && slice_index == *max_frame) ? " (left edge)" : "");
          }
        }
      }

      if (!max_frame) {
        out.push_back(zx::time(-1));
        continue;
      }

      auto left_edge = *max_frame + search_frame_start;
      out.push_back(zx::time(frames_to_mono_time_.Apply(left_edge)));
      if (verbose) {
        printf("[verbose] *** signal estimated at frame %lu, expected signal at frame %lu\n",
               left_edge, frames_to_mono_time_.Inverse().Apply(expected_time_mono.get()));
      }
    }

    return out;
  }

 private:
  void SetupPayloadBuffer() {
    const auto frames_per_payload = format_.frames_per_second();  // 1s
    const auto bytes_per_payload = frames_per_payload * format_.bytes_per_frame();

    zx::vmo vmo;
    auto status = vmo_mapper_.CreateAndMap(
        bytes_per_payload, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo,
        ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
    CLI_CHECK_OK(status, "Failed to create " << bytes_per_payload << "-byte payload buffer");
    memset(PayloadStart(), 0, bytes_per_payload);
    capturer_->AddPayloadBuffer(0, std::move(vmo));
  }

  void Start() {
    printf("Starting capture to %s\n", filename_.c_str());
    capturer_.events().OnPacketProduced = [this](fuchsia::media::StreamPacket pkt) {
      OnPacket(pkt);
    };
    capturer_->StartAsyncCapture(kFramesPerCapturePacket);
  }

  void OnPacket(fuchsia::media::StreamPacket pkt) {
    auto cleanup = fit::defer([this, pkt]() { capturer_->ReleasePacket(pkt); });

    if (!wrote_first_packet_) {
      // The first output frame should occur at global_start_time_mono.
      // Write enough silence to cover the time between then and this packet's PTS.
      auto packet_time_mono =
          media::audio::clock::MonotonicTimeFromReferenceTime(clock_, zx::time(pkt.pts)).value();
      auto duration = packet_time_mono - global_start_time_mono;
      FX_CHECK(duration.get() > 0) << duration.get();

      auto num_silent_frames = format_.frames_per_ns().Scale(duration.get());
      if (verbose) {
        printf("[verbose] Writing %ld silent frames to the start of %s\n", num_silent_frames,
               filename_.c_str());
      }

      std::vector<char> buffer(num_silent_frames * format_.bytes_per_frame());
      if (!wav_writer_.Write(reinterpret_cast<void*>(&buffer[0]), buffer.size())) {
        printf("First write failed.\n");
        CLI_CHECK(wav_writer_.Close(), "File close failed as well.");
        Shutdown();
      }

      wrote_first_packet_ = true;
      frames_to_mono_time_ =
          media::TimelineFunction(packet_time_mono.get(), 0, format_.frames_per_ns().Inverse());
    } else {
      if (pkt.flags & fuchsia::media::STREAM_PACKET_FLAG_DISCONTINUITY) {
        printf("WARNING: found discontinuity within recording of %s\n", filename_.c_str());
      }
    }

    if (!pkt.payload_size) {
      return;
    }

    // Append this packet to the WAV file.
    auto first_byte = PayloadStart() + pkt.payload_offset;
    if (!wav_writer_.Write(reinterpret_cast<void*>(first_byte), pkt.payload_size)) {
      printf("File write failed. Trying to save any already-written data.\n");
      CLI_CHECK(wav_writer_.Close(), "File close failed as well.");
      Shutdown();
    }

    // Also save the full audio as an in-memory buffer.
    float* first_sample = reinterpret_cast<float*>(first_byte);
    buffer_.samples().insert(buffer_.samples().end(), first_sample,
                             first_sample + pkt.payload_size / format_.bytes_per_sample());
  }

  uint8_t* PayloadStart() const { return reinterpret_cast<uint8_t*>(vmo_mapper_.start()); }

  const std::string filename_;
  const media::audio::TypedFormat<ASF::FLOAT> format_;
  Barrier& barrier_;
  fuchsia::media::AudioCapturerPtr capturer_;
  media::audio::WavWriter<> wav_writer_;
  fzl::VmoMapper vmo_mapper_;
  zx::clock clock_;

  bool wrote_first_packet_ = false;

  media::TimelineFunction frames_to_mono_time_;
  media::audio::AudioBuffer<ASF::FLOAT> buffer_;
};

void PlaySound(fuchsia::media::AudioPtr& audio, zx::clock reference_clock, zx::time reference_time,
               media::audio::AudioBuffer<ASF::FLOAT> sound) {
  // Create a renderer.
  // We wrap this in a shared_ptr so it can live until the sound is fully rendered.
  auto holder = std::make_shared<fuchsia::media::AudioRendererPtr>();
  auto& r = *holder;
  audio->CreateAudioRenderer(r.NewRequest());
  r.set_error_handler([](zx_status_t status) {
    printf("PlaySound renderer failed with status %d.\n", status);
    Shutdown();
  });
  r->SetReferenceClock(std::move(reference_clock));
  r->SetUsage(fuchsia::media::AudioRenderUsage::MEDIA);
  r->SetPcmStreamType(kImpulseFormat.stream_type());

  // Setup the payload.
  fzl::VmoMapper vmo_mapper;
  zx::vmo vmo;
  auto status =
      vmo_mapper.CreateAndMap(sound.NumBytes(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo,
                              ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER);
  CLI_CHECK_OK(status, "Failed to create " << sound.NumBytes() << "-byte payload buffer");
  memmove(reinterpret_cast<uint8_t*>(vmo_mapper.start()),
          reinterpret_cast<uint8_t*>(&sound.samples()[0]), sound.NumBytes());
  r->AddPayloadBuffer(0, std::move(vmo));
  auto pkt = fuchsia::media::StreamPacket{
      .pts = 0,
      .payload_buffer_id = 0,
      .payload_offset = 0,
      .payload_size = sound.NumBytes(),
  };

  // Play this sound and tear down the renderer once the sound has been played.
  r->SendPacket(pkt, [holder]() mutable {
    printf("Played sound\n");
    holder->Unbind();
  });
  r->Play(reference_time.get(), 0,
          [reference_time](int64_t play_ref_time, int64_t play_media_time) {
            if (play_ref_time != reference_time.get()) {
              printf("WARNING: Play() changed the reference time by %ld ns\n",
                     play_ref_time - reference_time.get());
            }
            if (play_media_time != 0) {
              printf("WARNING: Play() changed the media time from 0 to %ld\n", play_media_time);
            }
          });
}

void CheckAlignment(std::vector<zx::time> play_times, std::vector<zx::time> microphone_times,
                    std::vector<zx::time> loopback_times) {
  printf("============================================\n");
  printf("Alignment\n");
  printf("\n");
  printf("Ideally, the loopback should be perfectly aligned with the renderer and the\n");
  printf("microphone should occur slightly later due to propagation delay between the\n");
  printf("speaker and microphone (assuming 6\" separation, the delay should be 437us).\n");
  printf("\n");

  int tests_pass = 0;
  int tests_unknown = 0;

  for (size_t k = 0; k < play_times.size(); k++) {
    auto rt = play_times[k] - global_start_time_mono;
    auto mt = microphone_times[k] - global_start_time_mono;
    auto lt = loopback_times[k] - global_start_time_mono;

    printf("Sound %lu\n", k);
    printf("  render @ %ld ns\n", rt.to_nsecs());

    if (mt.get() > 0) {
      printf("  microphone @ %ld ns, render - microphone = %s\n", mt.to_nsecs(),
             SprintDuration(rt - mt).c_str());
    } else {
      printf("  not found in microphone\n");
    }

    if (lt.get() > 0) {
      printf("  loopback @ %ld ns, render - loopback = %s", lt.to_nsecs(),
             SprintDuration(rt - lt).c_str());
      if (mt.get() > 0) {
        printf(", microphone - loopback = %s", SprintDuration(mt - lt).c_str());
      }
      printf("\n");
    } else {
      printf("  not found in loopback\n");
    }

    if (mt.get() > 0 && lt.get() > 0) {
      bool pass = true;

      // Loopback timestamp must match the render timestamp.
      if (auto delta_frames = std::abs(DurationToFrames(rt - lt)); delta_frames > 1) {
        pass = false;
        printf("  failed: loopback not aligned with renderer\n");
      }
      // Microphone timestamp must be beyond the loopback timestamp by at most 100ms.
      if (mt.get() < lt.get()) {
        pass = false;
        printf("  failed: microphone timestamp before loopback timestamp\n");
      } else if (mt - lt > zx::msec(100)) {
        pass = false;
        printf("  failed: microphone timestamp more than 100ms after loopback timestamp\n");
      }

      if (pass) {
        printf("  passed\n");
        tests_pass++;
      }
    } else {
      tests_unknown++;
    }

    printf("\n");
  }

  printf("Results\n");
  printf("  %d passed\n", tests_pass);
  printf("  %lu failed\n", play_times.size() - (tests_pass + tests_unknown));
  printf("  %d could not locate timestamps\n", tests_unknown);
  printf("\n");
}
}  // namespace

int main(int argc, const char** argv) {
  loop = new async::Loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto ctx = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  if (command_line.HasOption("help")) {
    printf("Usage: audio-capture-timestamp-validator [--duration-seconds=10] [--verbose]\n");
    printf("\n");
    printf("This tool helps to debug capture timestamp issues. It does three things\n");
    printf("concurrently:\n");
    printf("\n");
    printf("  1. Plays a short impulse once per second\n");
    printf("  2. Captures the loopback interface\n");
    printf("  3. Captures the microphone interface\n");
    printf("\n");
    printf("The tool then compares the timestamps at which the impulses are captured by\n");
    printf("the loopback and microphone interfaces. Microphone timestamps should occur\n");
    printf("strictly after loopback timestamps. Direct open-air acoustic propagation is\n");
    printf("approximately 1 ft/ms; many full-duplex algorithms accommodate environmental\n");
    printf("delays of up to 100 ms.\n");
    printf("\n");
    printf("The captured audio is saved to WAV files for futher debugging.\n");
    return 0;
  }

  verbose = command_line.HasOption("verbose");

  printf("WARNING: Volume will be increased to 100%% temporarily. If the tool does not\n");
  printf("         shut down cleanly, the volume may not be restored. For most accurate\n");
  printf("         results, run in a quiet environment.\n");

  std::string duration_str;
  int64_t duration_seconds = 10;
  if (command_line.GetOptionValue("duration-seconds", &duration_str)) {
    CLI_CHECK(sscanf(duration_str.c_str(), "%li", &duration_seconds) == 1,
              "--duration_seconds must be an integer");
    CLI_CHECK(duration_seconds > 0, "--duration_seconds must be positive");
  }

  fuchsia::media::AudioPtr audio = ctx->svc()->Connect<fuchsia::media::Audio>();

  // Set the volume to 100%.
  fuchsia::media::AudioCorePtr audio_core = ctx->svc()->Connect<fuchsia::media::AudioCore>();
  fuchsia::media::audio::VolumeControlPtr volume_control;
  audio_core->BindUsageVolumeControl(
      fuchsia::media::Usage::WithRenderUsage(fuchsia::media::AudioRenderUsage::MEDIA),
      volume_control.NewRequest());

  std::optional<float> old_volume;
  volume_control.events().OnVolumeMuteChanged = [&old_volume](float v, bool muted) {
    if (!old_volume) {
      printf("Saving old volume: %f\n", v);
      old_volume = v;
    }
  };
  while (!old_volume) {
    loop->RunUntilIdle();
  }
  volume_control->SetVolume(1.0);

  // Restore volume on exit.
  auto restore_volume =
      fit::defer([old_volume, &volume_control]() { volume_control->SetVolume(*old_volume); });

  // Play a short impulse every second.
  // Play the first sound at least 1s in the future so it's beyond the renderer MinLeadTime
  // and so we have plenty of time to setup the capturers before the first sound is played.
  auto impulse = GenerateConstantAudio(kImpulseFormat, kImpulseFrames, kImpulseMagnitude);
  global_start_time_mono = zx::clock::get_monotonic();
  std::vector<zx::time> play_times;
  for (auto k = 1; k < duration_seconds; k++) {
    auto t = global_start_time_mono + zx::sec(k);
    PlaySound(audio, DupClock(), t, impulse);
    play_times.push_back(t);
  }

  // Start the capturers.
  // We use a barrier to align the start time of the output wav files.
  auto barrier = std::make_unique<Barrier>(2);
  auto microphone = std::make_unique<Capture>(audio, false, "/tmp/microphone.wav", *barrier);
  auto loopback = std::make_unique<Capture>(audio, true, "/tmp/loopback.wav", *barrier);
  loop->Run(zx::clock::get_monotonic() + zx::sec(duration_seconds));

  microphone->Stop();
  loopback->Stop();
  loop->RunUntilIdle();

  // Check alignment.
  if (verbose) {
    printf("[verbose] Looking for sounds in the microphone capture\n");
  }
  auto microphone_times = microphone->FindSounds(play_times);
  if (verbose) {
    printf("[verbose] Looking for sounds in the loopback capture\n");
  }
  auto loopback_times = loopback->FindSounds(play_times);
  CheckAlignment(play_times, microphone_times, loopback_times);

  return 0;
}
