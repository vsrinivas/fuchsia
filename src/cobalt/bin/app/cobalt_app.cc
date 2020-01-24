// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/cobalt_app.h"

#include <memory>

#include "lib/sys/cpp/component_context.h"
#include "src/cobalt/bin/app/logger_impl.h"
#include "src/cobalt/bin/app/utils.h"
#include "src/cobalt/bin/utils/fuchsia_http_client.h"
#include "src/lib/backoff/exponential_backoff.h"
#include "third_party/cobalt/src/lib/util/posix_file_system.h"

namespace cobalt {

namespace http = ::fuchsia::net::oldhttp;

using encoder::ClientSecret;
using logger::ProjectContextFactory;
using util::PosixFileSystem;
using utils::FuchsiaHTTPClient;

const size_t kMaxBytesPerEnvelope = 512 * 1024;  // 0.5 MiB.

constexpr char kMetricsRegistryPath[] = "/config/data/global_metrics_registry.pb";

constexpr char kObservationStorePath[] = "/data/observation_store";
constexpr char kLocalAggregateProtoStorePath[] = "/data/local_aggregate_store";
constexpr char kObsHistoryProtoStorePath[] = "/data/obs_history_store";
constexpr char kSystemDataCachePrefix[] = "/data/system_data_";
constexpr char kLocalLogFilePath[] = "/data/cobalt_observations.pb";

const size_t kClearcutMaxRetries = 5;

namespace {

std::string ReadGlobalMetricsRegistryBytes(const std::string& global_metrics_registry_path) {
  std::ifstream registry_file_stream;
  registry_file_stream.open(global_metrics_registry_path);
  FXL_CHECK(registry_file_stream && registry_file_stream.good())
      << "Could not open the Cobalt global metrics registry: " << global_metrics_registry_path;
  std::string global_metrics_registry_bytes;
  global_metrics_registry_bytes.assign((std::istreambuf_iterator<char>(registry_file_stream)),
                                       std::istreambuf_iterator<char>());
  FXL_CHECK(!global_metrics_registry_bytes.empty())
      << "Could not read the Cobalt global metrics registry: " << global_metrics_registry_path;
  return global_metrics_registry_bytes;
}

}  // namespace

CobaltApp::CobaltApp(std::unique_ptr<sys::ComponentContext> context, async_dispatcher_t* dispatcher,
                     std::chrono::seconds target_interval, std::chrono::seconds min_interval,
                     std::chrono::seconds initial_interval, size_t event_aggregator_backfill_days,
                     bool start_event_aggregator_worker, bool use_memory_observation_store,
                     size_t max_bytes_per_observation_store, const std::string& product_name,
                     const std::string& board_name, const std::string& version)
    : context_(std::move(context)),
      system_clock_(FuchsiaSystemClock(context_.get())),
      network_wrapper_(dispatcher, std::make_unique<backoff::ExponentialBackoff>(),
                       [this] { return context_->svc()->Connect<http::HttpService>(); }),
      timer_manager_(dispatcher) {
  auto global_project_context_factory =
      std::make_shared<ProjectContextFactory>(ReadGlobalMetricsRegistryBytes(kMetricsRegistryPath));

  // |target_pipeline| is the pipeline used for sending data to cobalt. In particular, it is the
  // source of the encryption keys, as well as determining the destination for generated
  // observations (either clearcut, or the local filesystem).
  std::unique_ptr<TargetPipelineInterface> target_pipeline;
  const auto& backend_environment = configuration_data_.GetBackendEnvironment();
  if (backend_environment == system_data::Environment::LOCAL) {
    target_pipeline = std::make_unique<LocalPipeline>();
  } else {
    target_pipeline = std::make_unique<TargetPipeline>(
        backend_environment, ReadPublicKeyPem(configuration_data_.ShufflerPublicKeyPath()),
        ReadPublicKeyPem(configuration_data_.AnalyzerPublicKeyPath()),
        std::make_unique<FuchsiaHTTPClient>(&network_wrapper_, dispatcher), kClearcutMaxRetries);
  }

  auto internal_project_context = global_project_context_factory->NewProjectContext(
      logger::kCustomerName, logger::kProjectName);
  if (!internal_project_context) {
    FX_LOGS(ERROR) << "The CobaltRegistry bundled with Cobalt does not "
                      "include the expected internal metrics project. "
                      "Cobalt-measuring-Cobalt will be disabled.";
  }

  CobaltConfig cfg = {
      .product_name = product_name,
      .board_name_suggestion = board_name,
      .version = version,
      .release_stage = configuration_data_.GetReleaseStage(),

      .file_system = std::make_unique<PosixFileSystem>(),
      .use_memory_observation_store = use_memory_observation_store,
      .max_bytes_per_event = fuchsia::cobalt::MAX_BYTES_PER_EVENT,
      .max_bytes_per_envelope = kMaxBytesPerEnvelope,
      .max_bytes_total = max_bytes_per_observation_store,
      .observation_store_directory = kObservationStorePath,

      .local_aggregate_proto_store_path = kLocalAggregateProtoStorePath,
      .obs_history_proto_store_path = kObsHistoryProtoStorePath,

      .target_interval = target_interval,
      .min_interval = min_interval,
      .initial_interval = initial_interval,

      .target_pipeline = std::move(target_pipeline),

      .local_shipping_manager_path = kLocalLogFilePath,

      .api_key = configuration_data_.GetApiKey(),
      .client_secret = getClientSecret(),
      .internal_logger_project_context = std::move(internal_project_context),

      .local_aggregation_backfill_days = event_aggregator_backfill_days,

      .validated_clock = &system_clock_,
  };

  cobalt_service_ = std::make_unique<CobaltService>(std::move(cfg));

  auto current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  FX_LOGS(INFO) << "Waiting for the system clock to become accurate at: "
                << std::put_time(std::localtime(&current_time), "%F %T %z");
  system_clock_.AwaitExternalSource([this, start_event_aggregator_worker]() {
    auto current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    FX_LOGS(INFO) << "The system clock has become accurate, now at: "
                  << std::put_time(std::localtime(&current_time), "%F %T %z");

    auto system_clock = std::make_unique<util::SystemClock>();

    // Now that the clock is accurate, notify CobaltService.
    cobalt_service_->SystemClockIsAccurate(std::move(system_clock), start_event_aggregator_worker);
  });

  // Create LoggerFactory.
  logger_factory_impl_.reset(new LoggerFactoryImpl(std::move(global_project_context_factory),
                                                   &timer_manager_, cobalt_service_.get()));

  context_->outgoing()->AddPublicService(
      logger_factory_bindings_.GetHandler(logger_factory_impl_.get()));

  // Create SystemDataUpdater
  system_data_updater_impl_.reset(
      new SystemDataUpdaterImpl(cobalt_service_->system_data(), kSystemDataCachePrefix));
  context_->outgoing()->AddPublicService(
      system_data_updater_bindings_.GetHandler(system_data_updater_impl_.get()));

  controller_impl_ = std::make_unique<CobaltControllerImpl>(dispatcher, cobalt_service_.get());

  // Add other bindings.
  context_->outgoing()->AddPublicService(controller_bindings_.GetHandler(controller_impl_.get()));
}

ClientSecret CobaltApp::getClientSecret() {
  // TODO(rudominer): Generate a client secret only once, store it
  // persistently and reuse it in future instances.
  return ClientSecret::GenerateNewSecret();
}

}  // namespace cobalt
