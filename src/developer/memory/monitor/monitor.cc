// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/monitor.h"

#include <errno.h>
#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/vfs/cpp/internal/file.h>
#include <lib/vfs/cpp/vmo_file.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>

#include <soc/aml-common/aml-ram.h>
#include <trace-vthread/event_vthread.h>

#include "fuchsia/mem/cpp/fidl.h"
#include "lib/fpromise/result.h"
#include "src/developer/memory/metrics/bucket_match.h"
#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/printer.h"
#include "src/developer/memory/monitor/high_water.h"
#include "src/developer/memory/monitor/memory_metrics_registry.cb.h"
#include "src/developer/memory/monitor/pressure_observer.h"
#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

namespace monitor {

using namespace memory;

const char Monitor::kTraceName[] = "memory_monitor";

namespace {
// Path to the configuration file for buckets.
const std::string kBucketConfigPath = "/config/data/buckets.json";
const zx::duration kHighWaterPollFrequency = zx::sec(10);
const uint64_t kHighWaterThreshold = 10 * 1024 * 1024;
const zx::duration kMetricsPollFrequency = zx::min(5);
const char kTraceNameHighPrecisionBandwidth[] = "memory_monitor:high_precision_bandwidth";
const char kTraceNameHighPrecisionBandwidthCamera[] =
    "memory_monitor:high_precision_bandwidth_camera";
constexpr uint64_t kMaxPendingBandwidthMeasurements = 4;
constexpr uint64_t kMemCyclesToMeasure = 792000000 / 20;                 // 50 ms on sherlock
constexpr uint64_t kMemCyclesToMeasureHighPrecision = 792000000 / 1000;  // 1 ms
// TODO(fxbug.dev/48254): Get default channel information through the FIDL API.
struct RamChannel {
  const char* name;
  uint64_t mask;
};
constexpr RamChannel kRamDefaultChannels[] = {
    {.name = "cpu", .mask = aml_ram::kDefaultChannelCpu},
    {.name = "gpu", .mask = aml_ram::kDefaultChannelGpu},
    {.name = "vdec", .mask = aml_ram::kDefaultChannelVDec},
    {.name = "vpu", .mask = aml_ram::kDefaultChannelVpu},
};
constexpr RamChannel kRamCameraChannels[] = {
    {.name = "cpu", .mask = aml_ram::kDefaultChannelCpu},
    {.name = "isp", .mask = aml_ram::kPortIdMipiIsp},
    {.name = "gdc", .mask = aml_ram::kPortIdGDC},
    {.name = "ge2d", .mask = aml_ram::kPortIdGe2D},
};
uint64_t CounterToBandwidth(uint64_t counter, uint64_t frequency, uint64_t cycles) {
  return counter * frequency / cycles;
}
zx_ticks_t TimestampToTicks(zx_time_t timestamp) {
  __uint128_t temp = static_cast<__uint128_t>(timestamp) * zx_ticks_per_second() / ZX_SEC(1);
  return static_cast<zx_ticks_t>(temp);
}
fuchsia::hardware::ram::metrics::BandwidthMeasurementConfig BuildConfig(
    uint64_t cycles_to_measure, bool use_camera_channels = false) {
  fuchsia::hardware::ram::metrics::BandwidthMeasurementConfig config = {};
  config.cycles_to_measure = cycles_to_measure;
  size_t num_channels = std::size(kRamDefaultChannels);
  const auto* channels = kRamDefaultChannels;
  if (use_camera_channels) {
    num_channels = std::size(kRamCameraChannels);
    channels = kRamCameraChannels;
  }
  for (size_t i = 0; i < num_channels; i++) {
    config.channels[i] = channels[i].mask;
  }
  return config;
}
uint64_t TotalReadWriteCycles(const fuchsia::hardware::ram::metrics::BandwidthInfo& info) {
  uint64_t total_readwrite_cycles = 0;
  for (auto& channel : info.channels) {
    total_readwrite_cycles += channel.readwrite_cycles;
  }
  return total_readwrite_cycles;
}

std::vector<memory::BucketMatch> CreateBucketMatchesFromConfigData() {
  std::error_code _ignore;
  if (!std::filesystem::exists(kBucketConfigPath, _ignore)) {
    FX_LOGS(WARNING) << "Bucket configuration file not found; no buckets will be available.";
    return {};
  }

  std::string configuration_str;
  FX_CHECK(files::ReadFileToString(kBucketConfigPath, &configuration_str));
  auto bucket_matches = memory::BucketMatch::ReadBucketMatchesFromConfig(configuration_str);
  FX_CHECK(bucket_matches.has_value()) << "Unable to read configuration: " << configuration_str;
  return std::move(*bucket_matches);
}

}  // namespace

Monitor::Monitor(std::unique_ptr<sys::ComponentContext> context,
                 const fxl::CommandLine& command_line, async_dispatcher_t* dispatcher,
                 bool send_metrics, bool watch_memory_pressure,
                 bool send_critical_pressure_crash_reports)
    : prealloc_size_(0),
      logging_(command_line.HasOption("log")),
      tracing_(false),
      delay_(zx::sec(1)),
      dispatcher_(dispatcher),
      component_context_(std::move(context)),
      inspector_(component_context_.get()),
      logger_(
          dispatcher_, [this](Capture* c) { return GetCapture(c); },
          [this](const Capture& c, Digest* d) { GetDigest(c, d); }),
      level_(Level::kNumLevels) {
  auto bucket_matches = CreateBucketMatchesFromConfigData();
  digester_ = std::make_unique<Digester>(Digester(bucket_matches));
  high_water_ = std::make_unique<HighWater>(
      "/cache", kHighWaterPollFrequency, kHighWaterThreshold, dispatcher,
      [this](Capture* c, CaptureLevel l) { return Capture::GetCapture(c, capture_state_, l); },
      [this](const Capture& c, Digest* d) { digester_->Digest(c, d); });
  auto s = Capture::GetCaptureState(&capture_state_);
  if (s != ZX_OK) {
    FX_LOGS(ERROR) << "Error getting capture state: " << zx_status_get_string(s);
    exit(EXIT_FAILURE);
  }

  if (send_metrics)
    CreateMetrics(bucket_matches);

  // Expose lazy values under the root, populated from the Inspect method.
  inspector_.root().CreateLazyValues(
      "memory_measurements",
      [this, bucket_matches = std::move(bucket_matches)] {
        return fpromise::make_result_promise(fpromise::ok(Inspect(bucket_matches)));
      },
      &inspector_);

  component_context_->outgoing()->AddPublicService(bindings_.GetHandler(this));

  if (command_line.HasOption("help")) {
    PrintHelp();
    exit(EXIT_SUCCESS);
  }
  std::string delay_as_string;
  if (command_line.GetOptionValue("delay", &delay_as_string)) {
    unsigned delay_as_int;
    if (!fxl::StringToNumberWithError<unsigned>(delay_as_string, &delay_as_int)) {
      FX_LOGS(ERROR) << "Invalid value for delay: " << delay_as_string;
      exit(-1);
    }
    delay_ = zx::msec(delay_as_int);
  }
  std::string prealloc_as_string;
  if (command_line.GetOptionValue("prealloc", &prealloc_as_string)) {
    FX_LOGS(INFO) << "prealloc_string: " << prealloc_as_string;
    if (!fxl::StringToNumberWithError<uint64_t>(prealloc_as_string, &prealloc_size_)) {
      FX_LOGS(ERROR) << "Invalid value for prealloc: " << prealloc_as_string;
      exit(-1);
    }
    prealloc_size_ *= (1024 * 1024);
    auto status = zx::vmo::create(prealloc_size_, 0, &prealloc_vmo_);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "zx::vmo::create() returns " << zx_status_get_string(status);
      exit(-1);
    }
    prealloc_vmo_.get_size(&prealloc_size_);
    uintptr_t prealloc_addr = 0;
    status = zx::vmar::root_self()->map(ZX_VM_PERM_READ, 0, prealloc_vmo_, 0, prealloc_size_,
                                        &prealloc_addr);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "zx::vmar::map() returns " << zx_status_get_string(status);
      exit(-1);
    }

    status = prealloc_vmo_.op_range(ZX_VMO_OP_COMMIT, 0, prealloc_size_, NULL, 0);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "zx::vmo::op_range() returns " << zx_status_get_string(status);
      exit(-1);
    }
  }

  trace_observer_.Start(dispatcher_, [this] { UpdateState(); });
  if (logging_) {
    Capture capture;
    auto s = Capture::GetCapture(&capture, capture_state_, KMEM);
    if (s != ZX_OK) {
      FX_LOGS(ERROR) << "Error getting capture: " << zx_status_get_string(s);
      exit(EXIT_FAILURE);
    }
    const auto& kmem = capture.kmem();
    FX_LOGS(INFO) << "Total: " << kmem.total_bytes << " Wired: " << kmem.wired_bytes
                  << " Total Heap: " << kmem.total_heap_bytes;
  }

  pressure_notifier_ = std::make_unique<PressureNotifier>(
      watch_memory_pressure, send_critical_pressure_crash_reports, component_context_.get(),
      dispatcher, [this](Level l) { PressureLevelChanged(l); });
  memory_debugger_ =
      std::make_unique<MemoryDebugger>(component_context_.get(), pressure_notifier_.get());

  SampleAndPost();
}

Monitor::~Monitor() {}

void Monitor::SetRamDevice(fuchsia::hardware::ram::metrics::DevicePtr ptr) {
  ram_device_ = std::move(ptr);
  if (ram_device_.is_bound())
    PeriodicMeasureBandwidth();
}

void Monitor::CreateMetrics(const std::vector<memory::BucketMatch>& bucket_matches) {
  // Connect to the metrics fidl service provided by the environment.
  fuchsia::metrics::MetricEventLoggerFactorySyncPtr factory;
  component_context_->svc()->Connect(factory.NewRequest());
  if (!factory) {
    FX_LOGS(ERROR) << "Unable to get metrics.MetricEventLoggerFactory.";
    return;
  }
  // Create a Metric Event Logger. The ID name is the one we specified in the
  // Cobalt metrics registry.
  fuchsia::metrics::ProjectSpec project_spec;
  project_spec.set_customer_id(cobalt_registry::kCustomerId);
  project_spec.set_project_id(cobalt_registry::kProjectId);
  fuchsia::metrics::MetricEventLoggerFactory_CreateMetricEventLogger_Result result =
      fpromise::error(fuchsia::metrics::Error::INTERNAL_ERROR);
  factory->CreateMetricEventLogger(std::move(project_spec), metric_event_logger_.NewRequest(),
                                   &result);
  if (result.is_err()) {
    FX_LOGS(ERROR) << "Unable to get metrics.Logger from factory.";
    return;
  }

  metrics_ = std::make_unique<Metrics>(
      bucket_matches, kMetricsPollFrequency, dispatcher_, &inspector_, metric_event_logger_.get(),
      [this](Capture* c) { return GetCapture(c); },
      [this](const Capture& c, Digest* d) { GetDigest(c, d); });
}

void Monitor::Watch(fidl::InterfaceHandle<fuchsia::memory::Watcher> watcher) {
  fuchsia::memory::WatcherPtr watcher_proxy = watcher.Bind();
  fuchsia::memory::Watcher* proxy_raw_ptr = watcher_proxy.get();
  watcher_proxy.set_error_handler(
      [this, proxy_raw_ptr](zx_status_t status) { ReleaseWatcher(proxy_raw_ptr); });
  watchers_.push_back(std::move(watcher_proxy));
  SampleAndPost();
}

void Monitor::WriteJsonCapture(zx::socket socket) {
  // Capture state and store it in a string.
  Capture capture;
  const zx_status_t capture_status = GetCapture(&capture);
  if (capture_status != ZX_OK) {
    FX_LOGS(ERROR) << "Error getting capture: " << zx_status_get_string(capture_status);
    return;
  }
  std::stringstream stream;
  Printer printer(stream);
  printer.PrintCapture(capture);
  // TODO(b/229972119): avoid a copy by having the stream write directly to the socket.
  const std::string json_string = stream.str();

  // Send string through socket.
  fsl::BlockingCopyFromString(json_string, socket);
}

void Monitor::WriteJsonCaptureAndBuckets(zx::socket socket) {
  // Capture state.
  Capture capture;
  const zx_status_t capture_status = GetCapture(&capture);
  if (capture_status != ZX_OK) {
    FX_LOGS(ERROR) << "Error getting capture: " << zx_status_get_string(capture_status);
    return;
  }

  // Copy the bucket definition into a string
  std::string configuration_str;
  std::error_code _ignore;
  if (!std::filesystem::exists(kBucketConfigPath, _ignore)) {
    FX_LOGS(ERROR) << "Bucket configuration file not found; no buckets will be available.";
    configuration_str = "[]";
  } else {
    FX_CHECK(files::ReadFileToString(kBucketConfigPath, &configuration_str));
  }
  std::stringstream stream;
  Printer printer(stream);
  printer.PrintCaptureAndBucketConfig(capture, configuration_str);
  // TODO(b/229972119): avoid a copy by having the stream write directly to the socket.
  const std::string json_string = stream.str();

  // Send string through socket.
  fsl::BlockingCopyFromString(json_string, socket);
}

void Monitor::ReleaseWatcher(fuchsia::memory::Watcher* watcher) {
  auto predicate = [watcher](const auto& target) { return target.get() == watcher; };
  watchers_.erase(std::remove_if(watchers_.begin(), watchers_.end(), predicate));
}

void Monitor::NotifyWatchers(const zx_info_kmem_stats_t& kmem_stats) {
  fuchsia::memory::Stats stats{
      .total_bytes = kmem_stats.total_bytes,
      .free_bytes = kmem_stats.free_bytes,
      .wired_bytes = kmem_stats.wired_bytes,
      .total_heap_bytes = kmem_stats.total_heap_bytes,
      .free_heap_bytes = kmem_stats.free_heap_bytes,
      .vmo_bytes = kmem_stats.vmo_bytes,
      .mmu_overhead_bytes = kmem_stats.mmu_overhead_bytes,
      .ipc_bytes = kmem_stats.ipc_bytes,
      .other_bytes = kmem_stats.other_bytes,
  };

  for (auto& watcher : watchers_) {
    watcher->OnChange(stats);
  }
}

void Monitor::PrintHelp() {
  std::cout << "memory_monitor [options]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --log" << std::endl;
  std::cout << "  --prealloc=kbytes" << std::endl;
  std::cout << "  --delay=msecs" << std::endl;
}

inspect::Inspector Monitor::Inspect(const std::vector<memory::BucketMatch>& bucket_matches) {
  inspect::Inspector inspector(inspect::InspectSettings{.maximum_size = 1024 * 1024});
  auto& root = inspector.GetRoot();
  Capture capture;
  Capture::GetCapture(&capture, capture_state_, VMO);

  Summary summary(capture, Summary::kNameMatches);
  std::ostringstream summary_stream;
  Printer summary_printer(summary_stream);
  summary_printer.PrintSummary(summary, VMO, SORTED);
  auto current_string = summary_stream.str();
  auto high_water_string = high_water_->GetHighWater();
  auto previous_high_water_string = high_water_->GetPreviousHighWater();
  if (!current_string.empty()) {
    root.CreateString("current", current_string, &inspector);
  }
  if (!high_water_string.empty()) {
    root.CreateString("high_water", high_water_string, &inspector);
  }
  if (!previous_high_water_string.empty()) {
    root.CreateString("high_water_previous_boot", previous_high_water_string, &inspector);
  }

  // Expose raw values for downstream computation.
  auto values = root.CreateChild("values");
  values.CreateUint("free_bytes", capture.kmem().free_bytes, &inspector);
  values.CreateUint("free_heap_bytes", capture.kmem().free_heap_bytes, &inspector);
  values.CreateUint("ipc_bytes", capture.kmem().ipc_bytes, &inspector);
  values.CreateUint("mmu_overhead_bytes", capture.kmem().mmu_overhead_bytes, &inspector);
  values.CreateUint("other_bytes", capture.kmem().other_bytes, &inspector);
  values.CreateUint("total_bytes", capture.kmem().total_bytes, &inspector);
  values.CreateUint("total_heap_bytes", capture.kmem().total_heap_bytes, &inspector);
  values.CreateUint("vmo_bytes", capture.kmem().vmo_bytes, &inspector);
  values.CreateUint("wired_bytes", capture.kmem().wired_bytes, &inspector);
  inspector.emplace(std::move(values));

  Digest digest;
  digester_->Digest(capture, &digest);
  std::ostringstream digest_stream;
  Printer digest_printer(digest_stream);
  digest_printer.PrintDigest(digest);
  auto current_digest_string = digest_stream.str();
  auto high_water_digest_string = high_water_->GetHighWaterDigest();
  auto previous_high_water_digest_string = high_water_->GetPreviousHighWaterDigest();
  if (!current_digest_string.empty()) {
    root.CreateString("current_digest", current_digest_string, &inspector);
  }
  if (!high_water_digest_string.empty()) {
    root.CreateString("high_water_digest", high_water_digest_string, &inspector);
  }
  if (!previous_high_water_digest_string.empty()) {
    root.CreateString("high_water_digest_previous_boot", previous_high_water_digest_string,
                      &inspector);
  }

  return inspector;
}

void Monitor::SampleAndPost() {
  if (logging_ || tracing_ || watchers_.size() > 0) {
    Capture capture;
    auto s = Capture::GetCapture(&capture, capture_state_, KMEM);
    if (s != ZX_OK) {
      FX_LOGS(ERROR) << "Error getting capture: " << zx_status_get_string(s);
      return;
    }
    const auto& kmem = capture.kmem();
    if (logging_) {
      FX_LOGS(INFO) << "Free: " << kmem.free_bytes << " Free Heap: " << kmem.free_heap_bytes
                    << " VMO: " << kmem.vmo_bytes << " MMU: " << kmem.mmu_overhead_bytes
                    << " IPC: " << kmem.ipc_bytes;
    }
    if (tracing_) {
      TRACE_COUNTER(kTraceName, "allocated", 0, "vmo", kmem.vmo_bytes, "mmu_overhead",
                    kmem.mmu_overhead_bytes, "ipc", kmem.ipc_bytes);
      TRACE_COUNTER(kTraceName, "free", 0, "free", kmem.free_bytes, "free_heap",
                    kmem.free_heap_bytes);
    }
    NotifyWatchers(kmem);
    async::PostDelayedTask(
        dispatcher_, [this] { SampleAndPost(); }, delay_);
  }
}

void Monitor::MeasureBandwidthAndPost() {
  // Bandwidth measurements are cheap but they take some time to
  // perform as they run over a number of memory cycles. In order to
  // support a relatively small cycle count for measurements, we keep
  // multiple requests in-flight. This gives us results with high
  // granularity and relatively good coverage.
  while (tracing_ && pending_bandwidth_measurements_ < kMaxPendingBandwidthMeasurements) {
    uint64_t cycles_to_measure = kMemCyclesToMeasure;
    bool trace_high_precision = trace_is_category_enabled(kTraceNameHighPrecisionBandwidth);
    bool trace_high_precision_camera =
        trace_is_category_enabled(kTraceNameHighPrecisionBandwidthCamera);
    if (trace_high_precision && trace_high_precision_camera) {
      FX_LOGS(ERROR) << kTraceNameHighPrecisionBandwidth << " and "
                     << kTraceNameHighPrecisionBandwidthCamera
                     << " are mutually exclusive categories.";
    }
    if (trace_high_precision || trace_high_precision_camera) {
      cycles_to_measure = kMemCyclesToMeasureHighPrecision;
    }
    ++pending_bandwidth_measurements_;
    ram_device_->MeasureBandwidth(
        BuildConfig(cycles_to_measure, trace_high_precision_camera),
        [this, cycles_to_measure, trace_high_precision_camera](
            fuchsia::hardware::ram::metrics::Device_MeasureBandwidth_Result result) {
          --pending_bandwidth_measurements_;
          if (result.is_err()) {
            FX_LOGS(ERROR) << "Bad bandwidth measurement result: " << result.err();
          } else {
            const auto& info = result.response().info;
            uint64_t total_readwrite_cycles = TotalReadWriteCycles(info);
            uint64_t other_readwrite_cycles =
                (info.total.readwrite_cycles > total_readwrite_cycles)
                    ? info.total.readwrite_cycles - total_readwrite_cycles
                    : 0;
            static_assert(std::size(kRamDefaultChannels) == std::size(kRamCameraChannels));
            const auto* channels =
                trace_high_precision_camera ? kRamCameraChannels : kRamDefaultChannels;
            TRACE_VTHREAD_COUNTER(
                kTraceName, "bandwidth_usage", "membw" /*vthread_literal*/, 1 /*vthread_id*/,
                0 /*counter_id*/, TimestampToTicks(info.timestamp), channels[0].name,
                CounterToBandwidth(info.channels[0].readwrite_cycles, info.frequency,
                                   cycles_to_measure) *
                    info.bytes_per_cycle,
                channels[1].name,
                CounterToBandwidth(info.channels[1].readwrite_cycles, info.frequency,
                                   cycles_to_measure) *
                    info.bytes_per_cycle,
                channels[2].name,
                CounterToBandwidth(info.channels[2].readwrite_cycles, info.frequency,
                                   cycles_to_measure) *
                    info.bytes_per_cycle,
                channels[3].name,
                CounterToBandwidth(info.channels[3].readwrite_cycles, info.frequency,
                                   cycles_to_measure) *
                    info.bytes_per_cycle,
                "other",
                CounterToBandwidth(other_readwrite_cycles, info.frequency, cycles_to_measure) *
                    info.bytes_per_cycle);
            TRACE_VTHREAD_COUNTER(kTraceName, "bandwidth_free", "membw" /*vthread_literal*/,
                                  1 /*vthread_id*/, 0 /*counter_id*/,
                                  TimestampToTicks(info.timestamp), "value",
                                  CounterToBandwidth(cycles_to_measure - total_readwrite_cycles -
                                                         other_readwrite_cycles,
                                                     info.frequency, cycles_to_measure) *
                                      info.bytes_per_cycle);
          }
          async::PostTask(dispatcher_, [this] { MeasureBandwidthAndPost(); });
        });
  }
}

void Monitor::PeriodicMeasureBandwidth() {
  std::chrono::seconds seconds_to_sleep = std::chrono::seconds(1);
  async::PostDelayedTask(
      dispatcher_, [this]() { PeriodicMeasureBandwidth(); }, zx::sec(seconds_to_sleep.count()));

  // Will not do measurement when tracing
  if (tracing_)
    return;

  uint64_t cycles_to_measure = kMemCyclesToMeasure;
  ram_device_->MeasureBandwidth(
      BuildConfig(cycles_to_measure),
      [this,
       cycles_to_measure](fuchsia::hardware::ram::metrics::Device_MeasureBandwidth_Result result) {
        if (result.is_err()) {
          FX_LOGS(ERROR) << "Bad bandwidth measurement result: " << result.err();
        } else {
          const auto& info = result.response().info;
          uint64_t total_readwrite_cycles = TotalReadWriteCycles(info);
          total_readwrite_cycles = std::max(total_readwrite_cycles, info.total.readwrite_cycles);

          uint64_t memory_bandwidth_reading =
              CounterToBandwidth(total_readwrite_cycles, info.frequency, cycles_to_measure) *
              info.bytes_per_cycle;
          if (metrics_)
            metrics_->NextMemoryBandwidthReading(memory_bandwidth_reading, info.timestamp);
        }
      });
}

void Monitor::UpdateState() {
  if (trace_state() == TRACE_STARTED) {
    if (trace_is_category_enabled(kTraceName)) {
      FX_LOGS(INFO) << "Tracing started";
      if (!tracing_) {
        Capture capture;
        auto s = Capture::GetCapture(&capture, capture_state_, KMEM);
        if (s != ZX_OK) {
          FX_LOGS(ERROR) << "Error getting capture: " << zx_status_get_string(s);
          return;
        }
        const auto& kmem = capture.kmem();
        TRACE_COUNTER(kTraceName, "fixed", 0, "total", kmem.total_bytes, "wired", kmem.wired_bytes,
                      "total_heap", kmem.total_heap_bytes);
        tracing_ = true;
        if (!logging_) {
          SampleAndPost();
        }
        if (ram_device_.is_bound()) {
          MeasureBandwidthAndPost();
        }
      }
    }
  } else {
    if (tracing_) {
      FX_LOGS(INFO) << "Tracing stopped";
      tracing_ = false;
    }
  }
}

zx_status_t Monitor::GetCapture(memory::Capture* capture) {
  return Capture::GetCapture(capture, capture_state_, VMO);
}

void Monitor::GetDigest(const memory::Capture& capture, memory::Digest* digest) {
  std::lock_guard<std::mutex> lock(digester_mutex_);
  digester_->Digest(capture, digest);
}

void Monitor::PressureLevelChanged(Level level) {
  if (level == kImminentOOM) {
    // Force the current state to be written as the high_waters. Later is better.
    memory::Capture c;
    auto s = GetCapture(&c);
    if (s == ZX_OK) {
      high_water_->RecordHighWater(c);
      high_water_->RecordHighWaterDigest(c);
    } else {
      FX_LOGS(ERROR) << "Error getting capture: " << zx_status_get_string(s);
    }
  }
  if (level == level_) {
    return;
  }
  FX_LOGS(INFO) << "Memory pressure level changed from " << kLevelNames[level_] << " to "
                << kLevelNames[level];
  TRACE_INSTANT("memory_monitor", "MemoryPressureLevelChange", TRACE_SCOPE_THREAD, "from",
                kLevelNames[level_], "to", kLevelNames[level]);

  level_ = level;
  logger_.SetPressureLevel(level_);
}

}  // namespace monitor
