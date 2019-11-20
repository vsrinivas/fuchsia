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
#include "third_party/cobalt/src/logger/internal_metrics_config.cb.h"
#include "third_party/cobalt/src/observation_store/file_observation_store.h"
#include "third_party/cobalt/src/observation_store/memory_observation_store.h"
#include "third_party/cobalt/src/uploader/upload_scheduler.h"

namespace cobalt {

namespace http = ::fuchsia::net::oldhttp;

using encoder::ClearcutV1ShippingManager;
using encoder::ClientSecret;
using encoder::ShippingManager;
using encoder::UploadScheduler;
using lib::clearcut::ClearcutUploader;
using logger::ProjectContextFactory;
using observation_store::FileObservationStore;
using observation_store::MemoryObservationStore;
using observation_store::ObservationStore;
using util::PosixFileSystem;
using utils::FuchsiaHTTPClient;

const size_t kMaxBytesPerEnvelope = 512 * 1024;  // 0.5 MiB.

constexpr char kClearcutEndpoint[] = "https://play.googleapis.com/staging/log";

constexpr char kMetricsRegistryPath[] = "/pkg/data/global_metrics_registry.pb";

constexpr char kObservationStorePath[] = "/data/observation_store";
constexpr char kLocalAggregateProtoStorePath[] = "/data/local_aggregate_store";
constexpr char kObsHistoryProtoStorePath[] = "/data/obs_history_store";
constexpr char kSystemDataCachePrefix[] = "/data/system_data_";
constexpr char kLocalLogFilePath[] = "/data/cobalt_observations.pb";

const size_t kClearcutMaxRetries = 5;

namespace {
std::unique_ptr<ObservationStore> NewObservationStore(
    size_t max_bytes_per_event, size_t max_bytes_per_envelope, size_t max_bytes_total,
    std::string root_directory, const std::string& name_prefix, bool use_memory_observation_store) {
  std::unique_ptr<ObservationStore> store;
  if (use_memory_observation_store) {
    store.reset(
        new MemoryObservationStore(max_bytes_per_event, max_bytes_per_envelope, max_bytes_total));
  } else {
    store.reset(new FileObservationStore(max_bytes_per_event, max_bytes_per_envelope,
                                         max_bytes_total, std::make_unique<PosixFileSystem>(),
                                         std::move(root_directory),
                                         name_prefix + " FileObservationStore"));
  }

  return store;
}

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
    : system_data_(product_name, board_name, configuration_data_.GetReleaseStage(), version),
      context_(std::move(context)),
      system_clock_(FuchsiaSystemClock(context_.get())),
      network_wrapper_(dispatcher, std::make_unique<backoff::ExponentialBackoff>(),
                       [this] { return context_->svc()->Connect<http::HttpService>(); }),
      // TODO(pesk): Observations for UniqueActives reports are of comparable
      // size to the events logged for them, so it makes sense to use
      // MAX_BYTES_PER_EVENT as the value of max_bytes_per_observation.
      // Revisit this as we add more non-immediate report types.
      observation_store_(NewObservationStore(fuchsia::cobalt::MAX_BYTES_PER_EVENT,
                                             kMaxBytesPerEnvelope, max_bytes_per_observation_store,
                                             kObservationStorePath, "V1",
                                             use_memory_observation_store)),
      timer_manager_(dispatcher),
      local_aggregate_proto_store_(kLocalAggregateProtoStorePath,
                                   std::make_unique<PosixFileSystem>()),
      obs_history_proto_store_(kObsHistoryProtoStorePath, std::make_unique<PosixFileSystem>()),
      logger_encoder_(getClientSecret(), &system_data_) {
  auto global_project_context_factory =
      std::make_shared<ProjectContextFactory>(ReadGlobalMetricsRegistryBytes(kMetricsRegistryPath));

  auto envs = configuration_data_.GetBackendEnvironments();
  encoder::ClearcutV1ShippingManager* clearcut_shipping_manager = nullptr;
  if (std::find(envs.begin(), envs.end(), config::Environment::LOCAL) != envs.end()) {
    FXL_CHECK(envs.size() == 1) << "Only one backend environment is supported if one is LOCAL.";
    encrypt_to_analyzer_ = util::EncryptedMessageMaker::MakeUnencrypted();
    FX_LOGS(INFO) << "Writing the Cobalt observations to: " << kLocalLogFilePath;
    shipping_manager_ = std::make_unique<encoder::LocalShippingManager>(
        observation_store_.get(), kLocalLogFilePath, std::make_unique<PosixFileSystem>());
  } else {
    encrypt_to_analyzer_ = util::EncryptedMessageMaker::MakeForObservations(
                               ReadPublicKeyPem(configuration_data_.AnalyzerPublicKeyPath()))
                               .ValueOrDie();
    clearcut_shipping_manager = new encoder::ClearcutV1ShippingManager(
        UploadScheduler(target_interval, min_interval, initial_interval), observation_store_.get(),
        encrypt_to_analyzer_.get(),
        std::make_unique<ClearcutUploader>(
            kClearcutEndpoint, std::make_unique<FuchsiaHTTPClient>(&network_wrapper_, dispatcher)),
        nullptr, kClearcutMaxRetries, configuration_data_.GetApiKey());
    shipping_manager_ = std::unique_ptr<encoder::ShippingManager>(clearcut_shipping_manager);
    // TODO(camrdale): remove this once the log source transition is complete.
    for (const auto& backend_environment : configuration_data_.GetBackendEnvironments()) {
      auto encrypt_to_shuffler =
          util::EncryptedMessageMaker::MakeForEnvelopes(
              ReadPublicKeyPem(configuration_data_.ShufflerPublicKeyPath(backend_environment)))
              .ValueOrDie();
      clearcut_shipping_manager->AddClearcutDestination(
          encrypt_to_shuffler.get(), configuration_data_.GetLogSourceId(backend_environment));
      encrypt_to_shufflers_.emplace_back(std::move(encrypt_to_shuffler));
    }
  }
  observation_writer_ = std::make_unique<logger::ObservationWriter>(
      observation_store_.get(), shipping_manager_.get(), encrypt_to_analyzer_.get());

  // Construct an EventAggregatorManager using default values for the snapshot
  // intervals.
  event_aggregator_manager_ = std::make_unique<local_aggregation::EventAggregatorManager>(
      &logger_encoder_, observation_writer_.get(), &local_aggregate_proto_store_,
      &obs_history_proto_store_, event_aggregator_backfill_days);

  controller_impl_ = std::make_unique<CobaltControllerImpl>(
      dispatcher, shipping_manager_.get(), event_aggregator_manager_->GetEventAggregator(),
      observation_store_.get());

  undated_event_manager_ = std::make_shared<logger::UndatedEventManager>(
      &logger_encoder_, event_aggregator_manager_->GetEventAggregator(), observation_writer_.get(),
      &system_data_);

  // Create internal Logger and pass a pointer to objects which use it.
  internal_logger_ = NewInternalLogger(global_project_context_factory, logger::kCustomerName,
                                       logger::kProjectName, configuration_data_.GetReleaseStage());

  observation_store_->ResetInternalMetrics(internal_logger_.get());
  if (clearcut_shipping_manager != nullptr) {
    clearcut_shipping_manager->ResetInternalMetrics(internal_logger_.get());
  }

  auto current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  FX_LOGS(INFO) << "Waiting for the system clock to become accurate at: "
                << std::put_time(std::localtime(&current_time), "%F %T %z");
  system_clock_.AwaitExternalSource([this, start_event_aggregator_worker]() {
    auto current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    FX_LOGS(INFO) << "The system clock has become accurate, now at: "
                  << std::put_time(std::localtime(&current_time), "%F %T %z");

    auto system_clock = std::make_unique<util::SystemClock>();
    undated_event_manager_->Flush(system_clock.get(), internal_logger_.get());
    undated_event_manager_.reset();

    // Now that the clock is accurate, start workers that need an accurate clock.
    if (start_event_aggregator_worker) {
      event_aggregator_manager_->Start(std::move(system_clock));
    }
  });

  // Start workers.
  shipping_manager_->Start();

  // Create LoggerFactory.
  logger_factory_impl_.reset(new LoggerFactoryImpl(
      std::move(global_project_context_factory), getClientSecret(), &timer_manager_,
      &logger_encoder_, observation_writer_.get(), event_aggregator_manager_->GetEventAggregator(),
      &system_clock_, undated_event_manager_, internal_logger_.get(), &system_data_));

  context_->outgoing()->AddPublicService(
      logger_factory_bindings_.GetHandler(logger_factory_impl_.get()));

  // Create SystemDataUpdater
  system_data_updater_impl_.reset(new SystemDataUpdaterImpl(&system_data_, kSystemDataCachePrefix));
  context_->outgoing()->AddPublicService(
      system_data_updater_bindings_.GetHandler(system_data_updater_impl_.get()));

  // Add other bindings.
  context_->outgoing()->AddPublicService(controller_bindings_.GetHandler(controller_impl_.get()));
}

ClientSecret CobaltApp::getClientSecret() {
  // TODO(rudominer): Generate a client secret only once, store it
  // persistently and reuse it in future instances.
  return ClientSecret::GenerateNewSecret();
}

std::unique_ptr<logger::Logger> CobaltApp::NewInternalLogger(
    const std::shared_ptr<logger::ProjectContextFactory> global_project_context_factory,
    const std::string& customer_name, const std::string& project_name, ReleaseStage release_stage) {
  auto internal_project_context = global_project_context_factory->NewProjectContext(
      logger::kCustomerName, logger::kProjectName, release_stage);
  if (!internal_project_context) {
    FX_LOGS(ERROR) << "The CobaltRegistry bundled with Cobalt does not "
                      "include the expected internal metrics project. "
                      "Cobalt-measuring-Cobalt will be disabled.";
  }
  return std::make_unique<logger::Logger>(std::move(internal_project_context), &logger_encoder_,
                                          event_aggregator_manager_->GetEventAggregator(),
                                          observation_writer_.get(), &system_data_);
}

}  // namespace cobalt
