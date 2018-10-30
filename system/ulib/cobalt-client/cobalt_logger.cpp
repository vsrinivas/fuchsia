// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/collector-internal.h>
#include <fuchsia/cobalt/c/fidl.h>
#include <lib/fdio/util.h>
#include <lib/fidl/coding.h>
#include <lib/fidl/cpp/vector_view.h>

namespace cobalt_client {
namespace internal {
namespace {

// Magic for initiating an async transaction. It is ok to reuse the same number, since
// we will never issue another request until a reply is issued, or the channel is closed,
// so number of 'on-the-fly' transactions will always be one.
// This is not necessary for sync bindings, because channel_call will fill it for us.
constexpr zx_txid_t kFactoryRequestTxnId = 1u;

// We reuse the same channel that is connecting the factory.
// TODO(gevalentino): When async FIDL bindings become available, use this.
zx_status_t SendLoggerSimpleCreateRequest(zx::channel* logger_factory_client,
                                          zx::channel* logger_svc, zx::vmo* config,
                                          size_t config_size, ReleaseStage release_stage) {
    uint32_t msg_size = sizeof(fuchsia_cobalt_LoggerFactoryCreateLoggerSimpleRequest);
    FIDL_ALIGNDECL uint8_t msg[msg_size];
    memset(msg, 0, sizeof(msg));
    fuchsia_cobalt_LoggerFactoryCreateLoggerSimpleRequest* request =
        reinterpret_cast<fuchsia_cobalt_LoggerFactoryCreateLoggerSimpleRequest*>(msg);
    request->hdr.txid = kFactoryRequestTxnId;
    request->hdr.ordinal = fuchsia_cobalt_LoggerFactoryCreateLoggerSimpleOrdinal;
    request->logger = logger_svc->release();

    request->profile.release_stage =
        static_cast<fbl::underlying_type<ReleaseStage>::type>(release_stage);
    request->profile.config.size = config_size;
    request->profile.config.vmo = config->release();
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t num_handles = 0;
    zx_status_t result =
        fidl_encode(&fuchsia_cobalt_LoggerFactoryCreateLoggerSimpleRequestTable, msg, msg_size,
                    handles, ZX_CHANNEL_MAX_MSG_HANDLES, &num_handles, nullptr);
    if (result != ZX_OK) {
        return result;
    }
    return logger_factory_client->write(0l, msg, msg_size, handles, num_handles);
}

zx_status_t ReadLoggerSimpleCreateResponse(zx::channel* logger, fuchsia_cobalt_Status* out_status) {
    uint32_t msg_size = sizeof(fuchsia_cobalt_LoggerSimpleLogIntHistogramResponse);
    FIDL_ALIGNDECL uint8_t msg[msg_size];
    uint32_t read_bytes = 0;
    zx_status_t result = logger->read(0l, &msg, msg_size, &read_bytes, nullptr, 0, nullptr);
    if (result != ZX_OK) {
        return result;
    }
    fuchsia_cobalt_LoggerFactoryCreateLoggerSimpleResponse* response =
        reinterpret_cast<fuchsia_cobalt_LoggerFactoryCreateLoggerSimpleResponse*>(msg);
    *out_status = response->status;
    return ZX_OK;
}

void HandleChannelStatus(zx::channel* logger_client, zx_status_t result) {
    switch (result) {
    case ZX_ERR_PEER_CLOSED:
        logger_client->reset();
        break;
    default:
        break;
    };
}

} // namespace

CobaltLogger::CobaltLogger(CobaltOptions options)
    : options_(fbl::move(options)), logger_(ZX_HANDLE_INVALID) {}

bool CobaltLogger::Log(const RemoteMetricInfo& metric_info,
                       const RemoteHistogram::EventBuffer& histogram) {
    if (!IsLoggerReady()) {
        return false;
    }

    const size_t buckets = histogram.event_data().count();
    uint32_t indexes[buckets];
    BaseHistogram::Count counts[buckets];
    // TODO(gevalentino): Update this method once the SimpleLayout limitations are gone.
    // Probably add LogBatchHistograms method to the logger, for efficient logging.
    for (uint32_t bucket_index = 0; bucket_index < buckets; ++bucket_index) {
        indexes[bucket_index] = histogram.event_data()[bucket_index].index;
        counts[bucket_index] = histogram.event_data()[bucket_index].count;
    };
    fuchsia_cobalt_Status cobalt_status;
    // TODO(gevalentino): Use RemoteMetricInfo::event_cote and RemoteMetricInfo::component once
    // availbale.
    zx_status_t result = fuchsia_cobalt_LoggerSimpleLogIntHistogram(
        logger_.get(), metric_info.metric_id, 0, nullptr, 0, indexes, buckets, counts, buckets,
        &cobalt_status);
    HandleChannelStatus(&logger_, result);
    return result == ZX_OK && cobalt_status == fuchsia_cobalt_Status_OK;
}

bool CobaltLogger::Log(const RemoteMetricInfo& metric_info,
                       const RemoteCounter::EventBuffer& counter) {
    if (!IsLoggerReady()) {
        return false;
    }

    fuchsia_cobalt_Status cobalt_status;
    // TODO(gevalentino): Use RemoteMetricInfo::event_cote and RemoteMetricInfo::component once
    // availbale.
    zx_status_t result = fuchsia_cobalt_LoggerBaseLogEventCount(
        logger_.get(), metric_info.metric_id, 0, nullptr, 0, 0,
        static_cast<int64_t>(counter.event_data()), &cobalt_status);
    HandleChannelStatus(&logger_, result);
    return result == ZX_OK && cobalt_status == fuchsia_cobalt_Status_OK;
}

bool CobaltLogger::HasCobaltReplied(zx::duration deadline) {
    is_first_attempt_ = false;
    zx_signals_t observed;
    zx_status_t result = logger_factory_.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                                  zx::deadline_after(deadline), &observed);
    if (result != ZX_OK) {
        if (result != ZX_ERR_TIMED_OUT) {
            logger_factory_.reset();
        }
        return false;
    }

    if (ZX_CHANNEL_PEER_CLOSED & observed) {
        logger_factory_.reset();
        TrySendLoggerRequest();
        return false;
    }

    // Read from the channel and check the returned status
    fuchsia_cobalt_Status status = fuchsia_cobalt_Status_OK;
    result = ReadLoggerSimpleCreateResponse(&logger_factory_, &status);
    // If the error is on our side, then reset, so we can try again later.
    if (status != fuchsia_cobalt_Status_OK && status != fuchsia_cobalt_Status_BUFFER_FULL) {
        logger_factory_.reset();
        return false;
    }
    HandleChannelStatus(&logger_factory_, result);
    return result == ZX_OK && status == fuchsia_cobalt_Status_OK;
}

bool CobaltLogger::TrySendLoggerRequest() {
    zx::channel logger_service, logger_client;
    zx::channel logger_factory, logger_factory_client;
    if (zx::channel::create(0, &logger_service, &logger_client) != ZX_OK) {
        return false;
    }

    if (zx::channel::create(0, &logger_factory, &logger_factory_client) != ZX_OK) {
        return false;
    }

    // Attempt to connect to LoggerFactory.
    if (options_.service_connect(options_.service_path.c_str(), fbl::move(logger_factory)) !=
        ZX_OK) {
        return false;
    }
    // Write a CreateLogger message into the channel.
    zx::vmo config;
    size_t config_size;
    if (!options_.config_reader(&config, &config_size)) {
        return false;
    }
    zx_status_t res;

    if ((res = SendLoggerSimpleCreateRequest(&logger_factory_client, &logger_service, &config,
                                             config_size, options_.release_stage)) != ZX_OK) {
        return false;
    }
    is_first_attempt_ = true;
    logger_factory_.reset(logger_factory_client.release());
    logger_.reset(logger_client.release());
    return true;
}

bool CobaltLogger::IsLoggerReady() {
    if (!logger_.is_valid() && !TrySendLoggerRequest()) {
        return false;
    }
    // if we are connecting, wait for |polling_deadline_| for a response to become available.
    // If the channel does not become readable, return as 'failed' and don't push the data yet.
    zx::duration deadline =
        is_first_attempt_ ? options_.logger_deadline_first_attempt : options_.logger_deadline;
    if (logger_factory_.is_valid() && !HasCobaltReplied(deadline)) {
        return false;
    }
    logger_factory_.reset();
    return true;
}

} // namespace internal
} // namespace cobalt_client
