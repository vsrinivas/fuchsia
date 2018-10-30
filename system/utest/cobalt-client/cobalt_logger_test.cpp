// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <cobalt-client/cpp/collector-internal.h>
#include <cobalt-client/cpp/types-internal.h>
#include <fbl/algorithm.h>
#include <fbl/type_info.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/cobalt/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>

#include <unittest/unittest.h>

namespace cobalt_client {
namespace internal {
namespace {

// 1 Kb VMO.
constexpr size_t kVmoSize = 1 << 10;

// Number of buckets for the histogram.
constexpr uint32_t kNumBuckets = 10;

// Value of the counter.
constexpr uint32_t kCounterValue = 21;

// Metric Id.
constexpr uint32_t kMetricId = 25;

constexpr uint32_t kEventCode = 26;

// Expected stage.
constexpr ReleaseStage kReleaseStage = ReleaseStage::kDebug;

// Expected path.
constexpr char kSvcPath[] = "/svc/cobalt_service";

// Component name being logged.
constexpr char kComponent[] = "ImportantComponent";

RemoteMetricInfo MakeRemoteMetricInfo() {
    RemoteMetricInfo metric_info;
    metric_info.metric_id = kMetricId;
    metric_info.event_code = kEventCode;
    metric_info.component = kComponent;
    return metric_info;
}

// Handles RPC call to LoggerSimple.
class FakeSimpleLogger {
public:
    zx_status_t LogIntHistogram(uint32_t metric_id, uint32_t event_code, const char* component_data,
                                size_t component_size, const uint32_t* bucket_indices_data,
                                size_t bucket_indices_count, const uint64_t* bucket_counts_data,
                                size_t bucket_counts_count, fidl_txn_t* txn) {
        EXPECT_EQ(metric_id, kMetricId);
        EXPECT_EQ(bucket_indices_count, bucket_counts_count);
        for (size_t i = 0; i < bucket_indices_count; ++i) {
            // We enforce our test data to be bucket_i = i
            EXPECT_EQ(bucket_counts_data[bucket_indices_data[i]], bucket_indices_data[i]);
        }
        // TODO(gevalentino): Verify |event_code| and |component_data| once cobalt
        // allows it.
        return fuchsia_cobalt_LoggerSimpleLogIntHistogram_reply(txn, response_status_);
    }

    zx_status_t LogCounter(uint32_t metric_id, uint32_t event_code, const char* component_data,
                           size_t component_size, int64_t duration_ms, int64_t count,
                           fidl_txn_t* txn) {
        EXPECT_EQ(metric_id, kMetricId);
        EXPECT_EQ(count, kCounterValue);
        // TODO(gevalentino): Verify |event_code| and |component_data| once cobalt
        // allows it.
        return fuchsia_cobalt_LoggerSimpleLogEventCount_reply(txn, response_status_);
    }

    zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel channel) {
        using Binder = fidl::Binder<FakeSimpleLogger>;
        static constexpr fuchsia_cobalt_LoggerSimple_ops_t kOps = {
            .LogEvent = nullptr,
            .LogEventCount = Binder::BindMember<&FakeSimpleLogger::LogCounter>,
            .LogElapsedTime = nullptr,
            .LogFrameRate = nullptr,
            .LogMemoryUsage = nullptr,
            .LogString = nullptr,
            .StartTimer = nullptr,
            .EndTimer = nullptr,
            .LogIntHistogram = Binder::BindMember<&FakeSimpleLogger::LogIntHistogram>,
        };
        return Binder::BindOps<fuchsia_cobalt_LoggerSimple_dispatch>(dispatcher, fbl::move(channel),
                                                                     this, &kOps);
    }

    void set_response_status(fuchsia_cobalt_Status status) { response_status_ = status; }

private:
    fuchsia_cobalt_Status response_status_ = fuchsia_cobalt_Status_OK;
};

// Handles RPC calls to LoggerFactory.
class FakeLoggerFactory {
public:
    FakeLoggerFactory() : logger_binder_(nullptr) {}

    zx_status_t CreateLogger(const fuchsia_cobalt_ProjectProfile* profile, zx_handle_t logger,
                             fidl_txn_t* txn) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t CreateLoggerSimple(const fuchsia_cobalt_ProjectProfile* profile, zx_handle_t logger,
                                   fidl_txn_t* txn) {
        zx::vmo config(profile->config.vmo);
        EXPECT_TRUE(config.is_valid());
        size_t actual_size;
        config.get_size(&actual_size);
        size_t expected_size = fbl::round_up(kVmoSize, static_cast<size_t>(PAGE_SIZE));
        EXPECT_EQ(profile->config.size, kVmoSize);
        EXPECT_EQ(actual_size, expected_size);
        EXPECT_EQ(profile->release_stage,
                  static_cast<fbl::underlying_type<ReleaseStage>::type>(kReleaseStage));
        if (logger_binder_) {
            logger_binder_(logger);
        } else {
            zx_handle_close(logger);
        }
        return fuchsia_cobalt_LoggerFactoryCreateLoggerSimple_reply(txn, logger_create_status);
    }

    zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel channel) {
        using Binder = fidl::Binder<FakeLoggerFactory>;
        static constexpr fuchsia_cobalt_LoggerFactory_ops_t kOps = {
            .CreateLogger = Binder::BindMember<&FakeLoggerFactory::CreateLogger>,
            .CreateLoggerSimple = Binder::BindMember<&FakeLoggerFactory::CreateLoggerSimple>,
        };
        return Binder::BindOps<fuchsia_cobalt_LoggerFactory_dispatch>(
            dispatcher, fbl::move(channel), this, &kOps);
    }

    void set_logger_create_status(fuchsia_cobalt_Status status) { logger_create_status = status; }

    void set_logger_binder(fbl::Function<void(zx_handle_t)> logger_binder) {
        logger_binder_ = fbl::move(logger_binder);
    }

private:
    fbl::Function<void(zx_handle_t)> logger_binder_;
    fuchsia_cobalt_Status logger_create_status = fuchsia_cobalt_Status_OK;
};

CobaltOptions MakeOptions(bool config_reader, zx::channel* svc_channel,
                          zx_status_t service_connect = ZX_OK) {
    CobaltOptions options;
    options.service_path.AppendPrintf("%s", kSvcPath);
    options.logger_deadline = zx::nsec(5);
    options.logger_deadline_first_attempt = zx::msec(5);
    options.config_reader = [config_reader](zx::vmo* config, size_t* size) {
        zx_status_t res = zx::vmo::create(kVmoSize, 0, config);
        *size = kVmoSize;
        return res == ZX_OK && config_reader;
    };
    options.service_connect = [svc_channel, service_connect](const char* path,
                                                             zx::channel channel) {
        svc_channel->reset(channel.release());
        return service_connect;
    };
    options.release_stage = ReleaseStage::kDebug;
    return options;
}

// Template for providing checks on FIDL calls.
template <typename MetricType, typename BufferType>
class CobaltLoggerTestBase {
public:
    // Verify we do not keep waiting on reply, after we failed to connect to the initial
    // service (LoggerFactory).
    static bool ServiceConnectionFailed() {
        BEGIN_TEST;
        Context context;
        context.return_values.service_connect = ZX_ERR_NOT_DIR;
        SetEventBuffer(&context);
        fbl::unique_ptr<CobaltLogger> logger = context.MakeLogger();
        ASSERT_FALSE(logger->Log(MakeRemoteMetricInfo(), context.event_buffer));
        ASSERT_FALSE(logger->IsListeningForReply());
        END_TEST;
    }

    // When we fail to read the configuration, we should not be waiting for any reply.
    static bool ConfigurationReadFailed() {
        BEGIN_TEST;
        Context context;
        context.return_values.config_reader = false;
        SetEventBuffer(&context);
        fbl::unique_ptr<CobaltLogger> logger = context.MakeLogger();
        ASSERT_FALSE(logger->Log(MakeRemoteMetricInfo(), context.event_buffer));
        ASSERT_FALSE(logger->IsListeningForReply());
        END_TEST;
    }

    // When we connect to the service (LoggerFactory), we should be listening for a reply,
    // which represents the binding of the SimpleLogger logger service.
    static bool ServiceConnectedWaitsForReply() {
        BEGIN_TEST;
        Context context;
        SetEventBuffer(&context);
        fbl::unique_ptr<CobaltLogger> logger = context.MakeLogger();
        // In order to capture the other endpoint of the channel, we need to attempt to
        // connect first. This will set |Context::channels::factory| to the other endpoint.
        ASSERT_FALSE(logger->Log(MakeRemoteMetricInfo(), context.event_buffer));
        // service_connect returned |ZX_OK|, so we should be waiting for a reply, meaning
        // each call to Log, will assert the channel for a reply.
        ASSERT_TRUE(logger->IsListeningForReply());
        END_TEST;
    }

    // When we connect to the service (LoggerFactory), and the service replied,
    // the we should no longer be listeining for a reply.
    static bool ServiceReplied() {
        BEGIN_TEST;
        Context context;
        SetEventBuffer(&context);
        fbl::unique_ptr<CobaltLogger> logger = context.MakeLogger();
        // In order to capture the other endpoint of the channel, we need to attempt to
        // connect first. This will set |Context::channels::factory| to the other endpoint.
        ASSERT_FALSE(logger->Log(MakeRemoteMetricInfo(), context.event_buffer));
        // We set a bad status, so the reply is handled, but we are not able to log.
        context.services.factory.set_logger_create_status(fuchsia_cobalt_Status_INVALID_ARGUMENTS);
        // Now we can start servicing factory requests.
        ASSERT_TRUE(context.StartFactoryService());
        ASSERT_TRUE(context.services.ProcessAllMessages());

        // Now that the service has started, but no bound SimpleLoggerService exists,
        // the log will still fail, BUT we will not longer be waiting for a reply.
        ASSERT_EQ(logger->WaitForReply(), ZX_OK);
        ASSERT_FALSE(logger->Log(MakeRemoteMetricInfo(), context.event_buffer));
        ASSERT_FALSE(logger->IsListeningForReply());
        END_TEST;
    }

    static bool RetryOnFactoryPeerClosed() {
        BEGIN_TEST;
        Context context;
        SetEventBuffer(&context);
        fbl::unique_ptr<CobaltLogger> logger = context.MakeLogger();
        // In order to capture the other endpoint of the channel, we need to attempt to
        // connect first. This will set |Context::channels::factory| to the other endpoint.
        ASSERT_FALSE(logger->Log(MakeRemoteMetricInfo(), context.event_buffer));
        ASSERT_TRUE(logger->IsListeningForReply());

        // Close the channel instead of binding it. After we attempt to connect again,
        // the factory channel should be valid again, and we should be waiting for a reply.
        context.channels.factory.reset();
        zx_signals_t observed;

        // Wait for the channel to close.
        ASSERT_EQ(logger->WaitForReply(&observed), ZX_OK);
        ASSERT_NE(observed & ZX_CHANNEL_PEER_CLOSED, 0);

        // Restablish the channel with the Factory service.
        ASSERT_FALSE(logger->Log(MakeRemoteMetricInfo(), context.event_buffer));
        ASSERT_TRUE(logger->IsListeningForReply());

        ASSERT_TRUE(context.channels.factory.is_valid());
        END_TEST;
    }

    static bool RetryOnLoggerPeerClosed() {
        BEGIN_TEST;
        Context context;
        SetEventBuffer(&context);
        fbl::unique_ptr<CobaltLogger> logger = context.MakeLogger();
        // Return OK, and the closing channel can be interpreted as something going
        // wrong after we set up the connection.
        context.services.factory.set_logger_create_status(fuchsia_cobalt_Status_OK);
        // Instead of binding the channel, close it.
        context.services.factory.set_logger_binder(
            [](zx_handle_t logger) { zx_handle_close(logger); });

        // In order to capture the other endpoint of the channel, we need to attempt to
        // connect first. This will set |Context::channels::factory| to the other endpoint.
        ASSERT_FALSE(logger->Log(MakeRemoteMetricInfo(), context.event_buffer));
        // Now we can start servicing factory requests.
        ASSERT_TRUE(context.StartFactoryService());
        ASSERT_TRUE(context.services.ProcessAllMessages());
        ASSERT_EQ(logger->WaitForReply(), ZX_OK);
        ASSERT_FALSE(logger->Log(MakeRemoteMetricInfo(), context.event_buffer));
        ASSERT_FALSE(logger->IsListeningForReply());
        END_TEST;
    }

    // When we connect to the service (LoggerFactory), and the service replied,
    // the we should no longer be listeining for a reply.
    static bool LogSuccessfully() {
        BEGIN_TEST;
        Context context;
        SetEventBuffer(&context);
        fbl::unique_ptr<CobaltLogger> logger = context.MakeLogger();
        // When requesting a LoggerSimple from the factory, bind it to the channel.
        context.EnableLoggerService();
        // Now that we are binding a logger, return OK.
        context.services.factory.set_logger_create_status(fuchsia_cobalt_Status_OK);

        // In order to capture the other endpoint of the channel, we need to attempt to
        // connect first. This will set |Context::channels::factory| to the other endpoint.
        ASSERT_FALSE(logger->Log(MakeRemoteMetricInfo(), context.event_buffer));
        // Now we can start servicing factory requests.
        ASSERT_TRUE(context.StartFactoryService());
        ASSERT_TRUE(context.services.ProcessAllMessages());
        ASSERT_EQ(logger->WaitForReply(), ZX_OK);
        ASSERT_TRUE(logger->Log(MakeRemoteMetricInfo(), context.event_buffer));
        END_TEST;
    }

private:
    // Collection of data for setting up the environment for requests,
    // and methods for setting them up.
    struct Context {

        Context() {
            services.loop =
                fbl::move(fbl::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread));
        }

        struct ReturnValues {
            bool config_reader = true;
            zx_status_t service_connect = ZX_OK;
        };

        struct Services {
            Services() : loop(nullptr), factory() {}

            bool ProcessAllMessages() {
                BEGIN_HELPER;
                ASSERT_EQ(loop->RunUntilIdle(), ZX_OK);
                END_HELPER;
            }

            fbl::unique_ptr<async::Loop> loop;
            FakeLoggerFactory factory;
            FakeSimpleLogger logger;
            bool factory_close_channel_on_logger_create;
        };

        struct Channels {
            zx::channel factory;
        };

        fbl::unique_ptr<CobaltLogger> MakeLogger() {
            CobaltOptions options = MakeOptions(return_values.config_reader, &channels.factory,
                                                return_values.service_connect);
            fbl::unique_ptr<CobaltLogger> logger =
                fbl::make_unique<CobaltLogger>(fbl::move(options));
            services.loop->StartThread("FactoryServiceThread");
            return logger;
        }

        // When called will wait for a request to become available in the factory channel,
        // and will then bind the |FakeFactory| service to it. This wait, allows preventing race
        // conditions, such as waiting for requests to be added to the dispatcher port, after we
        // wait for |async::Loop::RunUntilIdle|.
        bool StartFactoryService() {
            BEGIN_HELPER;
            ASSERT_EQ(zx_object_wait_one(channels.factory.get(), ZX_CHANNEL_READABLE,
                                         zx::time::infinite().get(), nullptr),
                      ZX_OK);
            ASSERT_EQ(
                services.factory.Bind(services.loop->dispatcher(), fbl::move(channels.factory)),
                ZX_OK);
            END_HELPER;
        }

        // When called, next call to |Context::StartFactoryService|, will bind the
        // request<logger> to |FakeLoggerService| instance.
        void EnableLoggerService() {
            services.factory.set_logger_binder([this](zx_handle_t logger) {
                services.logger.Bind(services.loop->dispatcher(), zx::channel(logger));
            });
        }

        // Set of return values for stubbed parts of the Logger.
        // Usually these involve an IPC or interacting with another service or process.
        ReturnValues return_values;

        // Set of in process FIDL services.
        Services services;

        // Set of channels extracted to allow communication with with other in process
        // services.
        Channels channels;

        // Memory for the internal buffer.
        BufferType internal_buffer;

        // Event buffer sent to the SimpleLogger service.
        typename MetricType::EventBuffer event_buffer = typename MetricType::EventBuffer();
    };

    // Sets the data of the event buffer for the context.
    static void SetEventBuffer(Context* context);
};

template <>
void CobaltLoggerTestBase<RemoteCounter, uint32_t>::SetEventBuffer(
    CobaltLoggerTestBase<RemoteCounter, uint32_t>::Context* context) {
    *context->event_buffer.mutable_event_data() = kCounterValue;
}

template <>
void CobaltLoggerTestBase<RemoteHistogram, fbl::Vector<HistogramBucket>>::SetEventBuffer(
    CobaltLoggerTestBase<RemoteHistogram, fbl::Vector<HistogramBucket>>::Context* context) {
    auto& buffer = context->event_buffer;
    auto* buckets = &context->internal_buffer;

    for (uint32_t bucket_index = 0; bucket_index < kNumBuckets; ++bucket_index) {
        HistogramBucket bucket;
        bucket.count = bucket_index;
        bucket.index = bucket_index;
        buckets->push_back(bucket);
    }

    buffer.mutable_event_data()->set_data(buckets->get());
    buffer.mutable_event_data()->set_count(kNumBuckets);
}

// Test instance for logging Histograms.
using LogHistogramTest = CobaltLoggerTestBase<RemoteHistogram, fbl::Vector<HistogramBucket>>;
// Test instance for logging Counters.
using LogCounterTest = CobaltLoggerTestBase<RemoteCounter, uint32_t>;

BEGIN_TEST_CASE(CobaltLoggerTest)
RUN_TEST(LogHistogramTest::ServiceConnectionFailed)
RUN_TEST(LogHistogramTest::ConfigurationReadFailed)
RUN_TEST(LogHistogramTest::ServiceConnectedWaitsForReply)
RUN_TEST(LogHistogramTest::ServiceReplied)
RUN_TEST(LogHistogramTest::RetryOnFactoryPeerClosed)
RUN_TEST(LogHistogramTest::RetryOnLoggerPeerClosed)
RUN_TEST(LogHistogramTest::LogSuccessfully)
RUN_TEST(LogCounterTest::ServiceConnectionFailed)
RUN_TEST(LogCounterTest::ConfigurationReadFailed)
RUN_TEST(LogCounterTest::ServiceConnectedWaitsForReply)
RUN_TEST(LogCounterTest::ServiceReplied)
RUN_TEST(LogCounterTest::RetryOnFactoryPeerClosed)
RUN_TEST(LogCounterTest::RetryOnLoggerPeerClosed)
RUN_TEST(LogCounterTest::LogSuccessfully)
END_TEST_CASE(CobaltLoggerTest)

} // namespace
} // namespace internal
} // namespace cobalt_client
