// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_METRICS_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_METRICS_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>
#include <unordered_map>

#include <src/lib/cobalt/cpp/cobalt_event_builder.h>
#include <src/lib/cobalt/cpp/cobalt_logger.h>
#include <src/lib/metrics_buffer/metrics_buffer.h>

#include <src/media/lib/metrics/metrics.cb.h>

// Methods of this class can be called on any thread.
//
// TODO(fxb/86491): This wrapper is temporary to minimize files with diffs in a CL for fxb/86491.
// We can switch to using MetricsBuffer/MetricBuffer directly in a later CL.
class CodecMetrics final {
 public:
  // A nop instance so unit tests don't need to wire up cobalt.
  CodecMetrics();

  // !service_directory is ok.  If !service_directory, the instance will be a nop instance until
  // SetServiceDirectory() is called.
  CodecMetrics(std::shared_ptr<sys::ServiceDirectory> service_directory);

  ~CodecMetrics();

  // Set the ServiceDirectory from which to get fuchsia.cobalt.LoggerFactory.  This can be nullptr.
  // This can be called again, regardless of whether there was already a previous ServiceDirectory.
  // Previously-queued events may be lost (especially recently-queued events) when switching to a
  // new ServiceDirectory.
  void SetServiceDirectory(std::shared_ptr<sys::ServiceDirectory> service_directory);

  // Log the event as EVENT_COUNT, with period_duration_micros 0, possibly aggregating with any
  // other calls to this method with the same component and event wihtin a short duration to limit
  // the rate of FIDL calls to cobalt, per the rate requirement/recommendation in the cobalt docs.
  //
  // No attempt is made to flush pending events before driver exit or suspend, since this driver
  // isn't expected to unbind very often, if ever, and if we're suspending already then it's
  // unlikely that the pending cobalt events would be persisted anyway.
  void LogEvent(media_metrics::StreamProcessorEvents2MetricDimensionImplementation implementation,
                media_metrics::StreamProcessorEvents2MetricDimensionEvent event);

 private:
  std::shared_ptr<cobalt::MetricsBuffer> metrics_buffer_;
  cobalt::MetricBuffer metric_buffer_;
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_METRICS_H_
