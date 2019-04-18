// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include <storage-metrics/fs-metrics.h>
#include <storage-metrics/storage-metrics.h>

namespace storage_metrics {
CallStat::CallStatRaw::CallStatRaw() {
    Reset();
}

void CallStat::CallStatRaw::Reset() {
    minimum_latency = 0;
    maximum_latency = 0;
    total_time_spent = 0;
    total_calls = 0;
    bytes_transferred = 0;
    minimum_latency = kUninitializedMinimumLatency;
}

void CallStat::CallStatRaw::CopyFromRawFidl(const CallStatRawFidl* istat) {
    minimum_latency = istat->minimum_latency;
    maximum_latency = istat->maximum_latency;
    total_time_spent = istat->total_time_spent;
    total_calls = istat->total_calls;
    bytes_transferred = istat->bytes_transferred;
    minimum_latency = istat->minimum_latency;
}

void CallStat::CallStatRaw::CopyToRawFidl(CallStatRawFidl* out) const {
    out->minimum_latency = minimum_latency.load();
    out->maximum_latency = maximum_latency.load();
    out->total_time_spent = total_time_spent.load();
    out->total_calls = total_calls.load();
    out->bytes_transferred = bytes_transferred.load();
    out->minimum_latency = minimum_latency.load();
}

void CallStat::CallStatRaw::UpdateRawCallStat(zx_ticks_t delta_time, uint64_t bytes) {
    total_calls++;
    total_time_spent += delta_time;
    bytes_transferred += bytes;

    uint64_t max = maximum_latency.load();
    while (delta_time > max) {
        maximum_latency.compare_exchange_strong(max, delta_time);
        max = maximum_latency.load();
    }

    uint64_t min = minimum_latency.load();
    while (delta_time < min) {
        minimum_latency.compare_exchange_strong(min, delta_time);
        min = minimum_latency.load();
    }
}

CallStat::CallStat() {
    Reset();
}

void CallStat::Reset() {
    success_stat_.Reset();
    failure_stat_.Reset();
}

void CallStat::CopyFromFidl(const CallStatFidl* stat) {
    success_stat_.CopyFromRawFidl(&stat->success);
    failure_stat_.CopyFromRawFidl(&stat->failure);
}

void CallStat::CopyToFidl(CallStatFidl* out) const {
    success_stat_.CopyToRawFidl(&out->success);
    failure_stat_.CopyToRawFidl(&out->failure);
}

uint64_t CallStat::minimum_latency(std::optional<bool> success) const {
    if (success) {
        if (*success == true) {
            return success_stat_.minimum_latency;
        } else {
            return failure_stat_.minimum_latency;
        }
    }
    return std::min(success_stat_.minimum_latency, failure_stat_.minimum_latency);
}
uint64_t CallStat::maximum_latency(std::optional<bool> success) const {
    if (success) {
        if (*success == true) {
            return success_stat_.maximum_latency;
        } else {
            return failure_stat_.maximum_latency;
        }
    }
    return std::max(success_stat_.maximum_latency, failure_stat_.maximum_latency);
}
uint64_t CallStat::total_time_spent(std::optional<bool> success) const {
    if (success) {
        if (*success == true) {
            return success_stat_.total_time_spent;
        } else {
            return failure_stat_.total_time_spent;
        }
    }
    return success_stat_.total_time_spent + failure_stat_.total_time_spent;
}
uint64_t CallStat::total_calls(std::optional<bool> success) const {
    if (success) {
        if (*success == true) {
            return success_stat_.total_calls;
        } else {
            return failure_stat_.total_calls;
        }
    }
    return success_stat_.total_calls + failure_stat_.total_calls;
}
uint64_t CallStat::bytes_transferred(std::optional<bool> success) const {
    if (success) {
        if (*success == true) {
            return success_stat_.bytes_transferred;
        } else {
            return failure_stat_.bytes_transferred;
        }
    }
    return success_stat_.bytes_transferred + failure_stat_.bytes_transferred;
}

void CallStat::Dump(FILE* stream, const char* stat_name, std::optional<bool> success) const {
    const char* stat_success = "aggregate";
    if (success) {
        if (*success) {
            stat_success = "success";
        } else {
            stat_success = "failure";
        }
    }
    fprintf(stream, "%s.%s.total_calls:         %lu\n", stat_name, stat_success,
            total_calls(success));
    fprintf(stream, "%s.%s.total_time_spent:    %lu\n", stat_name, stat_success,
            total_time_spent(success));
    fprintf(stream, "%s.%s.maximum_latency:     %lu\n", stat_name, stat_success,
            maximum_latency(success));
    fprintf(stream, "%s.%s.minimum_latency:     %lu\n", stat_name, stat_success,
            minimum_latency(success) == kUninitializedMinimumLatency ? 0
                                                                     : minimum_latency(success));
    fprintf(stream, "%s.%s.bytes_transferred:   %lu\n", stat_name, stat_success,
            bytes_transferred(success));
    fprintf(stream, "\n");
}

void CallStat::UpdateCallStat(bool success, zx_ticks_t delta_time, uint64_t bytes_transferred) {
    CallStatRaw* stat;
    if (success) {
        stat = &success_stat_;
    } else {
        stat = &failure_stat_;
    }

    stat->UpdateRawCallStat(delta_time, bytes_transferred);
}

void Metrics::SetEnable(bool enable) {
    enabled_ = enable;
}

bool Metrics::Enabled() const {
    return enabled_;
}

FsMetrics::FsMetrics(const fuchsia_storage_metrics_FsMetrics* metrics) {
    create_.CopyFromFidl(&metrics->create);
    read_.CopyFromFidl(&metrics->read);
    write_.CopyFromFidl(&metrics->write);
    truncate_.CopyFromFidl(&metrics->truncate);
    unlink_.CopyFromFidl(&metrics->unlink);
    rename_.CopyFromFidl(&metrics->rename);
    lookup_.CopyFromFidl(&metrics->lookup);
    open_.CopyFromFidl(&metrics->open);

    SetEnable(true);
}

void FsMetrics::CopyToFidl(fuchsia_storage_metrics_FsMetrics* metrics) const {
    create_.CopyToFidl(&metrics->create);
    read_.CopyToFidl(&metrics->read);
    write_.CopyToFidl(&metrics->write);
    truncate_.CopyToFidl(&metrics->truncate);
    unlink_.CopyToFidl(&metrics->unlink);
    rename_.CopyToFidl(&metrics->rename);
    lookup_.CopyToFidl(&metrics->lookup);
    open_.CopyToFidl(&metrics->open);
}

void FsMetrics::Dump(FILE* stream, std::optional<bool> success) const {
    create_.Dump(stream, "create", success);
    read_.Dump(stream, "read", success);
    write_.Dump(stream, "write", success);
    truncate_.Dump(stream, "truncate", success);
    unlink_.Dump(stream, "unlink", success);
    rename_.Dump(stream, "rename", success);
    lookup_.Dump(stream, "lookup", success);
    open_.Dump(stream, "open", success);
}

} // namespace storage_metrics
