// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/tools/output_pipeline_benchmark/output_pipeline_benchmark.h"

#include <lib/fit/defer.h>
#include <lib/zx/time.h>
#include <zircon/compiler.h>
#include <zircon/device/audio.h>

#include <map>
#include <string>
#include <vector>

#include "src/media/audio/audio_core/shared/process_config.h"
#include "src/media/audio/audio_core/shared/process_config_loader.h"
#include "src/media/audio/audio_core/v1/testing/sine_wave_stream.h"

using ASF = fuchsia::media::AudioSampleFormat;
using StageMetricsVector = media::audio::ReadableStream::ReadLockContext::StageMetricsVector;

namespace media::audio {
namespace {

double to_usecs(zx::duration duration) { return static_cast<double>(duration.to_nsecs()) / 1000.0; }

// Records the performance of multiple runs and produces statistics.
class Stats {
 public:
  explicit Stats(perftest::TestCaseResults* result) : perftest_result_(result) {}

  struct Var {
    std::string name;
    zx::duration min;
    zx::duration p10;
    zx::duration p50;
    zx::duration p90;
    zx::duration max;
  };

  // Returns a mapping from stage name to list of variables measured in that stage.
  // Use a std::map so the keys are sorted.
  std::map<std::string, std::vector<Var>> Summarize() {
    std::map<std::string, std::vector<Var>> out;
    for (auto& [stage_name, metrics] : all_) {
      for (auto& [metric_name, values] : metrics) {
        std::sort(values.begin(), values.end());
        out[stage_name].push_back(Var{
            .name = metric_name,
            .min = values.front(),
            .p10 = PercentileFromSorted(values, 10),
            .p50 = PercentileFromSorted(values, 50),
            .p90 = PercentileFromSorted(values, 90),
            .max = values.back(),
        });
      }
    }
    return out;
  }

  void Add(const StageMetrics& overall_metrics, const StageMetricsVector& per_stage_metrics) {
    if (perftest_result_) {
      perftest_result_->AppendValue(static_cast<double>(overall_metrics.wall_time.get()));
    }
    Add(overall_metrics);
    for (size_t k = 0; k < per_stage_metrics.size(); k++) {
      Add(per_stage_metrics[k]);
    }
  }

 private:
  void Add(const StageMetrics& metrics) {
    std::string name(std::string_view(metrics.name));
    all_[name]["wall"].push_back(metrics.wall_time);
    all_[name]["cpu"].push_back(metrics.cpu_time);
    all_[name]["queue"].push_back(metrics.queue_time);
    all_[name]["page_fault"].push_back(metrics.page_fault_time);
    all_[name]["kernel_locks"].push_back(metrics.kernel_lock_contention_time);
  }

  static zx::duration PercentileFromSorted(const std::vector<zx::duration>& sorted,
                                           int percentile) {
    auto size = static_cast<double>(sorted.size());
    auto pos = static_cast<double>(percentile) / 100 * (size - 1);
    auto pos_int = std::trunc(pos);
    auto pos_frac = pos - pos_int;

    if (static_cast<size_t>(pos_int) == sorted.size()) {
      return sorted.back();
    }

    // LERP between pos_int and pos_int+1.
    auto n = static_cast<size_t>(pos_int);
    auto a = static_cast<double>(sorted[n].get());
    auto b = static_cast<double>(sorted[n + 1].get());
    return zx::nsec(static_cast<int64_t>((1.0 - pos_frac) * a + pos_frac * b));
  }

  std::string scenario_name_;
  // Mapping from stage name => variable name => values.
  // Use a map so keys are sorted.
  std::map<std::string, std::map<std::string, std::vector<zx::duration>>> all_;
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

std::shared_ptr<OutputPipeline> OutputPipelineBenchmark::CreateOutputPipeline(
    const ProcessConfig& process_config, std::shared_ptr<Clock> device_clock,
    EffectsLoaderV2* effects_loader_v2) {
  auto device_profile =
      process_config.device_config().output_device_profile(AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS);

  const Format pipeline_format = device_profile.pipeline_config().OutputFormat(effects_loader_v2);
  uint32_t fps = pipeline_format.frames_per_second();

  // zx::time(0) == frame 0
  auto ref_time_to_frac_presentation_frame =
      TimelineFunction(TimelineRate(Fixed(fps).raw_value(), zx::sec(1).to_nsecs()));

  return std::make_shared<OutputPipelineImpl>(device_profile.pipeline_config(),
                                              device_profile.volume_curve(), effects_loader_v2, 960,
                                              ref_time_to_frac_presentation_frame, device_clock);
}

std::unique_ptr<EffectsLoaderV2> OutputPipelineBenchmark::CreateEffectsLoaderV2(
    sys::ComponentContext& context) {
  auto result = EffectsLoaderV2::CreateFromContext(context);
  if (result.is_ok()) {
    return std::move(result.value());
  }

  FX_PLOGS(WARNING, result.error())
      << "Failed to connect to V2 effects factory: V2 effects are not available";
  return nullptr;
}

ProcessConfig OutputPipelineBenchmark::LoadProcessConfigOrDie() {
  constexpr char kProcessConfigPath[] = "/config/data/audio_core_config.json";
  auto process_config = ProcessConfigLoader::LoadProcessConfig(kProcessConfigPath);
  FX_CHECK(!process_config.is_error())
      << "Failed to load " << kProcessConfigPath << ": " << process_config.error();
  return process_config.take_value();
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
      "    Metrics for a single %.2f ms mix job, displayed in the following format:\n"
      "\n"
      "        config(N runs):\n"
      "          stage1\n"
      "            metric1 [min, 10pp, 50pp, 90pp, max]\n"
      "            metric2 [min, 10pp, 50pp, 90pp, max]\n"
      "            metric3 [min, 10pp, 50pp, 90pp, max]\n"
      "          ...\n"
      "\n"
      "    The 'main' stage covers the full mix job end-to-end, with\n"
      "    per-thread breakdowns computed on the main thread. Additional\n"
      "    stages are pipeline-specific. For example, there might be one\n"
      "    stage for each out-of-process effect invoked by the mix job.\n"
      "\n"
      "    For each metric we give a list of summary statistics (min, max,\n"
      "    and three percentiles). All times are nanoseconds. The metrics are:\n"
      "\n"
      "        wall = wall time, in microseconds\n"
      "        cpu = how long the thread spent running on cpu\n"
      "        queue = how long the thread spent ready to run but waiting to be scheduled\n"
      "        page_fault = how long the thread spent handling page faults\n"
      "        kernel_locks = how long the thread spent blocked on kernel locks\n"
      "\n"
      "    The mixer config has the form X/VV, where X is a list of input\n"
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
      "        VC: constant non-unity volume\n"
      "        VS: discrete volume change just before each mix job ('stepped')\n"
      "        VR: ramping volume change during each mix job\n"
      "\n",
      mix_period_ms);
}

void OutputPipelineBenchmark::PrintHeader() {
  printf("\t\t\t  Min         10%%         50%%         90%%         Max\n");
}

void OutputPipelineBenchmark::Run(Scenario scenario, int64_t runs_per_scenario,
                                  zx::duration mix_period, perftest::ResultsSet* results,
                                  bool print_summary) {
  constexpr float kConstantGainDb = -5.0f;
  constexpr float kAlternateGainDb = -50.0f;

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
  for (auto iter = 0; iter < runs_per_scenario; iter++) {
    std::optional<bool> mute;
    std::optional<float> gain_db, end_gain_db;

    // For Muted and Constant, we only need to set things up once (iter 0)
    switch (scenario.volume) {
      case VolumeSetting::Muted:
        if (iter == 0) {
          mute = true;
        }
        break;

      case VolumeSetting::Constant:
        if (iter == 0) {
          gain_db = kConstantGainDb;
        }
        break;

      case VolumeSetting::StepChange:
        gain_db = (iter % 2) == 0 ? kConstantGainDb : kAlternateGainDb;
        __FALLTHROUGH;
      case VolumeSetting::RampChange:
        end_gain_db = (iter % 2) == 0 ? kAlternateGainDb : kConstantGainDb;
        break;
    }
    for (auto m : mixers) {
      if (mute.has_value()) {
        m->gain.SetSourceMute(*mute);
      }
      if (gain_db.has_value()) {
        m->gain.SetSourceGain(*gain_db);
      }
      if (end_gain_db.has_value()) {
        m->gain.SetSourceGainWithRamp(*end_gain_db, mix_period);
      }
    }

    Fixed frame_start = output_pipeline_->FracPresentationFrameAtRefTime(device_clock_->now());

    ReadableStream::ReadLockContext ctx;
    StageMetricsTimer timer("main");
    timer.Start();
    auto got_buffer = output_pipeline_->ReadLock(ctx, frame_start, frames_per_mix).has_value();
    output_pipeline_->Trim(frame_start + Fixed(frames_per_mix));
    timer.Stop();

    auto overall_metrics = timer.Metrics();
    stats.Add(overall_metrics, ctx.per_stage_metrics());

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
      zx::nanosleep(zx::deadline_after(mix_period - overall_metrics.wall_time));
    }
  }

  if (print_summary) {
    printf("%s (%ld runs):\n", scenario.ToString().c_str(), runs_per_scenario);
    for (auto& [stage_name, vars] : stats.Summarize()) {
      printf("  %s\n", stage_name.c_str());
      for (auto& var : vars) {
        printf("    %-15s [%10.3lf, %10.3lf, %10.3lf, %10.3lf, %10.3lf]\n", var.name.c_str(),
               to_usecs(var.min), to_usecs(var.p10), to_usecs(var.p50), to_usecs(var.p90),
               to_usecs(var.max));
      }
    }

    // This should never happen: we configure each input to cover the infinite past and
    // future, so as long as we have inputs there should be something to mix.
    if (silent > 0 && !scenario.inputs.empty()) {
      printf("WARNING: %ld of %lu runs produced no output\n", silent, runs_per_scenario);
    }
  }
}

}  // namespace media::audio
