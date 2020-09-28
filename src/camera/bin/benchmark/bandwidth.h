// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_BENCHMARK_BANDWIDTH_H_
#define SRC_CAMERA_BIN_BENCHMARK_BANDWIDTH_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/hardware/ram/metrics/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fit/source_location.h>

#include <sstream>
#include <string_view>

#include <soc/aml-common/aml-ram.h>

namespace camera::benchmark {

// Measures memory bandwidth consumption by the camera stack.
class Bandwidth {
 public:
  Bandwidth(fuchsia::sysmem::AllocatorHandle sysmem_allocator,
            fuchsia::camera3::DeviceWatcherHandle camera_device_watcher,
            fuchsia::hardware::ram::metrics::DeviceHandle metrics_device,
            async_dispatcher_t* dispatcher);

  // Profiles camera-relevant bandwidth channels for all configurations, writing the results to
  // |sink| in the fuchsia benchmark json format. Invokes |callback| when profiling has completed.
  void Profile(std::ostream& sink, fit::closure callback);

 private:
  void OnDevicesChanged(std::vector<fuchsia::camera3::WatchDevicesEvent> events);
  void OnConfigurationChanged(uint32_t index);
  void StartAllStreams(uint32_t configuration_index, fit::closure callback);
  void ConnectSequential(uint32_t stream_index);

  struct RamChannelMeasurement {
    std::string_view name;
    uint64_t bandwidth_bytes_per_second;
  };
  void MeasureRamChannels(fit::function<void(std::vector<RamChannelMeasurement>)> callback);
  void WriteResults(std::string mode, std::vector<RamChannelMeasurement> results);
  std::ostream& sink();

  fuchsia::sysmem::AllocatorPtr sysmem_allocator_;
  fuchsia::camera3::DeviceWatcherPtr camera_device_watcher_;
  fuchsia::hardware::ram::metrics::DevicePtr metrics_device_;
  async_dispatcher_t* dispatcher_;
  std::ostream* sink_;
  fit::closure callback_;
  fuchsia::camera3::DevicePtr camera_device_;
  std::vector<fuchsia::camera3::Configuration> camera_configurations_;
  struct PerStream {
    fuchsia::camera3::StreamPtr ptr;
    fuchsia::sysmem::BufferCollectionTokenPtr token;
    fuchsia::sysmem::BufferCollectionPtr collection;
    fuchsia::camera3::Stream::GetNextFrameCallback frame_callback;
    uint64_t frames_received = 0;
  };
  std::vector<PerStream> streams_;
  uint32_t warm_streams_ = 0;
  bool first_result_ = true;
};

}  // namespace camera::benchmark

#endif  // SRC_CAMERA_BIN_BENCHMARK_BANDWIDTH_H_
