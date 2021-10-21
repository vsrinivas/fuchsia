// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/codec_impl/codec_metrics.h"

#include <inttypes.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/syslog/global.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <mutex>

#include "lib/async/cpp/task.h"
#include "lib/media/codec_impl/log.h"
#include "src/lib/cobalt/cpp/cobalt_event_builder.h"
#include "src/media/lib/metrics/metrics.cb.h"

CodecMetrics::CodecMetrics()
    : metrics_buffer_(cobalt::MetricsBuffer::Create(media_metrics::kProjectId)),
      metric_buffer_(
          metrics_buffer_->CreateMetricBuffer(media_metrics::kStreamProcessorEvents2MetricId)) {}

CodecMetrics::CodecMetrics(std::shared_ptr<sys::ServiceDirectory> service_directory)
    : metrics_buffer_(cobalt::MetricsBuffer::Create(media_metrics::kProjectId, service_directory)),
      metric_buffer_(
          metrics_buffer_->CreateMetricBuffer(media_metrics::kStreamProcessorEvents2MetricId)) {}

CodecMetrics::~CodecMetrics() {}

void CodecMetrics::SetServiceDirectory(std::shared_ptr<sys::ServiceDirectory> service_directory) {
  metrics_buffer_->SetServiceDirectory(service_directory);
}

void CodecMetrics::LogEvent(
    media_metrics::StreamProcessorEvents2MetricDimensionImplementation implementation,
    media_metrics::StreamProcessorEvents2MetricDimensionEvent event) {
  metric_buffer_.LogEvent({implementation, event});
}
