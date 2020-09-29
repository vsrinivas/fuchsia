// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/time.h>

#include <fs/metrics/composite_latency_event.h>

namespace fs_metrics {

namespace internal {
cobalt_client::Histogram<fs_metrics::FsCommonMetrics::kHistogramBuckets>* SelectHistogram(
    const Event event, fs_metrics::FsCommonMetrics* metrics) {
  switch (event) {
    case Event::kClose:
      return &metrics->vnode.close;

    case Event::kRead:
      return &metrics->vnode.read;

    case Event::kWrite:
      return &metrics->vnode.write;

    case Event::kAppend:
      return &metrics->vnode.append;

    case Event::kTruncate:
      return &metrics->vnode.truncate;

    case Event::kSetAttr:
      return &metrics->vnode.set_attr;

    case Event::kGetAttr:
      return &metrics->vnode.get_attr;

    case Event::kReadDir:
      return &metrics->vnode.read_dir;

    case Event::kSync:
      return &metrics->vnode.sync;

    case Event::kLookUp:
      return &metrics->vnode.look_up;

    case Event::kCreate:
      return &metrics->vnode.create;

    case Event::kLink:
      return &metrics->vnode.link;

    case Event::kUnlink:
      return &metrics->vnode.unlink;

    case Event::kJournalWriteData:
      return &metrics->journal.write_data;

    case Event::kJournalWriteMetadata:
      return &metrics->journal.write_metadata;

    case Event::kJournalTrimData:
      return &metrics->journal.trim_data;

    case Event::kJournalSync:
      return &metrics->journal.sync;

    case Event::kJournalScheduleTask:
      return &metrics->journal.schedule_task;

    case Event::kJournalWriterWriteData:
      return &metrics->journal.writer_write_data;

    case Event::kJournalWriterWriteMetadata:
      return &metrics->journal.writer_write_metadata;

    case Event::kJournalWriterTrimData:
      return &metrics->journal.writer_trim_data;

    case Event::kJournalWriterSync:
      return &metrics->journal.writer_sync;

    case Event::kJournalWriterWriteInfoBlock:
      return &metrics->journal.writer_write_info_block;

    default:
      return nullptr;
  };
}
}  // namespace internal

CompositeLatencyEvent::CompositeLatencyEvent(Event event,
                                             fs_metrics::Histograms* histogram_collection,
                                             fs_metrics::FsCommonMetrics* metrics)
    : inspect_event_(histogram_collection, event) {
  cobalt_histogram_ = internal::SelectHistogram(event, metrics);
}

CompositeLatencyEvent::~CompositeLatencyEvent() {
  if (cobalt_histogram_ != nullptr && inspect_event_.start().get() > 0) {
    zx::ticks delta = zx::ticks::now() - inspect_event_.start();
    cobalt_histogram_->Add(fzl::TicksToNs(delta).get());
  }
}

void CompositeLatencyEvent::Cancel() { inspect_event_.Cancel(); }

void CompositeLatencyEvent::Reset() { inspect_event_.Reset(); }

}  // namespace fs_metrics
