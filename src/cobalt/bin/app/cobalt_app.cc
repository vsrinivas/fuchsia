// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/cobalt_app.h"

#include <memory>

#include "lib/sys/cpp/component_context.h"
#include "src/cobalt/bin/app/activity_listener_impl.h"
#include "src/cobalt/bin/app/metric_event_logger_factory_impl.h"
#include "src/cobalt/bin/app/utils.h"
#include "src/cobalt/bin/utils/fuchsia_http_client.h"
#include "third_party/cobalt/src/lib/util/posix_file_system.h"
#include "third_party/cobalt/src/public/cobalt_config.h"
#include "third_party/cobalt/src/public/cobalt_service.h"

namespace cobalt {

using encoder::ClientSecret;
using logger::ProjectContextFactory;
using util::PosixFileSystem;
using utils::FuchsiaHTTPClient;

const size_t kMaxBytesPerEnvelope = 512 * 1024;  // 0.5 MiB.

constexpr char kMetricsRegistryPath[] = "/config/data/global_metrics_registry.pb";

constexpr char kObservationStorePath[] = "/data/observation_store";
constexpr char kLocalAggregateProtoStorePath[] = "/data/local_aggregate_store";
constexpr char kLocalAggregationPath[] = "/data/local_aggregate_storage";
constexpr char kObsHistoryProtoStorePath[] = "/data/obs_history_store";
constexpr char kSystemDataCachePrefix[] = "/data/system_data_";
constexpr char kLocalLogFilePath[] = "/data/cobalt_observations.pb";

const size_t kClearcutMaxRetries = 5;

std::unique_ptr<CobaltRegistry> ReadRegistry(const std::string& global_metrics_registry_path) {
  std::ifstream registry_file_stream;
  registry_file_stream.open(global_metrics_registry_path);
  FX_CHECK(registry_file_stream && registry_file_stream.good())
      << "Could not open the Cobalt global metrics registry: " << global_metrics_registry_path;
  std::string global_metrics_registry_bytes;
  global_metrics_registry_bytes.assign((std::istreambuf_iterator<char>(registry_file_stream)),
                                       std::istreambuf_iterator<char>());
  FX_CHECK(!global_metrics_registry_bytes.empty())
      << "Could not read the Cobalt global metrics registry: " << global_metrics_registry_path;

  auto cobalt_registry = std::make_unique<CobaltRegistry>();
  FX_CHECK(cobalt_registry->ParseFromString(global_metrics_registry_bytes))
      << "Unable to parse global metrics registry at: " << global_metrics_registry_path;

  return cobalt_registry;
}

CobaltConfig CobaltApp::CreateCobaltConfig(
    async_dispatcher_t* dispatcher, const std::string& global_metrics_registry_path,
    const FuchsiaConfigurationData& configuration_data, FuchsiaSystemClockInterface* system_clock,
    FuchsiaHTTPClient::LoaderFactory http_loader_factory, std::chrono::seconds target_interval,
    std::chrono::seconds min_interval, std::chrono::seconds initial_interval,
    size_t event_aggregator_backfill_days, bool use_memory_observation_store,
    size_t max_bytes_per_observation_store, const std::string& product_name,
    const std::string& board_name, const std::string& version,
    std::unique_ptr<ActivityListenerImpl> listener) {
  // |target_pipeline| is the pipeline used for sending data to cobalt. In particular, it is the
  // source of the encryption keys, as well as determining the destination for generated
  // observations (either clearcut, or the local filesystem).
  std::unique_ptr<TargetPipelineInterface> target_pipeline;
  const auto& backend_environment = configuration_data.GetBackendEnvironment();
  if (backend_environment == system_data::Environment::LOCAL) {
    target_pipeline = std::make_unique<LocalPipeline>();
  } else {
    target_pipeline = std::make_unique<TargetPipeline>(
        backend_environment, ReadPublicKeyPem(configuration_data.ShufflerPublicKeyPath()),
        ReadPublicKeyPem(configuration_data.AnalyzerPublicKeyPath()),
        std::make_unique<FuchsiaHTTPClient>(std::move(http_loader_factory)), kClearcutMaxRetries);
  }

  CobaltConfig cfg = {
      .product_name = product_name,
      .board_name_suggestion = board_name,
      .version = version,
      .release_stage = configuration_data.GetReleaseStage(),

      .file_system = std::make_unique<PosixFileSystem>(),
      .use_memory_observation_store = use_memory_observation_store,
      .max_bytes_per_event = fuchsia::cobalt::MAX_BYTES_PER_EVENT,
      .max_bytes_per_envelope = kMaxBytesPerEnvelope,
      .max_bytes_total = max_bytes_per_observation_store,
      .observation_store_directory = kObservationStorePath,

      .local_aggregate_proto_store_path = kLocalAggregateProtoStorePath,
      .obs_history_proto_store_path = kObsHistoryProtoStorePath,
      .local_aggregate_store_dir = kLocalAggregationPath,
      .local_aggregate_store_strategy = StorageStrategy::Delayed,

      .target_interval = target_interval,
      .min_interval = min_interval,
      .initial_interval = initial_interval,

      .target_pipeline = std::move(target_pipeline),

      .local_shipping_manager_path = kLocalLogFilePath,

      .api_key = configuration_data.GetApiKey(),
      .client_secret = CobaltApp::getClientSecret(),
      .global_registry = ReadRegistry(global_metrics_registry_path),

      .local_aggregation_backfill_days = event_aggregator_backfill_days,

      .validated_clock = system_clock,

      .activity_listener = std::move(listener),
  };
  return cfg;
}

CobaltApp CobaltApp::CreateCobaltApp(
    std::unique_ptr<sys::ComponentContext> context, async_dispatcher_t* dispatcher,
    std::chrono::seconds target_interval, std::chrono::seconds min_interval,
    std::chrono::seconds initial_interval, size_t event_aggregator_backfill_days,
    bool start_event_aggregator_worker, bool use_memory_observation_store,
    size_t max_bytes_per_observation_store, const std::string& product_name,
    const std::string& board_name, const std::string& version) {
  // Create the configuration data from the data in the filesystem.
  FuchsiaConfigurationData configuration_data;

  sys::ComponentContext* context_ptr = context.get();

  auto validated_clock = std::make_unique<FuchsiaSystemClock>(context_ptr->svc());

  auto cobalt_service = std::make_unique<CobaltService>(CreateCobaltConfig(
      dispatcher, kMetricsRegistryPath, configuration_data, validated_clock.get(),
      [context_ptr]() {
        fuchsia::net::http::LoaderSyncPtr loader_sync;
        context_ptr->svc()->Connect(loader_sync.NewRequest());
        return loader_sync;
      },
      target_interval, min_interval, initial_interval, event_aggregator_backfill_days,
      use_memory_observation_store, max_bytes_per_observation_store, product_name, board_name,
      version, std::make_unique<ActivityListenerImpl>(dispatcher, context->svc())));

  cobalt_service->SetDataCollectionPolicy(configuration_data.GetDataCollectionPolicy());

  return CobaltApp(std::move(context), dispatcher, std::move(cobalt_service),
                   std::move(validated_clock), start_event_aggregator_worker,
                   configuration_data.GetWatchForUserConsent());
}

CobaltApp::CobaltApp(std::unique_ptr<sys::ComponentContext> context, async_dispatcher_t* dispatcher,
                     std::unique_ptr<CobaltServiceInterface> cobalt_service,
                     std::unique_ptr<FuchsiaSystemClockInterface> validated_clock,
                     bool start_event_aggregator_worker, bool watch_for_user_consent)
    : context_(std::move(context)),
      cobalt_service_(std::move(cobalt_service)),
      validated_clock_(std::move(validated_clock)),
      timer_manager_(dispatcher) {
  auto current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  FX_LOGS(INFO) << "Waiting for the system clock to become accurate at: "
                << std::put_time(std::localtime(&current_time), "%F %T %z");
  validated_clock_->AwaitExternalSource([this, start_event_aggregator_worker]() {
    auto current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    FX_LOGS(INFO) << "The system clock has become accurate, now at: "
                  << std::put_time(std::localtime(&current_time), "%F %T %z");

    // Now that the clock is accurate, notify CobaltService.
    cobalt_service_->SystemClockIsAccurate(std::make_unique<util::SystemClock>(),
                                           start_event_aggregator_worker);
    controller_impl_->OnSystemClockBecomesAccurate();
  });

  // Create LoggerFactory protocol implementation and start serving it.
  logger_factory_impl_.reset(new LoggerFactoryImpl(&timer_manager_, cobalt_service_.get()));
  context_->outgoing()->AddPublicService(
      logger_factory_bindings_.GetHandler(logger_factory_impl_.get()));

  // Create MetricEventLoggerFactory protocol implementation and start serving it.
  metric_event_logger_factory_impl_ =
      std::make_unique<MetricEventLoggerFactoryImpl>(cobalt_service_.get());
  context_->outgoing()->AddPublicService(
      metric_event_logger_factory_bindings_.GetHandler(metric_event_logger_factory_impl_.get()));

  // Create SystemDataUpdater protocol implementation and start serving it.
  system_data_updater_impl_.reset(
      new SystemDataUpdaterImpl(cobalt_service_->system_data(), kSystemDataCachePrefix));
  context_->outgoing()->AddPublicService(
      system_data_updater_bindings_.GetHandler(system_data_updater_impl_.get()));

  if (watch_for_user_consent) {
    user_consent_watcher_ = std::make_unique<UserConsentWatcher>(
        dispatcher, context_->svc(), [this](const CobaltService::DataCollectionPolicy& new_policy) {
          cobalt_service_->SetDataCollectionPolicy(new_policy);
        });

    user_consent_watcher_->StartWatching();
  }

  // Create Controller protocol implementation and start serving it.
  controller_impl_ = std::make_unique<CobaltControllerImpl>(dispatcher, cobalt_service_.get());
  context_->outgoing()->AddPublicService(controller_bindings_.GetHandler(controller_impl_.get()));
}

ClientSecret CobaltApp::getClientSecret() {
  // TODO(rudominer): Generate a client secret only once, store it
  // persistently and reuse it in future instances.
  return ClientSecret::GenerateNewSecret();
}

}  // namespace cobalt
