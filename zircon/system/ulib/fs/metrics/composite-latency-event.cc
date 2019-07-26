// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/metrics/composite-latency-event.h>

namespace fs_metrics {

namespace internal {
cobalt_client::Histogram<fs_metrics::VnodeMetrics::kHistogramBuckets>* SelectHistogram(
    const Event event, fs_metrics::VnodeMetrics* metrics) {
  switch (event) {
    case Event::kClose:
      return &metrics->close;

    case Event::kRead:
      return &metrics->read;

    case Event::kWrite:
      return &metrics->write;

    case Event::kAppend:
      return &metrics->append;

    case Event::kTruncate:
      return &metrics->truncate;

    case Event::kSetAttr:
      return &metrics->set_attr;

    case Event::kGetAttr:
      return &metrics->get_attr;

    case Event::kReadDir:
      return &metrics->read_dir;

    case Event::kSync:
      return &metrics->sync;

    case Event::kLookUp:
      return &metrics->look_up;

    case Event::kCreate:
      return &metrics->create;

    case Event::kLink:
      return &metrics->link;

    case Event::kUnlink:
      return &metrics->unlink;

    default:
      return nullptr;
  };
}
}  // namespace internal

CompositeLatencyEvent::CompositeLatencyEvent(Event event,
                                             fs_metrics::Histograms* histogram_collection,
                                             fs_metrics::VnodeMetrics* metrics)
    : inspect_event_(histogram_collection, event) {
  cobalt_histogram_ = internal::SelectHistogram(event, metrics);
}

CompositeLatencyEvent::~CompositeLatencyEvent() {
  if (cobalt_histogram_ != nullptr && inspect_event_.start().get() > 0) {
    zx::ticks delta = zx::ticks::now() - inspect_event_.start();
    cobalt_histogram_->Add(delta.get());
  }
}

void CompositeLatencyEvent::Cancel() { inspect_event_.Cancel(); }

void CompositeLatencyEvent::Reset() { inspect_event_.Reset(); }

}  // namespace fs_metrics
