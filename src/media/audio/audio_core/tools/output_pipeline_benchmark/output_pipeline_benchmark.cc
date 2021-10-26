// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/tools/output_pipeline_benchmark/output_pipeline_benchmark.h"

#include <lib/fit/defer.h>
#include <lib/zx/time.h>
#include <zircon/device/audio.h>

#include <vector>

#include "src/media/audio/audio_core/process_config_loader.h"
#include "src/media/audio/audio_core/testing/sine_wave_stream.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio {
namespace {

double to_usecs(zx::duration duration) { return static_cast<double>(duration.to_nsecs()) / 1000.0; }

// Records the performance of multiple runs and produces statistics.
class Stats {
 public:
  explicit Stats(perftest::TestCaseResults* result) : perftest_result_(result) {}

  size_t iterations() { return all_.size(); }
  zx::duration total() { return total_; }
  zx::duration mean() { return total_ / all_.size(); }
  zx::duration min() {
    std::sort(all_.begin(), all_.end());
    return all_.front();
  }
  zx::duration max() {
    std::sort(all_.begin(), all_.end());
    return all_.back();
  }
  zx::duration median() {
    std::sort(all_.begin(), all_.end());
    if (all_.size() % 2 == 1) {
      return all_[all_.size() / 2];
    } else {
      return (all_[all_.size() / 2 - 1] + all_[all_.size() / 2]) / 2;
    }
  }

  void Add(zx::duration elapsed) {
    if (perftest_result_) {
      perftest_result_->AppendValue(static_cast<double>(elapsed.get()));
    }
    all_.push_back(elapsed);
    total_ += elapsed;
  }

 private:
  std::string scenario_name_;
  std::vector<zx::duration> all_;
  zx::duration total_;
  perftest::TestCaseResults* perftest_result_;
};

}  // namespace

std::string OutputPipelineBenchmark::Input::ToString() const {
  switch (usage) {
    case RenderUsage::BACKGROUND:
      return "B";
    case RenderUsage::MEDIA:
      return "M";
    case RenderUsage::INTERRUPTION:
      return "I";
    case RenderUsage::SYSTEM_AGENT:
      return "S";
    case RenderUsage::COMMUNICATION:
      return "C";
    case RenderUsage::ULTRASOUND:
      return "U";
    default:
      FX_CHECK(false) << "unknown usage: " << static_cast<int>(usage);
      return "?";
  }
}

std::string OutputPipelineBenchmark::Scenario::ToString() const {
  if (inputs.empty()) {
    return "empty";
  }
  std::string out;
  for (auto& i : inputs) {
    out += i.ToString();
  }
  switch (volume) {
    case VolumeSetting::Muted:
      out += "/VM";
      break;
    case VolumeSetting::Constant:
      out += "/VC";
      break;
    case VolumeSetting::StepChange:
      out += "/VS";
      break;
    case VolumeSetting::RampChange:
      out += "/VR";
      break;
  }
  return out;
}

OutputPipelineBenchmark::Input OutputPipelineBenchmark::Input::FromString(const std::string& str) {
  FX_CHECK(str.size() == 1);
  RenderUsage usage;
  switch (str[0]) {
    case 'B':
      usage = RenderUsage::BACKGROUND;
      break;
    case 'M':
      usage = RenderUsage::MEDIA;
      break;
    case 'I':
      usage = RenderUsage::INTERRUPTION;
      break;
    case 'S':
      usage = RenderUsage::SYSTEM_AGENT;
      break;
    case 'C':
      usage = RenderUsage::COMMUNICATION;
      break;
    case 'U':
      usage = RenderUsage::ULTRASOUND;
      break;
    default:
      FX_CHECK(false) << "unknown usage: " << str;
      __builtin_unreachable();
  }

  // This primarily tests the overall pipeline, not the core mixer,
  // so for now we hardcode fps and channels.
  return {
      .usage = usage,
      .fps = (usage == RenderUsage::ULTRASOUND) ? 96000u : 48000u,
      .channels = 1,
  };
}

OutputPipelineBenchmark::Scenario OutputPipelineBenchmark::Scenario::FromString(
    const std::string& str) {
  if (str == "empty") {
    return Scenario();
  }
  Scenario s;
  size_t k = 0;
  for (; k < str.size() && str[k] != '/'; k++) {
    s.inputs.push_back(Input::FromString(str.substr(k, 1)));
  }
  FX_CHECK(!str.empty()) << "Scenario missing volume setting: " << str;
  if (str.substr(k) == "/VM") {
    s.volume = VolumeSetting::Muted;
  } else if (str.substr(k) == "/VC") {
    s.volume = VolumeSetting::Constant;
  } else if (str.substr(k) == "/VS") {
    s.volume = VolumeSetting::StepChange;
  } else if (str.substr(k) == "/VR") {
    s.volume = VolumeSetting::RampChange;
  } else {
    FX_CHECK(false) << "Scenario has unknown volume setting: " << str;
  }
  return s;
}

std::shared_ptr<OutputPipeline> OutputPipelineBenchmark::CreateOutputPipeline() {
  constexpr char kProcessConfigPath[] = "/config/data/audio_core_config.json";
  auto process_config = ProcessConfigLoader::LoadProcessConfig(kProcessConfigPath);
  FX_CHECK(!process_config.is_error())
      << "Failed to load " << kProcessConfigPath << ": " << process_config.error();

  auto device_profile = process_config.value().device_config().output_device_profile(
      AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS);

  const Format pipeline_format =
      device_profile.pipeline_config().OutputFormat(effects_loader_v2_.get());

  uint32_t fps = pipeline_format.frames_per_second();

  // zx::time(0) == frame 0
  auto ref_time_to_frac_presentation_frame =
      TimelineFunction(TimelineRate(Fixed(fps).raw_value(), zx::sec(1).to_nsecs()));

  return std::make_shared<OutputPipelineImpl>(
      device_profile.pipeline_config(), device_profile.volume_curve(), effects_loader_v2_.get(),
      960, ref_time_to_frac_presentation_frame, *device_clock_);
}

std::unique_ptr<EffectsLoaderV2> OutputPipelineBenchmark::CreateEffectsLoaderV2() {
  auto result = EffectsLoaderV2::CreateFromContext(context_);
  if (result.is_ok()) {
    return std::move(result.value());
  }

  FX_PLOGS(WARNING, result.error())
      << "Failed to connect to V2 effects factory: V2 effects are not available";
  return nullptr;
}

std::shared_ptr<ReadableStream> OutputPipelineBenchmark::CreateInput(const Input& input) {
  // Create a sine wave input. Use an audible frequency for audible inputs
  // and an ultrasonic frequency for ultrasound inputs.
  int64_t period_frames;
  switch (input.fps) {
    case 48000:
      period_frames = 48;  // 1kHz
      break;
    case 96000:
      period_frames = 3;  // 32kHz
      break;
    default:
      period_frames = 10;  // arbitrary
      break;
  }
  return std::make_shared<testing::SineWaveStream<ASF::FLOAT>>(
      Format::Create<ASF::FLOAT>(input.channels, input.fps).value(), period_frames,
      StreamUsage::WithRenderUsage(input.usage),
      clock_factory_->CreateClientAdjustable(zx::time(0), 0));
}

void OutputPipelineBenchmark::PrintLegend(zx::duration mix_period) {
  auto mix_period_ms = static_cast<double>(mix_period.get()) / 1e6;
  printf(
      "\n"
      "    Elapsed time in microseconds for a single %.2fms mix job\n"
      "    for mixer configuration X/VV where X is a list of input\n"
      "    streams, each of which has one of the following usages:\n"
      "\n"
      "        B: BACKGROUND\n"
      "        M: MEDIA\n"
      "        I: INTERRUPTION\n"
      "        S: SYSTEM_AGENT\n"
      "        C: COMMUNICATION\n"
      "        U: ULTRASOUND\n"
      "\n"
      "    and VV is a volume setting:\n"
      "\n"
      "        VM: muted volume\n"
      "        VC: constant volume\n"
      "        VS: discrete volume change just before each mix job (\"stepped\")\n"
      "        VR: ramped volume change just before each mix job\n"
      "\n"
      "Config\t      Mean\t    Median\t      Best\t     Worst\tIterations\n",
      mix_period_ms);
}

void OutputPipelineBenchmark::Run(Scenario scenario, int64_t runs_per_scenario,
                                  zx::duration mix_period, perftest::ResultsSet* results,
                                  bool print_summary) {
  // Create streams for this scenario.
  std::vector<std::shared_ptr<ReadableStream>> streams;
  std::vector<std::shared_ptr<Mixer>> mixers;
  for (auto& input : scenario.inputs) {
    auto stream = CreateInput(input);
    mixers.push_back(output_pipeline_->AddInput(stream, StreamUsage::WithRenderUsage(input.usage)));
    streams.push_back(stream);
  }

  // Ensure streams are removed when the scenario is over.
  auto cleanup = fit::defer([this, streams]() {
    for (auto stream : streams) {
      output_pipeline_->RemoveInput(*stream);
    }
  });

  const int64_t frames_per_mix =
      output_pipeline_->FracPresentationFrameAtRefTime(zx::time(0) + mix_period).Floor();

  Stats stats(results ? results->AddTestCase("fuchsia.audio.output_pipeline",
                                             scenario.ToString().c_str(), "nanoseconds")
                      : nullptr);
  int64_t silent = 0;

  if (scenario.volume == VolumeSetting::Muted) {
    for (auto m : mixers) {
      m->bookkeeping().gain.SetSourceMute(true);
    }
  } else if (scenario.volume == VolumeSetting::Constant) {
    for (auto m : mixers) {
      m->bookkeeping().gain.SetSourceGain(0.0f);
    }
  }

  for (auto iter = 0; iter < runs_per_scenario; iter++) {
    if (scenario.volume == VolumeSetting::StepChange ||
        scenario.volume == VolumeSetting::RampChange) {
      float gain = (iter % 2) == 0 ? 0.0f : -50.0f;
      for (auto m : mixers) {
        if (scenario.volume == VolumeSetting::StepChange) {
          m->bookkeeping().gain.SetSourceGain(gain);
        } else {
          m->bookkeeping().gain.SetSourceGainWithRamp(gain, mix_period);
        }
      }
    }

    Fixed frame_start = output_pipeline_->FracPresentationFrameAtRefTime(device_clock_->Read());

    ReadableStream::ReadLockContext ctx;
    auto t0 = zx::clock::get_monotonic();
    auto got_buffer = output_pipeline_->ReadLock(ctx, frame_start, frames_per_mix).has_value();
    output_pipeline_->Trim(frame_start + Fixed(frames_per_mix));
    auto t1 = zx::clock::get_monotonic();
    stats.Add(t1 - t0);

    if (!got_buffer) {
      silent++;
    }

    clock_factory_->AdvanceMonoTimeBy(mix_period);

    // Our thread (plus the threads for all out-of-process effects) are assigned a deadline
    // profile which is intended to provide enough CPU for a single mix period. If we run
    // each mix period back-to-back, we risk overruning those deadlines, which can make mix
    // jobs take artificially long due to CPU throttling from the kernel. To avoid this, sleep
    // until the end of the mix period.
    if (iter + 1 < runs_per_scenario) {
      zx::nanosleep(zx::deadline_after(mix_period - (t1 - t0)));
    }
  }

  if (print_summary) {
    printf("%s\t%10.3lf\t%10.3lf\t%10.3lf\t%10.3lf\t%10lu\n", scenario.ToString().c_str(),
           to_usecs(stats.mean()), to_usecs(stats.median()), to_usecs(stats.min()),
           to_usecs(stats.max()), stats.iterations());

    // This should never happen: we configure each input to cover the infinite past and
    // future, so as long as we have inputs there should be something to mix.
    if (silent > 0 && !scenario.inputs.empty()) {
      printf("WARNING: %ld of %lu runs produced no output\n", silent, stats.iterations());
    }
  }
}

}  // namespace media::audio
