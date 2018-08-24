// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/telemetry.h>

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/logging.h>
#include <wlan/mlme/dispatcher.h>

#include "lib/svc/cpp/services.h"

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_stats = ::fuchsia::wlan::stats;
namespace cobalt = ::fuchsia::cobalt;

Telemetry::Telemetry(Dispatcher* dispatcher, std::shared_ptr<component::Services> services)
    : dispatcher_(dispatcher), services_(services) {}

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
    encoder_ = ConnectToService();

    while (is_active_) {
        wlan_mlme::StatsQueryResponse stats_response = dispatcher_->GetStatsToFidl();
        dispatcher_->ResetStats();

        if (stats_response.stats.mlme_stats) {
            ReportClientMlmeStats(stats_response.stats.mlme_stats->client_mlme_stats());
        }

        // TODO(alexandrew): Fix the report timing, since the FIDL construction
        // takes time.
        std::this_thread::sleep_for(report_period);
    }
}

cobalt::EncoderSyncPtr Telemetry::ConnectToService() {
    cobalt::EncoderSyncPtr encoder;
    cobalt::EncoderFactorySyncPtr factory;
    services_->ConnectToService(factory.NewRequest());
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

    if (status != cobalt::Status::OK) {
        errorf("telemetry: could not add dispatcher packet observation %d\n", status);
    }
}

void Telemetry::ReportClientMlmeStats(const wlan_stats::ClientMlmeStats& stats) {
    ReportRssiStats(kClientAssocDataRssi, stats.assoc_data_rssi);
    ReportRssiStats(kClientBeaconRssi, stats.beacon_rssi);
}

void Telemetry::ReportRssiStats(const uint32_t rssi_metric_id, const wlan_stats::RssiStats& stats) {
    fidl::VectorPtr<cobalt::BucketDistributionEntry> distribution;

    // In the internal stats histogram, hist[x] represents the number of frames
    // with RSSI -x. For the Cobalt representation, buckets from -128 to 0 are
    // used. When data is sent to Cobalt, the concept of index is utilized.
    //
    // Shortly, for Cobalt:
    // Bucket -128 -> index   0
    // Bucket -127 -> index   1
    // ...
    // Bucket    0 -> index 128
    //
    // The for loop below converts the stats internal representation to the
    // Cobalt representation and prepares the histogram that will be sent.
    const std::vector<uint64_t>& hist = stats.hist;
    for (uint8_t bin = 0; bin < wlan_stats::RSSI_BINS; ++bin) {
        if (hist[bin]) {
            cobalt::BucketDistributionEntry entry;
            entry.index = wlan_stats::RSSI_BINS - bin - 1;
            entry.count = hist[bin];
            distribution.push_back(std::move(entry));
        }
    }

    if (distribution->empty()) { return; }

    cobalt::Status status = cobalt::Status::INTERNAL_ERROR;
    encoder_->AddIntBucketDistribution(rssi_metric_id, CobaltEncodingId::kRawEncoding,
                                       std::move(distribution), &status);

    if (status != cobalt::Status::OK) {
        errorf("telemetry: could not add client RSSI observation: %d\n", status);
    }
}

}  // namespace wlan
