// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/camera/bin/device/metrics_reporter.h"

#include <lib/syslog/cpp/macros.h>

namespace camera {

MetricsReporter::MetricsReporter(sys::ComponentContext& component_context)
    : component_context_(component_context),
      inspector_(std::make_unique<sys::ComponentInspector>(&component_context_)),
      configurations_node_(inspector_->root().CreateChild("configurations")) {}

std::unique_ptr<MetricsReporter::Configuration> MetricsReporter::CreateConfiguration(
    uint32_t index, size_t num_streams) {
  return std::make_unique<Configuration>(configurations_node_, index, num_streams);
}

MetricsReporter::Configuration::Configuration(inspect::Node& parent, uint32_t index,
                                              size_t num_streams)
    : node_(parent.CreateChild(std::to_string(index))),
      active_node_(node_.CreateBool("active", false)),
      streams_node_(node_.CreateChild("streams")) {
  for (size_t i = 0; i < num_streams; ++i) {
    streams_.emplace_back(streams_node_, i);
  }
}

MetricsReporter::Stream::Stream(inspect::Node& parent, uint32_t index)
    : node_(parent.CreateChild(std::to_string(index))),
      frame_rate_(node_.CreateString("frame rate", "")),
      supports_crop_region_(node_.CreateBool("supports crop region", false)),
      supported_resolutions_node_(node_.CreateChild("supported resolutions")),
      frames_received_(node_.CreateUint("frames received", 0)),
      frames_dropped_(node_.CreateUint("frames dropped", 0)) {}

void MetricsReporter::Stream::SetProperties(const fuchsia::camera3::StreamProperties2& props) {
  frame_rate_.Set(std::to_string(props.frame_rate().numerator) + "/" +
                  std::to_string(props.frame_rate().denominator));
  supports_crop_region_.Set(props.supports_crop_region());
  supported_resolutions_.clear();
  for (const auto& resolution : props.supported_resolutions()) {
    supported_resolutions_.push_back(supported_resolutions_node_.CreateString(
        std::to_string(resolution.width) + "x" + std::to_string(resolution.height), ""));
  }
}

void MetricsReporter::Stream::FrameReceived() { frames_received_.Add(1); }

void MetricsReporter::Stream::FrameDropped() { frames_dropped_.Add(1); }

}  // namespace camera
