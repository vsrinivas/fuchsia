// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_METRICS_REPORTER_H_
#define SRC_CAMERA_BIN_DEVICE_METRICS_REPORTER_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/inspect/cpp/component.h>

namespace camera {

// |MetricsReporter| handles instrumentation concerns (e.g. exposing information via inspect,
// cobalt, etc) for a camera device instance.
class MetricsReporter {
 public:
  explicit MetricsReporter(sys::ComponentContext& component_context);

  const inspect::Inspector& inspector() { return *inspector_->inspector(); }

  class Stream {
   public:
    Stream(inspect::Node& parent, uint32_t index);

    void SetProperties(const fuchsia::camera3::StreamProperties2&);
    void FrameReceived();
    void FrameDropped();

   private:
    inspect::Node node_;

    inspect::StringProperty frame_rate_;
    inspect::BoolProperty supports_crop_region_;
    inspect::Node supported_resolutions_node_;
    std::vector<inspect::StringProperty> supported_resolutions_;

    inspect::UintProperty frames_received_;
    inspect::UintProperty frames_dropped_;
  };

  class Configuration {
   public:
    Configuration(inspect::Node& parent, uint32_t index, size_t num_streams);

    // Returns a reference to the metrics object for the stream with `index`, where
    // 0 <= index < num_streams.
    Stream& stream(uint32_t index) { return streams_[index]; }

    void SetActive(bool active) { active_node_.Set(active); }

   private:
    inspect::Node node_;
    inspect::BoolProperty active_node_;
    inspect::Node streams_node_;
    std::vector<Stream> streams_;
  };

  std::unique_ptr<Configuration> CreateConfiguration(uint32_t index, size_t num_streams);

 private:
  sys::ComponentContext& component_context_;
  std::unique_ptr<sys::ComponentInspector> inspector_;

  inspect::Node configurations_node_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_DEVICE_METRICS_REPORTER_H_
