// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>

#include <fs/metrics/histograms.h>
#include <lib/inspect-vmo/inspect.h>
#include <lib/inspect-vmo/types.h>
#include <lib/zx/time.h>
#include <zxtest/zxtest.h>

namespace fs_metrics {

class HistogramsTest : public zxtest::Test {
public:
    void SetUp() final {
        root_ = inspector_.CreateObject("root-test");
        ASSERT_TRUE(static_cast<bool>(root_));
    }

protected:
    inspect::vmo::Inspector inspector_;
    inspect::vmo::Object root_;
};

constexpr zx::duration kDuration = zx::nsec(5);

const std::vector<EventOptions>& GetOptionsSets() {
    constexpr int64_t kBlockCounts[] = {std::numeric_limits<int64_t>::min(), 1, 5, 31, 32,
                                        std::numeric_limits<int64_t>::max()};
    constexpr int64_t kNodeDepths[] = {
        std::numeric_limits<int64_t>::min(), 1, 2, 4, 8, 16, 32, 64, 128,
        std::numeric_limits<int64_t>::max()};
    constexpr int64_t kNodeDegrees[] = {
        std::numeric_limits<int64_t>::min(), 1, 2, 4, 8, 16, 32, 64, 128, 1024, 1024 * 1024,
        std::numeric_limits<int64_t>::max()};
    constexpr bool kBuffered[] = {true, false};
    constexpr bool kSuccess[] = {true, false};

    static std::vector<EventOptions> option_set;
    option_set.reserve(fbl::count_of(kBlockCounts) * fbl::count_of(kNodeDepths) *
                       fbl::count_of(kNodeDegrees) * fbl::count_of(kBuffered) *
                       fbl::count_of(kSuccess));

    for (auto block_count : kBlockCounts) {
        for (auto node_depth : kNodeDepths) {
            for (auto node_degree : kNodeDegrees) {
                for (auto buffered : kBuffered) {
                    for (auto success : kSuccess) {
                        EventOptions options;
                        options.block_count = block_count;
                        options.node_degree = node_degree;
                        options.node_depth = node_depth;
                        options.success = success;
                        options.buffered = buffered;
                        option_set.push_back(options);
                    }
                }
            }
        }
    }

    return option_set;
}

constexpr OperationType kOperations[] = {
    OperationType::kClose,   OperationType::kRead,     OperationType::kWrite,
    OperationType::kAppend,  OperationType::kTruncate, OperationType::kSetAttr,
    OperationType::kGetAttr, OperationType::kReadDir,  OperationType::kSync,
    OperationType::kLookUp,  OperationType::kCreate,   OperationType::kLink,
    OperationType::kUnlink,
};
static_assert(fbl::count_of(kOperations) == kOperationCount, "Untested operation.");

TEST_F(HistogramsTest, AllOptionsAreValid) {

    Histograms histograms = Histograms(&root_);
    std::set<uint64_t> histogram_ids;

    for (auto operation : kOperations) {
        uint64_t prev_size = histogram_ids.size();
        for (auto option_set : GetOptionsSets()) {
            uint64_t histogram_id = histograms.GetHistogramId(operation, option_set);
            ASSERT_GE(histogram_id, 0);
            ASSERT_LT(histogram_id, histograms.GetHistogramCount());
            histogram_ids.insert(histogram_id);
            histograms.Record(histogram_id, kDuration);
        }
        ASSERT_EQ(histograms.GetHistogramCount(operation), histogram_ids.size() - prev_size,
                  " Operation Histogram Count is wrong. %lu", static_cast<uint64_t>(operation));
    }

    ASSERT_EQ(histogram_ids.size(), histograms.GetHistogramCount(),
              "Failed to cover all histograms with all options set.");
}

TEST_F(HistogramsTest, DefaultLatencyEventSmokeTest) {
    Histograms histograms = Histograms(&root_);
    std::set<uint64_t> histogram_ids;

    // This is will log an event with default options for every operation, which would crash with
    // unitialized memory.
    for (auto operation : kOperations) {
        histograms.NewLatencyEvent(operation);
    }
}

TEST_F(HistogramsTest, InvalidOptionsReturnsHistogramCount) {
    Histograms histograms = Histograms(&root_);
    ASSERT_EQ(
        histograms.GetHistogramId(static_cast<OperationType>(kOperationCount), EventOptions()),
        histograms.GetHistogramCount());
}

} // namespace fs_metrics
