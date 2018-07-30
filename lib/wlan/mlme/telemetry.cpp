// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/telemetry.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/logging.h>
#include <wlan/mlme/dispatcher.h>

#include "lib/component/cpp/environment_services.h"

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace cobalt = ::fuchsia::cobalt;

Telemetry::Telemetry(Dispatcher* dispatcher) : dispatcher_(dispatcher) {}

Telemetry::~Telemetry() {
    StopWorker();
}

void Telemetry::StartWorker(uint8_t report_period_minutes) {
    if (is_active_) {
        errorf(
            "telemetry: could not initialize the wlan telemetry thread, another thread is already "
            "running\n");
        return;
    }

    is_active_ = true;
    worker_thread_ =
        std::thread(&Telemetry::CobaltReporter, this, std::chrono::minutes(report_period_minutes));
}

void Telemetry::StopWorker() {
    is_active_ = false;
    if (worker_thread_.joinable()) { worker_thread_.join(); }
}

void Telemetry::CobaltReporter(std::chrono::minutes report_period) {
    infof("telemetry: thread started\n");
    auto encoder = ConnectToEnvironmentService();

    while (is_active_) {
        wlan_mlme::StatsQueryResponse stats_response = dispatcher_->GetStatsToFidl();
        uint64_t packet_count = stats_response.stats.dispatcher_stats.any_packet.in.count;

        cobalt::Status status = cobalt::Status::INTERNAL_ERROR;
        encoder->AddIntObservation(CobaltMetricId::kPacketCounter, CobaltEncodingId::kRawEncoding,
                                   packet_count, &status);

        if (status != cobalt::Status::OK) {
            errorf("telemetry: could not add dispatcher observation\n");
        } else {
            dispatcher_->ResetStats();
        }

        // TODO(alexandrew): Fix the report timing, since the FIDL construction
        // takes time.
        std::this_thread::sleep_for(report_period);
    }
}

cobalt::EncoderSyncPtr Telemetry::ConnectToEnvironmentService() {
    cobalt::EncoderSyncPtr encoder;
    cobalt::EncoderFactorySyncPtr factory;
    component::ConnectToEnvironmentService(factory.NewRequest());
    factory->GetEncoder(kCobaltProjectId, encoder.NewRequest());
    infof("telemetry: connected to Cobalt\n");
    return encoder;
}

}  // namespace wlan
