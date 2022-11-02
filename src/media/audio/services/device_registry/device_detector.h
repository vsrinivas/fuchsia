// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_DEVICE_REGISTRY_DEVICE_DETECTOR_H_
#define SRC_MEDIA_AUDIO_SERVICES_DEVICE_REGISTRY_DEVICE_DETECTOR_H_

#include <fidl/fuchsia.audio.device/cpp/common_types.h>
#include <fidl/fuchsia.hardware.audio/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/zx/result.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "src/lib/fsl/io/device_watcher.h"

namespace media_audio {

using DeviceDetectionHandler =
    std::function<void(std::string_view, fuchsia_audio_device::DeviceType,
                       fidl::ClientEnd<fuchsia_hardware_audio::StreamConfig>)>;

// This class detects devices and invokes the provided handler for those devices. It uses two
// file-system watchers that focus on the device file system (devfs), specifically the locations
// where registered audio devices are exposed (dev/class/audio-input and dev/class/audio-output).
class DeviceDetector {
 public:
  // Immediately kick off watchers in 'devfs' directories where audio devices are found.
  // Upon detection, our DeviceDetectionHandler is run on the dispatcher's thread.
  static zx::result<std::shared_ptr<DeviceDetector>> Create(DeviceDetectionHandler handler,
                                                            async_dispatcher_t* dispatcher);
  virtual ~DeviceDetector() = default;

 private:
  DeviceDetector(DeviceDetectionHandler handler, async_dispatcher_t* dispatcher)
      : handler_(std::move(handler)), dispatcher_(dispatcher) {}
  DeviceDetector() = delete;

  zx_status_t StartDeviceWatchers();

  // Open a devnode at the given path; use its FDIO device channel as a StreamConfigConnector to
  // connect (retrieve) the device's StreamConfig.
  void StreamConfigFromDevFs(int dir_fd, std::string_view name,
                             fuchsia_audio_device::DeviceType device_type);

  DeviceDetectionHandler handler_;
  std::vector<std::unique_ptr<fsl::DeviceWatcher>> watchers_;

  async_dispatcher_t* dispatcher_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_DEVICE_REGISTRY_DEVICE_DETECTOR_H_
