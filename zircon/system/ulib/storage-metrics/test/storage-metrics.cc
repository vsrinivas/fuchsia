// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include <storage-metrics/fs-metrics.h>
#include <storage-metrics/storage-metrics.h>
#include <zxtest/zxtest.h>

namespace storage_metrics {
namespace {

using storage_metrics::CallStat;

// Campare CallStat fields with the corresponding fields in fuchsia_storage_metrics_CallStat
// structure
void ExpectCallStatMatchFidlStat(CallStat& cs, fuchsia_storage_metrics_CallStat& cs_fidl) {
    EXPECT_EQ(cs.minimum_latency(true), cs_fidl.success.minimum_latency);
    EXPECT_EQ(cs.maximum_latency(true), cs_fidl.success.maximum_latency);
    EXPECT_EQ(cs.total_time_spent(true), cs_fidl.success.total_time_spent);
    EXPECT_EQ(cs.total_calls(true), cs_fidl.success.total_calls);
    EXPECT_EQ(cs.bytes_transferred(true), cs_fidl.success.bytes_transferred);

    EXPECT_EQ(cs.minimum_latency(false), cs_fidl.failure.minimum_latency);
    EXPECT_EQ(cs.maximum_latency(false), cs_fidl.failure.maximum_latency);
    EXPECT_EQ(cs.total_time_spent(false), cs_fidl.failure.total_time_spent);
    EXPECT_EQ(cs.total_calls(false), cs_fidl.failure.total_calls);
    EXPECT_EQ(cs.bytes_transferred(false), cs_fidl.failure.bytes_transferred);

    EXPECT_EQ(cs.minimum_latency(),
              std::min(cs_fidl.success.minimum_latency, cs_fidl.failure.minimum_latency));
    EXPECT_EQ(cs.maximum_latency(),
              std::max(cs_fidl.success.maximum_latency, cs_fidl.failure.maximum_latency));
    EXPECT_EQ(cs.total_time_spent(),
              (cs_fidl.success.total_time_spent + cs_fidl.failure.total_time_spent));
    EXPECT_EQ(cs.total_calls(), (cs_fidl.success.total_calls + cs_fidl.failure.total_calls));
    EXPECT_EQ(cs.bytes_transferred(),
              (cs_fidl.success.bytes_transferred + cs_fidl.failure.bytes_transferred));

    fuchsia_storage_metrics_CallStat tmp;
    cs.CopyToFidl(&tmp);
}

// Deep campares two fuchsia_storage_metrics_CallStatRaw structures. Can be
void ExpectFidlCallStatRawMatch(const fuchsia_storage_metrics_CallStatRaw& lhs,
                                const fuchsia_storage_metrics_CallStatRaw& rhs) {
    EXPECT_EQ(lhs.total_calls, rhs.total_calls);
    EXPECT_EQ(lhs.total_time_spent, rhs.total_time_spent);
    EXPECT_EQ(lhs.minimum_latency, rhs.minimum_latency);
    EXPECT_EQ(lhs.maximum_latency, rhs.maximum_latency);
    EXPECT_EQ(lhs.bytes_transferred, rhs.bytes_transferred);
}

// Compares two fuchsia_storage_metrics_CallStat structures
void ExpectFsMetricsMatchCallStat(const fuchsia_storage_metrics_CallStat& lhs,
                                  const fuchsia_storage_metrics_CallStat& rhs) {
    ExpectFidlCallStatRawMatch(lhs.success, rhs.success);
    ExpectFidlCallStatRawMatch(lhs.failure, rhs.failure);
}

// Compares all fuchsia_storage_metrics_CallStat fields within |fidl_fs_metrics|,
// with |fidl_call_stat|
void CompareFidlFsStatAll(const fuchsia_storage_metrics_FsMetrics& fidl_fs_metrics,
                          const fuchsia_storage_metrics_CallStat& fidl_call_stat) {

    ExpectFsMetricsMatchCallStat(fidl_fs_metrics.create, fidl_call_stat);
    ExpectFsMetricsMatchCallStat(fidl_fs_metrics.read, fidl_call_stat);
    ExpectFsMetricsMatchCallStat(fidl_fs_metrics.write, fidl_call_stat);
    ExpectFsMetricsMatchCallStat(fidl_fs_metrics.truncate, fidl_call_stat);
    ExpectFsMetricsMatchCallStat(fidl_fs_metrics.unlink, fidl_call_stat);
    ExpectFsMetricsMatchCallStat(fidl_fs_metrics.rename, fidl_call_stat);
    ExpectFsMetricsMatchCallStat(fidl_fs_metrics.lookup, fidl_call_stat);
    ExpectFsMetricsMatchCallStat(fidl_fs_metrics.open, fidl_call_stat);
}

// Updates all private CallStat fields of |metrics|
void UpdateAllFsMetricsRaw(storage_metrics::FsMetrics& metrics, bool success, zx_ticks_t delta,
                           uint64_t bytes_transferred) {
    metrics.UpdateCreateStat(success, delta, bytes_transferred);
    metrics.UpdateReadStat(success, delta, bytes_transferred);
    metrics.UpdateWriteStat(success, delta, bytes_transferred);
    metrics.UpdateTruncateStat(success, delta, bytes_transferred);
    metrics.UpdateUnlinkStat(success, delta, bytes_transferred);
    metrics.UpdateRenameStat(success, delta, bytes_transferred);
    metrics.UpdateLookupStat(success, delta, bytes_transferred);
    metrics.UpdateOpenStat(success, delta, bytes_transferred);
}

// Updates both success and failure stats with (|minimum_latency|, |bytes_transferred1|)
// (|maximum_latency|, |bytes_transferred2|) respectively.
void FsMetricsUpdate(storage_metrics::FsMetrics& metrics, zx_ticks_t minimum_latency,
                     zx_ticks_t maximum_latency, uint64_t bytes_transferred1,
                     uint64_t bytes_transferred2) {
    // Update successful minimum and maximum latencies
    UpdateAllFsMetricsRaw(metrics, true, minimum_latency, bytes_transferred1);
    UpdateAllFsMetricsRaw(metrics, true, maximum_latency, bytes_transferred2);
    UpdateAllFsMetricsRaw(metrics, false, minimum_latency, bytes_transferred1);
    UpdateAllFsMetricsRaw(metrics, false, maximum_latency, bytes_transferred2);
}

// Expects if |fidl_fs_mterics| is not properly initialized.
void ExpectInitialState(const fuchsia_storage_metrics_FsMetrics& fidl_fs_metrics) {
    fuchsia_storage_metrics_CallStat fidl_call_stat = {};
    fidl_call_stat.success.minimum_latency = storage_metrics::kUninitializedMinimumLatency;
    fidl_call_stat.failure.minimum_latency = storage_metrics::kUninitializedMinimumLatency;
    CompareFidlFsStatAll(fidl_fs_metrics, fidl_call_stat);
}

TEST(CallStatTest, UpdateSuccess) {
    storage_metrics::CallStat cs = {};
    fuchsia_storage_metrics_CallStat fidl_stat;

    // Copy initial state
    cs.CopyToFidl(&fidl_stat);

    cs.UpdateCallStat(true, 10, 100);
    fidl_stat.success.total_calls++;
    fidl_stat.success.total_time_spent += 10;
    fidl_stat.success.minimum_latency = 10;
    fidl_stat.success.maximum_latency = 10;
    fidl_stat.success.bytes_transferred += 100;
    ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, UpdateFailure) {
    storage_metrics::CallStat cs = {};
    fuchsia_storage_metrics_CallStat fidl_stat;

    // Copy initial state
    cs.CopyToFidl(&fidl_stat);

    // No change in success stats but everything else changes
    cs.UpdateCallStat(false, 10, 100);
    fidl_stat.failure.total_calls++;
    fidl_stat.failure.total_time_spent += 10;
    fidl_stat.failure.minimum_latency = 10;
    fidl_stat.failure.maximum_latency = 10;
    fidl_stat.failure.bytes_transferred += 100;
    ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, UpdateBytesTransferred) {
    storage_metrics::CallStat cs = {};
    fuchsia_storage_metrics_CallStat fidl_stat;

    // Copy initial state
    cs.CopyToFidl(&fidl_stat);

    // No change in min/max latencies or failure but everything else changes
    cs.UpdateCallStat(true, 10, 100);
    fidl_stat.success.total_calls++;
    fidl_stat.success.total_time_spent += 10;
    fidl_stat.success.minimum_latency = 10;
    fidl_stat.success.maximum_latency = 10;
    fidl_stat.success.bytes_transferred += 100;
    ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, UpdateMinimumLatency) {
    storage_metrics::CallStat cs = {};
    fuchsia_storage_metrics_CallStat fidl_stat;

    // Copy initial state
    cs.CopyToFidl(&fidl_stat);

    // Expect min latency to change and bytes transferred unchanged
    cs.UpdateCallStat(true, 9, 0);
    cs.UpdateCallStat(true, 7, 0);
    fidl_stat.success.total_calls += 2;
    fidl_stat.success.total_time_spent += (9 + 7);
    fidl_stat.success.minimum_latency = 7;
    fidl_stat.success.maximum_latency = 9;
    fidl_stat.success.bytes_transferred += 0;
    ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, UpdateFailedMaximumLatency) {
    storage_metrics::CallStat cs = {};
    fuchsia_storage_metrics_CallStat fidl_stat;

    // Copy initial state
    cs.CopyToFidl(&fidl_stat);

    // Expect max latency and failed count to change
    cs.UpdateCallStat(false, 20, 100);
    cs.UpdateCallStat(false, 30, 100);
    fidl_stat.failure.total_calls += 2;
    fidl_stat.failure.total_time_spent += (20 + 30);
    fidl_stat.failure.minimum_latency = 20;
    fidl_stat.failure.maximum_latency = 30;
    fidl_stat.failure.bytes_transferred += (100 + 100);
    ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, UpdateTimeSpent) {
    storage_metrics::CallStat cs = {};
    fuchsia_storage_metrics_CallStat fidl_stat;

    // Copy initial state
    cs.CopyToFidl(&fidl_stat);

    // Expect only time spent and total calls to change
    cs.UpdateCallStat(true, 20, 0);
    cs.UpdateCallStat(true, 20, 0);
    fidl_stat.success.total_calls += 2;
    fidl_stat.success.minimum_latency = 20;
    fidl_stat.success.maximum_latency = 20;
    fidl_stat.success.total_time_spent += (20 + 20);
    ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, Reset) {
    storage_metrics::CallStat cs = {};
    fuchsia_storage_metrics_CallStat fidl_stat;

    cs.UpdateCallStat(true, 20, 100);
    cs.UpdateCallStat(false, 20, 100);

    // Everything should be cleared
    cs.Reset();
    fidl_stat = {};
    fidl_stat.success.minimum_latency = storage_metrics::kUninitializedMinimumLatency;
    fidl_stat.failure.minimum_latency = storage_metrics::kUninitializedMinimumLatency;
    ExpectCallStatMatchFidlStat(cs, fidl_stat);
}

TEST(CallStatTest, TestCopyToFidl) {

    fuchsia_storage_metrics_CallStat f = {};
    storage_metrics::CallStat cs;

    // Set max latency
    cs.UpdateCallStat(true, 20, 100);

    // Set min latency and fail count
    cs.UpdateCallStat(true, 10, 20);
    cs.CopyToFidl(&f);

    ExpectCallStatMatchFidlStat(cs, f);
}

TEST(CallStatTest, TestCopyFromFidl) {
    fuchsia_storage_metrics_CallStat f;
    storage_metrics::CallStat cs;

    f.success.total_calls = 3;
    f.success.minimum_latency = 4;
    f.success.maximum_latency = 15;
    f.success.total_time_spent = 19;
    f.success.bytes_transferred = 92;
    f.failure.total_calls = 3;
    f.failure.minimum_latency = 4;
    f.failure.maximum_latency = 15;
    f.failure.total_time_spent = 19;
    f.failure.bytes_transferred = 92;
    cs.CopyFromFidl(&f);

    ExpectCallStatMatchFidlStat(cs, f);
}

// Tests enable/disable functionality
TEST(MetricsTest, SetEnable) {
    storage_metrics::Metrics metrics;

    EXPECT_TRUE(metrics.Enabled());
    metrics.SetEnable(false);
    EXPECT_FALSE(metrics.Enabled());
    metrics.SetEnable(true);
    EXPECT_TRUE(metrics.Enabled());
}

// Test initial state of FsMetrics
TEST(FsMetricsTest, DefaultValues) {
    storage_metrics::FsMetrics metrics;
    fuchsia_storage_metrics_FsMetrics fidl_fs_metrics;
    fuchsia_storage_metrics_CallStat fidl_call_stat = {};
    fidl_call_stat.success.minimum_latency = storage_metrics::kUninitializedMinimumLatency;
    fidl_call_stat.failure.minimum_latency = storage_metrics::kUninitializedMinimumLatency;

    EXPECT_TRUE(metrics.Enabled());

    metrics.CopyToFidl(&fidl_fs_metrics);
    CompareFidlFsStatAll(fidl_fs_metrics, fidl_call_stat);
}

// Tests no-updates are made when metrics is disabled
TEST(FsMetricsTest, DisabledMetricsIgnoreUpdates) {
    storage_metrics::FsMetrics metrics;
    fuchsia_storage_metrics_FsMetrics fidl_fs_metrics;

    EXPECT_TRUE(metrics.Enabled());

    metrics.SetEnable(false);
    EXPECT_FALSE(metrics.Enabled());

    // When not enabled, this should not update anything
    FsMetricsUpdate(metrics, 10, 100, 100, 800);

    metrics.CopyToFidl(&fidl_fs_metrics);
    ExpectInitialState(fidl_fs_metrics);
}

// Tests updates to fs metrics
TEST(FsMetricsTest, EnabledMetricsCollectOnUpdate) {
    storage_metrics::FsMetrics metrics;
    fuchsia_storage_metrics_FsMetrics fidl_fs_metrics;
    fuchsia_storage_metrics_CallStatRaw fidl_call_stat_raw = {};
    fuchsia_storage_metrics_CallStat fidl_call_stat;
    EXPECT_TRUE(metrics.Enabled());

    zx_ticks_t minimum_latency = 10;
    zx_ticks_t maximum_latency = 100;
    uint64_t bytes_transferred1 = 330;
    uint64_t bytes_transferred2 = 440;

    FsMetricsUpdate(metrics, minimum_latency, maximum_latency, bytes_transferred1,
                    bytes_transferred2);

    metrics.CopyToFidl(&fidl_fs_metrics);
    fidl_call_stat_raw.minimum_latency = minimum_latency;
    fidl_call_stat_raw.maximum_latency = maximum_latency;
    fidl_call_stat_raw.total_time_spent = minimum_latency + maximum_latency;
    fidl_call_stat_raw.total_calls = 2;
    fidl_call_stat_raw.bytes_transferred = bytes_transferred1 + bytes_transferred2;
    fidl_call_stat.success = fidl_call_stat_raw;
    fidl_call_stat.failure = fidl_call_stat_raw;

    CompareFidlFsStatAll(fidl_fs_metrics, fidl_call_stat);

    // Disable enable should not change the metrics
    metrics.SetEnable(false);
    metrics.CopyToFidl(&fidl_fs_metrics);
    CompareFidlFsStatAll(fidl_fs_metrics, fidl_call_stat);
    metrics.SetEnable(true);
    metrics.CopyToFidl(&fidl_fs_metrics);
    CompareFidlFsStatAll(fidl_fs_metrics, fidl_call_stat);
}

} // namespace
} // namespace storage_metrics
