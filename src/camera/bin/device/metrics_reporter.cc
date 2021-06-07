// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/camera/bin/device/metrics_reporter.h"

#include <lib/syslog/cpp/macros.h>

namespace camera {

namespace {

std::mutex factory_mutex;
MetricsReporter* metrics_reporter;
MetricsReporter* metrics_reporter_nop;

}  // anonymous namespace

// MetricsReporter implementation
MetricsReporter& MetricsReporter::Get() {
  std::lock_guard<std::mutex> lock(factory_mutex);
  if (metrics_reporter) {
    return *metrics_reporter;
  } else {
    // If the reporter has not initialized yet, we're returning a nop version of it.
    FX_LOGS(WARNING) << "MetricsReporter is not initialized yet.";
    if (!metrics_reporter_nop) {
      metrics_reporter_nop = new MetricsReporter();
    }
    return *metrics_reporter_nop;
  }
}

void MetricsReporter::Initialize(sys::ComponentContext& context) {
  std::lock_guard<std::mutex> lock(factory_mutex);
  if (metrics_reporter) {
    FX_LOGS(DEBUG) << "MetricsReporter is initialized already.";
    return;
  }

  metrics_reporter = new MetricsReporter(context);
  FX_LOGS(INFO) << "MetricsReporter is initialized.";
}

MetricsReporter::MetricsReporter(sys::ComponentContext& context)
    : impl_(std::make_unique<Impl>(context)) {
  std::lock_guard<std::mutex> lock(mutex_);
  InitInspector();
  // TODO(fxbug.dev/75535): Initializes the Cobalt logger.
}

void MetricsReporter::InitInspector() {
  // Initializes the inspector and creates the configuration node.
  impl_->inspector_ = std::make_unique<sys::ComponentInspector>(&impl_->context_);
  impl_->node_ = impl_->inspector_->root().CreateChild(kConfigurationInspectorNodeName);
}

MetricsReporter::Impl::Impl(sys::ComponentContext& context) : context_(context) {}

MetricsReporter::Impl::~Impl() {}

MetricsReporter::ConfigurationRecord::ConfigurationRecord(MetricsReporter::Impl& impl,
                                                          uint32_t index, size_t num_streams)
    : node_(impl.node_.CreateChild(std::to_string(index))),
      active_node_(node_.CreateBool(kConfigurationInspectorActivePropertyName, false)),
      stream_node_(node_.CreateChild(kStreamInspectorNodeName)) {
  // Creates the number of the stream records.
  for (size_t i = 0; i < num_streams; ++i) {
    stream_records_.emplace_back(impl, stream_node_, i);
  }
}

std::unique_ptr<MetricsReporter::ConfigurationRecord>
MetricsReporter::CreateConfigurationRecord(uint32_t index, size_t num_streams) {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::make_unique<ConfigurationRecord>(*impl_, index, num_streams);
}

MetricsReporter::StreamRecord::StreamRecord(MetricsReporter::Impl& impl, inspect::Node& parent,
                                            uint32_t stream_index)
    : node_(parent.CreateChild(std::to_string(stream_index))),
      frame_rate_(node_.CreateString(kStreamInspectorFrameratePropertyName, "")),
      supports_crop_region_(node_.CreateBool(kStreamInspectorCropPropertyName, false)),
      supported_resolutions_node_(node_.CreateChild(kStreamInspectorResolutionNodeName)),
      frames_received_(node_.CreateUint(kStreamInspectorFramesReceivedPropertyName, 0)),
      frames_dropped_(node_.CreateUint(kStreamInspectorFramesDroppedPropertyName, 0)) {}

void MetricsReporter::StreamRecord::SetProperties(
    const fuchsia::camera3::StreamProperties2& props) {
  frame_rate_.Set(std::to_string(props.frame_rate().numerator) + "/" +
                  std::to_string(props.frame_rate().denominator));
  supports_crop_region_.Set(props.supports_crop_region());
  supported_resolutions_.clear();
  for (const auto& resolution : props.supported_resolutions()) {
    supported_resolutions_.push_back(supported_resolutions_node_.CreateString(
        std::to_string(resolution.width) + "x" + std::to_string(resolution.height), ""));
  }
}

void MetricsReporter::StreamRecord::FrameReceived() { frames_received_.Add(1); }

void MetricsReporter::StreamRecord::FrameDropped() {
  frames_dropped_.Add(1);

  // TODO(fxbug.dev/75535): Reports a frame drop to the Cobalt logger.
}

}  // namespace camera
