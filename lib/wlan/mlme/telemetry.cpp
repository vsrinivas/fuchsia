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
namespace wlan_stats = ::fuchsia::wlan::stats;
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
    encoder_ = ConnectToEnvironmentService();

    while (is_active_) {
        wlan_mlme::StatsQueryResponse stats_response = dispatcher_->GetStatsToFidl();
        ReportDispatcherStats(stats_response.stats.dispatcher_stats);
        dispatcher_->ResetStats();

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

void Telemetry::ReportDispatcherStats(const wlan_stats::DispatcherStats& stats) {
    // LINT.IfChange
    constexpr uint8_t kDispatcherInPacketCountIndex = 0u;
    constexpr uint8_t kDispatcherOutPacketCountIndex = 1u;
    constexpr uint8_t kDispatcherDropPacketCountIndex = 2u;
    // LINT.ThenChange(//third_party/cobalt_config/fuchsia/wlan/config.yaml)

    ReportDispatcherPackets(kDispatcherInPacketCountIndex, stats.any_packet.in.count);
    ReportDispatcherPackets(kDispatcherOutPacketCountIndex, stats.any_packet.out.count);
    ReportDispatcherPackets(kDispatcherDropPacketCountIndex, stats.any_packet.drop.count);
}

void Telemetry::ReportDispatcherPackets(const uint8_t packet_type_index,
                                        const uint64_t packet_count) {
    auto values = fidl::VectorPtr<cobalt::ObservationValue>::New(2);
    values->at(0).name = "packet_type_index";
    values->at(0).value.set_index_value(packet_type_index);
    values->at(0).encoding_id = CobaltEncodingId::kRawEncoding;
    values->at(1).name = "packet_count";
    values->at(1).value.set_int_value(packet_count);
    values->at(1).encoding_id = CobaltEncodingId::kRawEncoding;

    cobalt::Status status = cobalt::Status::INTERNAL_ERROR;
    encoder_->AddMultipartObservation(CobaltMetricId::kDispatcherPacketCounter, std::move(values),
                                      &status);

    // TODO(alexandrew): Throttle this error when Cobalt is down.
    if (status != cobalt::Status::OK) {
        errorf("telemetry: could not add dispatcher packet observation\n");
    }
}

}  // namespace wlan
