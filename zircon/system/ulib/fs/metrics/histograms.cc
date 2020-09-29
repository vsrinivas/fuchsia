// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <algorithm>
#include <limits>
#include <string>
#include <type_traits>

#include <fbl/algorithm.h>
#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>
#include <fs/metrics/histograms.h>
#include <fs/metrics/internal/attributes.h>
#include <fs/metrics/internal/object_offsets.h>

namespace fs_metrics {

namespace {
// Number of buckets used for histograms. Must keep in syn with cobalt configuration if
// meant to be exported.
constexpr uint64_t kHistogramBuckets = 10;

// Attributes we are currently tracking.

// An attribute which indicates the number of blocks that were affected by a given event.
//
// Inheriting from this attribute within an event indicates that such event is affected by
// the number of blocks.
struct BlockCount : NumericAttribute<BlockCount, int64_t> {
  static constexpr int64_t kBuckets[] = {
      // Bucket 0: [0, 5) for really small events.
      5,
      // Bucket 1: [5, 32)
      32,
  };

  static constexpr int64_t EventOptions::*kAttributeValue = &EventOptions::block_count;
};

// An attribute which indicates whether the event may be cached in memory or not.
//
// Inheriting from this attribute within an event indicates that such event may have
// variable modes of events, where it either acts on in-memory structures or sends requests to
// the underlying storage.
struct Bufferable : public BinaryAttribute {
  static constexpr bool EventOptions::*kAttributeValue = &EventOptions::buffered;

  static std::string ToString(size_t index) { return index == 0 ? "unbuffered" : "buffered"; }
};

// An attribute which indicates whether the event successful completion should be treated
// differently than when it completes with failure.
//
// Inheriting from this attribute within an event indicates that such event may fail
// at any point, and that the recorded data should be splitted.
struct Success : public BinaryAttribute {
  static constexpr bool EventOptions::*kAttributeValue = &EventOptions::success;

  static std::string ToString(size_t index) { return index == 0 ? "ok" : "fail"; }
};

// An attribute which indicates the number of childs a given node in the file system has.
//
// Inheriting from this attribute within an event indicates that such event is affected
// by the number of children the node has. An example is a lookup event.
struct NodeDegree : NumericAttribute<NodeDegree, int64_t> {
  static constexpr int64_t kBuckets[] = {
      // Bucket 0: [0, 10)
      10,
      // Bucket 1: [10, 100)
      100,
      // Bucket 2: [100, 1000)
      1000,
  };

  static constexpr int64_t EventOptions::*kAttributeValue = &EventOptions::node_degree;
};

void CreateMicrosecHistogramId(const char* name, inspect::Node* root,
                               std::vector<inspect::ExponentialUintHistogram>* hist_list) {
  constexpr uint64_t kBase = 2;
  constexpr uint64_t kInitialStep = 10000;
  constexpr uint64_t kFloor = 0;
  hist_list->push_back(
      root->CreateExponentialUintHistogram(name, kFloor, kInitialStep, kBase, kHistogramBuckets));
}

// Provides a specialized type that keep track of created attributes. In order to add new
// attributes, the Attribute class needs to be listed here.
// Note: New attributes need to be added to |MakeOptionsSet| in HistogramsTest.
using HistogramOffsets = ObjectOffsets<NodeDegree, BlockCount, Bufferable, Success>;

// In order to add a new events a couple of things needs to be added:
//
// 1. Add the event to |Event| Enum.
// 2. Add a specialization to the |EventInfo| template for the added event.
// 3. Update switch tables in |Histograms::GetHistogramCount| and
//    |Histograms::GetHistgramCount(Event).
// 4. Add a call to |AddOpHistogram<Event>| in the constructor.
// 5. Add the new event to the event list in histograms-test.

// Base template specialization for Events.
template <Event event>
struct EventInfo {};

// Each event or metric we want to track needs to provide its own specialization with the
// relavant data and the attributes that it wishes to track.
template <>
struct EventInfo<Event::kRead> : public BlockCount, Bufferable, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "read";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = 0;
};

template <>
struct EventInfo<Event::kWrite> : public BlockCount, Bufferable, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "write";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kRead>>();
};

template <>
struct EventInfo<Event::kAppend> : public BlockCount, Bufferable, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "append";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kWrite>>();
};

template <>
struct EventInfo<Event::kTruncate> : public BlockCount, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "truncate";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kAppend>>();
};

template <>
struct EventInfo<Event::kSetAttr> : public Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "setattr";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kTruncate>>();
};

template <>
struct EventInfo<Event::kGetAttr> : public Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "getattr";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kSetAttr>>();
};

template <>
struct EventInfo<Event::kReadDir> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "readdir";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kGetAttr>>();
};

template <>
struct EventInfo<Event::kSync> : public BlockCount, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "sync";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kReadDir>>();
};

template <>
struct EventInfo<Event::kLookUp> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "lookup";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kSync>>();
};

template <>
struct EventInfo<Event::kCreate> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "create";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kLookUp>>();
};

template <>
struct EventInfo<Event::kClose> : public Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "close";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kCreate>>();
};

template <>
struct EventInfo<Event::kLink> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "link";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kClose>>();
};

template <>
struct EventInfo<Event::kUnlink> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "unlink";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kLink>>();
};

template <>
struct EventInfo<Event::kJournalWriteData> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "journal_write_data";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kUnlink>>();
};

template <>
struct EventInfo<Event::kJournalWriteMetadata> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "journal_write_metadata";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kJournalWriteData>>();
};

template <>
struct EventInfo<Event::kJournalTrimData> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "journal_trim_data";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart =
      HistogramOffsets::End<EventInfo<Event::kJournalWriteMetadata>>();
};

template <>
struct EventInfo<Event::kJournalSync> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "journal_sync";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kJournalTrimData>>();
};

template <>
struct EventInfo<Event::kJournalScheduleTask> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "journal_schedule_task";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kJournalSync>>();
};

template <>
struct EventInfo<Event::kJournalWriterWriteData> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "journal_writer_write_data";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart =
      HistogramOffsets::End<EventInfo<Event::kJournalScheduleTask>>();
};

template <>
struct EventInfo<Event::kJournalWriterWriteMetadata> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "journal_writer_write_metadata";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart =
      HistogramOffsets::End<EventInfo<Event::kJournalWriterWriteData>>();
};

template <>
struct EventInfo<Event::kJournalWriterTrimData> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "journal_writer_trim_data";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart =
      HistogramOffsets::End<EventInfo<Event::kJournalWriterWriteMetadata>>();
};

template <>
struct EventInfo<Event::kJournalWriterSync> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "journal_writer_sync";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart =
      HistogramOffsets::End<EventInfo<Event::kJournalWriterTrimData>>();
};

template <>
struct EventInfo<Event::kJournalWriterWriteInfoBlock> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  static constexpr char kPrefix[] = "journal_writer_write_info_block";
  static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart = HistogramOffsets::End<EventInfo<Event::kJournalWriterSync>>();
};

template <>
struct EventInfo<Event::kInvalidEvent> : public NodeDegree, Success {
  using AttributeData = EventOptions;
  [[maybe_unused]] static constexpr char kPrefix[] = "invalid event";
  [[maybe_unused]] static constexpr auto CreateTracker = CreateMicrosecHistogramId;
  static constexpr uint64_t kStart =
      HistogramOffsets::End<EventInfo<Event::kJournalWriterWriteInfoBlock>>();
};

template <Event event>
void AddOpHistograms(inspect::Node* root,
                     std::vector<inspect::ExponentialUintHistogram>* histograms) {
  HistogramOffsets::AddObjects<EventInfo<event>>(root, histograms);
}

}  // namespace

Histograms::Histograms(inspect::Node* root) {
  nodes_.push_back(root->CreateChild(kHistComponent));
  auto& hist_node = nodes_[nodes_.size() - 1];

  // Histogram names are defined based on event_name(_DimensionValue){0,5}, where
  // dimension value is determined at runtime based on the EventOptions.
  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kRead>::kStart);
  AddOpHistograms<Event::kRead>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kWrite>::kStart);
  AddOpHistograms<Event::kWrite>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kAppend>::kStart);
  AddOpHistograms<Event::kAppend>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kTruncate>::kStart);
  AddOpHistograms<Event::kTruncate>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kSetAttr>::kStart);
  AddOpHistograms<Event::kSetAttr>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kGetAttr>::kStart);
  AddOpHistograms<Event::kGetAttr>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kReadDir>::kStart);
  AddOpHistograms<Event::kReadDir>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kSync>::kStart);
  AddOpHistograms<Event::kSync>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kLookUp>::kStart);
  AddOpHistograms<Event::kLookUp>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kCreate>::kStart);
  AddOpHistograms<Event::kCreate>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kClose>::kStart);
  AddOpHistograms<Event::kClose>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kLink>::kStart);
  AddOpHistograms<Event::kLink>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kUnlink>::kStart);
  AddOpHistograms<Event::kUnlink>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kJournalWriteData>::kStart);
  AddOpHistograms<Event::kJournalWriteData>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kJournalWriteMetadata>::kStart);
  AddOpHistograms<Event::kJournalWriteMetadata>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kJournalTrimData>::kStart);
  AddOpHistograms<Event::kJournalTrimData>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kJournalSync>::kStart);
  AddOpHistograms<Event::kJournalSync>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kJournalScheduleTask>::kStart);
  AddOpHistograms<Event::kJournalScheduleTask>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kJournalWriterWriteData>::kStart);
  AddOpHistograms<Event::kJournalWriterWriteData>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kJournalWriterWriteMetadata>::kStart);
  AddOpHistograms<Event::kJournalWriterWriteMetadata>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kJournalWriterTrimData>::kStart);
  AddOpHistograms<Event::kJournalWriterTrimData>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kJournalWriterSync>::kStart);
  AddOpHistograms<Event::kJournalWriterSync>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() == EventInfo<Event::kJournalWriterWriteInfoBlock>::kStart);
  AddOpHistograms<Event::kJournalWriterWriteInfoBlock>(&hist_node, &histograms_);

  ZX_DEBUG_ASSERT(histograms_.size() ==
                  HistogramOffsets::End<EventInfo<Event::kJournalWriterWriteInfoBlock>>());
}

LatencyEvent Histograms::NewLatencyEvent(Event event) { return LatencyEvent(this, event); }

uint64_t Histograms::GetHistogramId(Event event, const EventOptions& options) const {
  switch (event) {
    case Event::kClose:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kClose>>(options);

    case Event::kRead:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kRead>>(options);

    case Event::kWrite:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kWrite>>(options);

    case Event::kAppend:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kAppend>>(options);

    case Event::kTruncate:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kTruncate>>(options);

    case Event::kSetAttr:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kSetAttr>>(options);

    case Event::kGetAttr:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kGetAttr>>(options);

    case Event::kReadDir:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kReadDir>>(options);

    case Event::kSync:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kSync>>(options);

    case Event::kLookUp:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kLookUp>>(options);

    case Event::kCreate:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kCreate>>(options);

    case Event::kLink:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kLink>>(options);

    case Event::kUnlink:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kUnlink>>(options);

    case Event::kJournalWriteData:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kJournalWriteData>>(options);

    case Event::kJournalWriteMetadata:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kJournalWriteMetadata>>(options);

    case Event::kJournalTrimData:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kJournalTrimData>>(options);

    case Event::kJournalSync:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kJournalSync>>(options);

    case Event::kJournalScheduleTask:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kJournalScheduleTask>>(options);

    case Event::kJournalWriterWriteData:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kJournalWriterWriteData>>(options);

    case Event::kJournalWriterWriteMetadata:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kJournalWriterWriteMetadata>>(
          options);

    case Event::kJournalWriterTrimData:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kJournalWriterTrimData>>(options);

    case Event::kJournalWriterSync:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kJournalWriterSync>>(options);

    case Event::kJournalWriterWriteInfoBlock:
      return HistogramOffsets::AbsoluteOffset<EventInfo<Event::kJournalWriterWriteInfoBlock>>(
          options);

    default:
      return GetHistogramCount();
  };
}

uint64_t Histograms::GetHistogramCount(Event event) {
  switch (event) {
    case Event::kClose:
      return HistogramOffsets::Count<EventInfo<Event::kClose>>();

    case Event::kRead:
      return HistogramOffsets::Count<EventInfo<Event::kRead>>();

    case Event::kWrite:
      return HistogramOffsets::Count<EventInfo<Event::kWrite>>();

    case Event::kAppend:
      return HistogramOffsets::Count<EventInfo<Event::kAppend>>();

    case Event::kTruncate:
      return HistogramOffsets::Count<EventInfo<Event::kTruncate>>();

    case Event::kSetAttr:
      return HistogramOffsets::Count<EventInfo<Event::kSetAttr>>();

    case Event::kGetAttr:
      return HistogramOffsets::Count<EventInfo<Event::kGetAttr>>();

    case Event::kReadDir:
      return HistogramOffsets::Count<EventInfo<Event::kReadDir>>();

    case Event::kSync:
      return HistogramOffsets::Count<EventInfo<Event::kSync>>();

    case Event::kLookUp:
      return HistogramOffsets::Count<EventInfo<Event::kLookUp>>();

    case Event::kCreate:
      return HistogramOffsets::Count<EventInfo<Event::kCreate>>();

    case Event::kLink:
      return HistogramOffsets::Count<EventInfo<Event::kLink>>();

    case Event::kUnlink:
      return HistogramOffsets::Count<EventInfo<Event::kUnlink>>();

    case Event::kJournalWriteData:
      return HistogramOffsets::Count<EventInfo<Event::kJournalWriteData>>();

    case Event::kJournalWriteMetadata:
      return HistogramOffsets::Count<EventInfo<Event::kJournalWriteMetadata>>();

    case Event::kJournalTrimData:
      return HistogramOffsets::Count<EventInfo<Event::kJournalTrimData>>();

    case Event::kJournalSync:
      return HistogramOffsets::Count<EventInfo<Event::kJournalSync>>();

    case Event::kJournalScheduleTask:
      return HistogramOffsets::Count<EventInfo<Event::kJournalScheduleTask>>();

    case Event::kJournalWriterWriteData:
      return HistogramOffsets::Count<EventInfo<Event::kJournalWriterWriteData>>();

    case Event::kJournalWriterWriteMetadata:
      return HistogramOffsets::Count<EventInfo<Event::kJournalWriterWriteMetadata>>();

    case Event::kJournalWriterTrimData:
      return HistogramOffsets::Count<EventInfo<Event::kJournalWriterTrimData>>();

    case Event::kJournalWriterSync:
      return HistogramOffsets::Count<EventInfo<Event::kJournalWriterSync>>();

    case Event::kJournalWriterWriteInfoBlock:
      return HistogramOffsets::Count<EventInfo<Event::kJournalWriterWriteInfoBlock>>();

    default:
      return 0;
  };
}

void Histograms::Record(uint64_t histogram_id, zx::duration duration) {
  ZX_ASSERT(histogram_id < GetHistogramCount());
  histograms_[histogram_id].Insert(duration.to_nsecs());
}

uint64_t Histograms::Size() {
  // An integer for each bucket + metadata
  constexpr uint32_t kApproximateNameLength = 30;
  return fbl::round_up(HistogramOffsets::End<EventInfo<Event::kInvalidEvent>>() *
                           ((kHistogramBuckets * sizeof(uint64_t) + kApproximateNameLength) +
                            strlen(Histograms::kHistComponent)),
                       static_cast<uint64_t>(PAGE_SIZE));
}

}  // namespace fs_metrics
