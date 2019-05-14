// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include <zircon/assert.h>

namespace fs_metrics {

namespace {
// Number of buckets used for histograms. Must keep in syn with cobalt configuration if
// meant to be exported.
constexpr uint64_t kHistogramBuckets = 10;

// Attributes we are currently tracking.

// An attribute which indicates the number of blocks that were affected by a given operation.
//
// Inheriting from this attribute within an operation indicates that such operation is affected by
// the number of blocks.
struct BlockCount : NumericAttribute<BlockCount, int64_t> {
    static constexpr int64_t kBuckets[] = {
        // Bucket 0: [0, 5) for really small operations.
        5,
        // Bucket 1: [5, 32)
        32,
    };

    static constexpr int64_t EventOptions::*kAttributeValue = &EventOptions::block_count;
};

// An attribute which indicates whether the operation may be cached in memory or not.
//
// Inheriting from this attribute within an operation indicates that such operation may have
// variable modes of operations, where it either acts on in-memory structures or sends requests to
// the underlying storage.
struct Bufferable : public BinaryAttribute {
    static constexpr bool EventOptions::*kAttributeValue = &EventOptions::buffered;

    static std::string ToString(size_t index) { return index == 0 ? "unbuffered" : "buffered"; }
};

// An attribute which indicates whether the operation successful completion should be treated
// differently than when it completes with failure.
//
// Inheriting from this attribute within an operation indicates that such operation may fail
// at any point, and that the recorded data should be splitted.
struct Success : public BinaryAttribute {
    static constexpr bool EventOptions::*kAttributeValue = &EventOptions::success;

    static std::string ToString(size_t index) { return index == 0 ? "ok" : "fail"; }
};

// An attribute which indicates the number of childs a given node in the file system has.
//
// Inheriting from this attribute within an operation indicates that such operation is affected
// by the number of children the node has. An example is a lookup operation.
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

// Create a histogram with the default number of buckets and properties matching cobalt
// configuration so we can eventually replace cobalt-client.
void CreateNanosecHistogramId(const char* name, inspect::vmo::Object* root,
                              std::vector<inspect::vmo::ExponentialUintHistogram>* hist_list) {
    constexpr uint64_t kBase = 2;
    constexpr uint64_t kInitialStep = 10;
    constexpr uint64_t kFloor = 0;
    hist_list->push_back(
        root->CreateExponentialUintHistogram(name, kFloor, kInitialStep, kBase, kHistogramBuckets));
}

void CreateMicrosecHistogramId(const char* name, inspect::vmo::Object* root,
                               std::vector<inspect::vmo::ExponentialUintHistogram>* hist_list) {
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

// In order to add a new operations a couple of things needs to be added:
//
// 1. Add the operation to |OperationType| Enum.
// 2. Add a specialization to the |OperationInfo| template for the added operation.
// 3. Update switch tables in |Histograms::GetHistogramCount| and
//    |Histograms::GetHistgramCount(OperationType).
// 4. Add a call to |AddOpHistogram<OperationType>| in the constructor.
// 5. Add the new operation to the operation list in histograms-test.

// Base template specialization for Operations.
template <OperationType operation>
struct OperationInfo {};

// Each operation or metric we want to track needs to provide its own specialization with the
// relavant data and the attributes that it wishes to track.
template <>
struct OperationInfo<OperationType::kRead> : public BlockCount, Bufferable, Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "read";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart = 0;
};

template <>
struct OperationInfo<OperationType::kWrite> : public BlockCount, Bufferable, Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "write";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart = HistogramOffsets::End<OperationInfo<OperationType::kRead>>();
};

template <>
struct OperationInfo<OperationType::kAppend> : public BlockCount, Bufferable, Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "append";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart =
        HistogramOffsets::End<OperationInfo<OperationType::kWrite>>();
};

template <>
struct OperationInfo<OperationType::kTruncate> : public BlockCount, Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "truncate";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart =
        HistogramOffsets::End<OperationInfo<OperationType::kAppend>>();
};

template <>
struct OperationInfo<OperationType::kSetAttr> : public Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "setattr";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart =
        HistogramOffsets::End<OperationInfo<OperationType::kTruncate>>();
};

template <>
struct OperationInfo<OperationType::kGetAttr> : public Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "getattr";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart =
        HistogramOffsets::End<OperationInfo<OperationType::kSetAttr>>();
};

template <>
struct OperationInfo<OperationType::kReadDir> : public NodeDegree, Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "readdir";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart =
        HistogramOffsets::End<OperationInfo<OperationType::kGetAttr>>();
};

template <>
struct OperationInfo<OperationType::kSync> : public BlockCount, Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "sync";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart =
        HistogramOffsets::End<OperationInfo<OperationType::kReadDir>>();
};

template <>
struct OperationInfo<OperationType::kLookUp> : public NodeDegree, Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "lookup";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart = HistogramOffsets::End<OperationInfo<OperationType::kSync>>();
};

template <>
struct OperationInfo<OperationType::kCreate> : public NodeDegree, Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "create";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart =
        HistogramOffsets::End<OperationInfo<OperationType::kLookUp>>();
};

template <>
struct OperationInfo<OperationType::kClose> : public Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "close";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart =
        HistogramOffsets::End<OperationInfo<OperationType::kCreate>>();
};

template <>
struct OperationInfo<OperationType::kLink> : public NodeDegree, Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "link";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart =
        HistogramOffsets::End<OperationInfo<OperationType::kClose>>();
};

template <>
struct OperationInfo<OperationType::kUnlink> : public NodeDegree, Success {
    using AttributeData = EventOptions;
    static constexpr char kPrefix[] = "unlink";
    static constexpr auto CreateTracker = CreateMicrosecHistogramId;
    static constexpr uint64_t kStart = HistogramOffsets::End<OperationInfo<OperationType::kLink>>();
};

template <OperationType operation>
void AddOpHistograms(inspect::vmo::Object* root,
                     std::vector<inspect::vmo::ExponentialUintHistogram>* histograms) {
    HistogramOffsets::AddObjects<OperationInfo<operation>>(root, histograms);
}

} // namespace

Histograms::Histograms(inspect::vmo::Object* root) {
    nodes_.push_back(root->CreateChild(kHistComponent));
    auto& hist_node = nodes_[nodes_.size() - 1];

    // Histogram names are defined based on operation_name(_DimensionValue){0,5}, where
    // dimension value is determined at runtime based on the EventOptions.
    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kRead>::kStart);
    AddOpHistograms<OperationType::kRead>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kWrite>::kStart);
    AddOpHistograms<OperationType::kWrite>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kAppend>::kStart);
    AddOpHistograms<OperationType::kAppend>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kTruncate>::kStart);
    AddOpHistograms<OperationType::kTruncate>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kSetAttr>::kStart);
    AddOpHistograms<OperationType::kSetAttr>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kGetAttr>::kStart);
    AddOpHistograms<OperationType::kGetAttr>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kReadDir>::kStart);
    AddOpHistograms<OperationType::kReadDir>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kSync>::kStart);
    AddOpHistograms<OperationType::kSync>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kLookUp>::kStart);
    AddOpHistograms<OperationType::kLookUp>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kCreate>::kStart);
    AddOpHistograms<OperationType::kCreate>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kClose>::kStart);
    AddOpHistograms<OperationType::kClose>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kLink>::kStart);
    AddOpHistograms<OperationType::kLink>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() == OperationInfo<OperationType::kUnlink>::kStart);
    AddOpHistograms<OperationType::kUnlink>(&hist_node, &histograms_);

    ZX_DEBUG_ASSERT(histograms_.size() ==
                    HistogramOffsets::End<OperationInfo<OperationType::kUnlink>>());
}

LatencyEvent Histograms::NewLatencyEvent(OperationType operation) {
    return LatencyEvent(this, operation);
}

uint64_t Histograms::GetHistogramId(OperationType operation, const EventOptions& options) const {

    switch (operation) {
    case OperationType::kClose:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kClose>>(options);

    case OperationType::kRead:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kRead>>(options);

    case OperationType::kWrite:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kWrite>>(options);

    case OperationType::kAppend:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kAppend>>(options);

    case OperationType::kTruncate:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kTruncate>>(options);

    case OperationType::kSetAttr:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kSetAttr>>(options);

    case OperationType::kGetAttr:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kGetAttr>>(options);

    case OperationType::kReadDir:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kReadDir>>(options);

    case OperationType::kSync:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kSync>>(options);

    case OperationType::kLookUp:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kLookUp>>(options);

    case OperationType::kCreate:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kCreate>>(options);

    case OperationType::kLink:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kLink>>(options);

    case OperationType::kUnlink:
        return HistogramOffsets::AbsoluteOffset<OperationInfo<OperationType::kUnlink>>(options);

    default:
        return GetHistogramCount();
    };
}

uint64_t Histograms::GetHistogramCount(OperationType operation) {
    switch (operation) {
    case OperationType::kClose:
        return HistogramOffsets::Count<OperationInfo<OperationType::kClose>>();

    case OperationType::kRead:
        return HistogramOffsets::Count<OperationInfo<OperationType::kRead>>();

    case OperationType::kWrite:
        return HistogramOffsets::Count<OperationInfo<OperationType::kWrite>>();

    case OperationType::kAppend:
        return HistogramOffsets::Count<OperationInfo<OperationType::kAppend>>();

    case OperationType::kTruncate:
        return HistogramOffsets::Count<OperationInfo<OperationType::kTruncate>>();

    case OperationType::kSetAttr:
        return HistogramOffsets::Count<OperationInfo<OperationType::kSetAttr>>();

    case OperationType::kGetAttr:
        return HistogramOffsets::Count<OperationInfo<OperationType::kGetAttr>>();

    case OperationType::kReadDir:
        return HistogramOffsets::Count<OperationInfo<OperationType::kReadDir>>();

    case OperationType::kSync:
        return HistogramOffsets::Count<OperationInfo<OperationType::kSync>>();

    case OperationType::kLookUp:
        return HistogramOffsets::Count<OperationInfo<OperationType::kLookUp>>();

    case OperationType::kCreate:
        return HistogramOffsets::Count<OperationInfo<OperationType::kCreate>>();

    case OperationType::kLink:
        return HistogramOffsets::Count<OperationInfo<OperationType::kLink>>();

    case OperationType::kUnlink:
        return HistogramOffsets::Count<OperationInfo<OperationType::kUnlink>>();

    default:
        return 0;
    };
}

void Histograms::Record(uint64_t histogram_id, zx::duration duration) {
    ZX_ASSERT(histogram_id < GetHistogramCount());
    histograms_[histogram_id].Insert(duration.to_nsecs());
}

} // namespace fs_metrics
