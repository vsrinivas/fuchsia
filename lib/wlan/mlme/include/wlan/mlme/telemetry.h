// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <thread>

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/logging.h>

namespace wlan {

class Dispatcher;

// LINT.IfChange
constexpr uint32_t kCobaltProjectId = 106;
// LINT.ThenChange(//third_party/cobalt_config/projects.yaml)

// LINT.IfChange
enum CobaltMetricId : uint32_t {
    kDispatcherPacketCounter = 5,
};

enum CobaltEncodingId : uint32_t {
    kRawEncoding = 1,
};
// LINT.ThenChange(//third_party/cobalt_config/fuchsia/wlan/config.yaml)

class Telemetry {
   public:
    Telemetry(Dispatcher* dispatcher);
    ~Telemetry();

    void StartWorker(uint8_t report_period_minutes);
    void StopWorker();

   private:
    // Every report_period minutes, the worker Cobalt reporter sends data to Cobalt.
    void CobaltReporter(std::chrono::minutes report_period);
    ::fuchsia::cobalt::EncoderSyncPtr ConnectToEnvironmentService();
    void ReportDispatcherStats(const ::fuchsia::wlan::stats::DispatcherStats& stats);
    void ReportDispatcherPackets(const uint8_t packet_type_index, const uint64_t packet_count);

    Dispatcher* dispatcher_;
    ::fuchsia::cobalt::EncoderSyncPtr encoder_;
    std::thread worker_thread_;
    std::atomic<bool> is_active_{false};
};

}  // namespace wlan
