// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/camera/bin/benchmark/bandwidth.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

namespace camera::benchmark {

// Launching a component or changing configurations may transiently increase bandwidth consumption.
// A delay is used to allow the system to reach steady-state before taking measurements.
constexpr auto kDelayInterval = zx::sec(5);

static fit::function<void(zx_status_t)> MakeErrorHandler(
    std::string name, fit::source_location source_location = fit::source_location::current()) {
  return [name, source_location](zx_status_t status) {
    FX_PLOGS(FATAL, status) << source_location.file_name() + 5 << "(" << source_location.line()
                            << "): " << name << " server disconnected.";
  };
}

Bandwidth::Bandwidth(fuchsia::sysmem::AllocatorHandle sysmem_allocator,
                     fuchsia::camera3::DeviceWatcherHandle camera_device_watcher,
                     fuchsia::hardware::ram::metrics::DeviceHandle metrics_device,
                     async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {
  sysmem_allocator_.set_error_handler(MakeErrorHandler("Camera DeviceWatcher"));
  camera_device_watcher_.set_error_handler(MakeErrorHandler("Camera DeviceWatcher"));
  metrics_device_.set_error_handler(MakeErrorHandler("Metrics Device"));
  sysmem_allocator_.Bind(std::move(sysmem_allocator), dispatcher_);
  camera_device_watcher_.Bind(std::move(camera_device_watcher), dispatcher_);
  metrics_device_.Bind(std::move(metrics_device), dispatcher_);
}

void Bandwidth::Profile(std::ostream& sink, fit::closure callback) {
  sink_ = &sink;
  callback_ = std::move(callback);
  sink << "[";
  async::PostDelayedTask(
      dispatcher_,
      [this] {
        MeasureRamChannels([this](std::vector<RamChannelMeasurement> results) {
          WriteResults("Baseline", results);
          camera_device_watcher_->WatchDevices(
              fit::bind_member(this, &Bandwidth::OnDevicesChanged));
        });
      },
      kDelayInterval);
}

void Bandwidth::OnDevicesChanged(std::vector<fuchsia::camera3::WatchDevicesEvent> events) {
  for (const auto& event : events) {
    if (event.is_added()) {
      camera_device_.set_error_handler(MakeErrorHandler("Camera Device"));
      camera_device_watcher_->ConnectToDevice(event.added(),
                                              camera_device_.NewRequest(dispatcher_));
      camera_device_->GetIdentifier([this](fidl::StringPtr identifier) {
        constexpr auto kSherlockCameraIdentifier = "18D1F00D";
        if (identifier == kSherlockCameraIdentifier) {
          camera_device_->GetConfigurations(
              [this](std::vector<fuchsia::camera3::Configuration> configurations) {
                camera_configurations_ = std::move(configurations);
                camera_device_->WatchCurrentConfiguration(
                    fit::bind_member(this, &Bandwidth::OnConfigurationChanged));
              });
          // Only attempt to benchmark the sherlock camera.
          return;
        }
      });
    }
  }
  camera_device_watcher_->WatchDevices(fit::bind_member(this, &Bandwidth::OnDevicesChanged));
}

void Bandwidth::OnConfigurationChanged(uint32_t index) {
  StartAllStreams(index, [this, index] {
    MeasureRamChannels([this, index](std::vector<RamChannelMeasurement> results) {
      WriteResults("Configuration" + std::to_string(index), results);
      streams_.clear();
      async::PostDelayedTask(
          dispatcher_,
          [this, next = index + 1] {
            if (next < camera_configurations_.size()) {
              camera_device_->SetCurrentConfiguration(next);
              camera_device_->WatchCurrentConfiguration(
                  fit::bind_member(this, &Bandwidth::OnConfigurationChanged));
            } else {
              sink() << "\n]\n";
              callback_();
            }
          },
          kDelayInterval);
    });
  });
}

void Bandwidth::StartAllStreams(uint32_t configuration_index, fit::closure callback) {
  streams_.resize(camera_configurations_[configuration_index].streams.size());
  warm_streams_ = 0;
  fit::closure warm = [this, callback = std::move(callback)] {
    if (++warm_streams_ == streams_.size()) {
      callback();
    }
  };
  for (auto& stream : streams_) {
    stream.ptr.set_error_handler(MakeErrorHandler("Camera Stream"));
    stream.token.set_error_handler(MakeErrorHandler("Sysmem BufferCollectionToken"));
    stream.collection.set_error_handler(MakeErrorHandler("Sysmem BufferCollection"));
    stream.frame_callback = [this, &stream, warm = warm.share()](fuchsia::camera3::FrameInfo info) {
      constexpr uint32_t kWarmupFrames = 20;
      if (++stream.frames_received == kWarmupFrames) {
        warm();
      }
      if (!streams_.empty()) {
        stream.ptr->GetNextFrame(stream.frame_callback.share());
      }
    };
  }
  ConnectSequential(0);
}

void Bandwidth::ConnectSequential(uint32_t stream_index) {
  if (stream_index >= streams_.size()) {
    return;
  }
  camera_device_->ConnectToStream(stream_index, streams_[stream_index].ptr.NewRequest(dispatcher_));
  sysmem_allocator_->AllocateSharedCollection(streams_[stream_index].token.NewRequest());
  streams_[stream_index].token->Sync([this, stream_index] {
    streams_[stream_index].ptr->SetBufferCollection(std::move(streams_[stream_index].token));
    streams_[stream_index].ptr->WatchBufferCollection(
        [this, stream_index](fuchsia::sysmem::BufferCollectionTokenHandle token) {
          sysmem_allocator_->BindSharedCollection(
              std::move(token), streams_[stream_index].collection.NewRequest(dispatcher_));
          streams_[stream_index].collection->SetConstraints(
              true, {.usage{.none = fuchsia::sysmem::noneUsage},
                     .min_buffer_count_for_camping = 2,
                     .has_buffer_memory_constraints = true,
                     .buffer_memory_constraints{.ram_domain_supported = true}});
          streams_[stream_index].collection->WaitForBuffersAllocated(
              [this, stream_index](zx_status_t status,
                                   fuchsia::sysmem::BufferCollectionInfo_2 buffers) {
                streams_[stream_index].ptr->GetNextFrame(
                    streams_[stream_index].frame_callback.share());
                ConnectSequential(stream_index + 1);
              });
        });
  });
}

void Bandwidth::MeasureRamChannels(
    fit::function<void(std::vector<RamChannelMeasurement>)> callback) {
  // Select cycle count such that the duration on Sherlock is an integral number of frames:
  //   Cycles Per Frame = 792000000Hz / 5FPS = 158400000
  //   Max Frames = floor(uint32_t_max / 158400000) = 27
  //   Cycles to Measure = 27 * 158400000 = 4276800000
  constexpr uint64_t kCyclesToMeasure = 4276800000ull;
  static constexpr struct {
    std::string_view name;
    uint64_t port_value;
  } kRamChannels[]{{"CPU", aml_ram::kDefaultChannelCpu},
                   {"ISP", aml_ram::kPortIdMipiIsp},
                   {"GDC", aml_ram::kPortIdGDC},
                   {"GE2D", aml_ram::kPortIdGe2D}};
  fuchsia::hardware::ram::metrics::BandwidthMeasurementConfig measurement_config{
      .cycles_to_measure = kCyclesToMeasure};
  auto channel_port_value = measurement_config.channels.begin();
  for (auto channel : kRamChannels) {
    *channel_port_value++ = channel.port_value;
  }
  metrics_device_->MeasureBandwidth(
      measurement_config,
      [callback = std::move(callback)](
          fuchsia::hardware::ram::metrics::Device_MeasureBandwidth_Result result) {
        if (result.is_err()) {
          FX_PLOGS(FATAL, result.err()) << "Measure failed.";
          return;
        }
        auto& info = result.response().info;
        std::vector<RamChannelMeasurement> results;
        auto channel_cycles = info.channels.begin();
        for (auto channel : kRamChannels) {
          uint64_t channel_bytes = (channel_cycles++)->readwrite_cycles * info.bytes_per_cycle;
          uint64_t channel_bytes_per_second = channel_bytes * kCyclesToMeasure / info.frequency;
          results.push_back(
              {.name = channel.name, .bandwidth_bytes_per_second = channel_bytes_per_second});
        }
        callback(std::move(results));
      });
}

void Bandwidth::WriteResults(std::string mode, std::vector<RamChannelMeasurement> results) {
  for (auto result : results) {
    if (!first_result_) {
      sink() << ",";
    }
    first_result_ = false;
    sink() << "\n";
    sink() << "    {\n";
    sink() << "        \"label\":\"" << mode << "/" << result.name << "\",\n";
    sink() << "        \"test_suite\":\"fuchsia.camera-benchmark\",\n";
    sink() << "        \"unit\":\"bytes/second\",\n";
    sink() << "        \"values\":[" << result.bandwidth_bytes_per_second << "],\n";
    sink() << "        \"split_first\":false\n";
    sink() << "    }";
    sink().flush();
  }
}

std::ostream& Bandwidth::sink() { return *sink_; }

}  // namespace camera::benchmark
