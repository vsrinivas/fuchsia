// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/cobalt_app.h"

#include "garnet/bin/cobalt/app/utils.h"
#include "garnet/bin/cobalt/utils/fuchsia_http_client.h"
#include "lib/backoff/exponential_backoff.h"
#include "third_party/cobalt/encoder/file_observation_store.h"
#include "third_party/cobalt/encoder/upload_scheduler.h"
#include "third_party/cobalt/util/posix_file_system.h"

namespace cobalt {

namespace http = ::fuchsia::net::oldhttp;

using clearcut::ClearcutUploader;
using config::ClientConfig;
using config::ProjectConfigs;
using encoder::ClearcutV1ShippingManager;
using encoder::ClientSecret;
using encoder::FileObservationStore;
using encoder::LegacyShippingManager;
using encoder::ShippingManager;
using encoder::UploadScheduler;
using util::PosixFileSystem;
using utils::FuchsiaHTTPClient;

// Each "send attempt" is actually a cycle of potential retries. These
// two parameters configure the SendRetryer.
const std::chrono::seconds kInitialRpcDeadline(10);
const std::chrono::seconds kDeadlinePerSendAttempt(60);

const size_t kMaxBytesPerEnvelope = 512 * 1024;  // 0.5 MiB.
const size_t kMaxBytesTotal = 1024 * 1024;       // 1 MiB

constexpr char kCloudShufflerUri[] = "shuffler.cobalt-api.fuchsia.com:443";
constexpr char kClearcutEndpoint[] = "https://jmt17.google.com/log";

constexpr char kAnalyzerPublicKeyPemPath[] =
    "/pkg/data/certs/cobaltv0.1/analyzer_public.pem";
constexpr char kShufflerPublicKeyPemPath[] =
    "/pkg/data/certs/cobaltv0.1/shuffler_public.pem";
constexpr char kMetricsRegistryPath[] = "/pkg/data/global_metrics_registry.pb";

constexpr char kLegacyObservationStorePath[] =
    "/data/cobalt_legacy_observation_store";

constexpr char kObservationStorePath[] = "/data/cobalt_observation_store";

constexpr char kLocalAggregateProtoStorePath[] =
    "/data/cobalt_local_aggregate_store";
constexpr char kObsHistoryProtoStorePath[] = "/data/cobalt_obs_history_store";

CobaltApp::CobaltApp(async_dispatcher_t* dispatcher,
                     std::chrono::seconds schedule_interval,
                     std::chrono::seconds min_interval,
                     std::chrono::seconds initial_interval,
                     const std::string& product_name)
    : system_data_(product_name),
      context_(component::StartupContext::CreateFromStartupInfo()),
      shuffler_client_(kCloudShufflerUri, true),
      send_retryer_(&shuffler_client_),
      network_wrapper_(
          dispatcher, std::make_unique<backoff::ExponentialBackoff>(),
          [this] {
            return context_->ConnectToEnvironmentService<http::HttpService>();
          }),
      // NOTE: Currently all observations are immediate observations and so it
      // makes sense to use MAX_BYTES_PER_EVENT as the value of
      // max_bytes_per_observation. But when we start implementing non-immediate
      // observations this needs to be revisited.
      // TODO(pesk): Observations for UniqueActives reports are of comparable
      // to the events logged for them, so no change is needed now. Update this
      // comment as we add more non-immediate report types.
      legacy_observation_store_(
          fuchsia::cobalt::MAX_BYTES_PER_EVENT, kMaxBytesPerEnvelope,
          kMaxBytesTotal, std::make_unique<PosixFileSystem>(),
          kLegacyObservationStorePath, "Legacy FileObservationStore"),
      observation_store_(fuchsia::cobalt::MAX_BYTES_PER_EVENT,
                         kMaxBytesPerEnvelope, kMaxBytesTotal,
                         std::make_unique<PosixFileSystem>(),
                         kObservationStorePath, "V1 FileObservationStore"),
      legacy_encrypt_to_analyzer_(ReadPublicKeyPem(kAnalyzerPublicKeyPemPath),
                                  EncryptedMessage::HYBRID_ECDH_V1),
      legacy_encrypt_to_shuffler_(ReadPublicKeyPem(kShufflerPublicKeyPemPath),
                                  EncryptedMessage::HYBRID_ECDH_V1),
      // TODO(rudominer,pesk) Support encryption in Cobalt 1.0.
      encrypt_to_analyzer_("", EncryptedMessage::NONE),
      encrypt_to_shuffler_("", EncryptedMessage::NONE),

      legacy_shipping_manager_(
          UploadScheduler(schedule_interval, min_interval, initial_interval),
          &legacy_observation_store_, &legacy_encrypt_to_shuffler_,
          LegacyShippingManager::SendRetryerParams(kInitialRpcDeadline,
                                                   kDeadlinePerSendAttempt),
          &send_retryer_),

      clearcut_shipping_manager_(
          UploadScheduler(schedule_interval, min_interval, initial_interval),
          &observation_store_, &encrypt_to_shuffler_,
          std::make_unique<clearcut::ClearcutUploader>(
              kClearcutEndpoint, std::make_unique<FuchsiaHTTPClient>(
                                     &network_wrapper_, dispatcher))),
      timer_manager_(dispatcher),
      local_aggregate_proto_store_(kLocalAggregateProtoStorePath,
                                   std::make_unique<PosixFileSystem>()),
      obs_history_proto_store_(kObsHistoryProtoStorePath,
                               std::make_unique<PosixFileSystem>()),
      logger_encoder_(getClientSecret(), &system_data_),
      observation_writer_(&observation_store_, &clearcut_shipping_manager_,
                          &encrypt_to_analyzer_),
      // Construct an EventAggregator using default values for the snapshot
      // intervals and the number of backfill days.
      // TODO(pesk): consider using non-default values for these arguments; in
      // particular, a non-zero number of backfill days.
      event_aggregator_(&logger_encoder_, &observation_writer_,
                        &local_aggregate_proto_store_,
                        &obs_history_proto_store_),
      controller_impl_(new CobaltControllerImpl(
          dispatcher,
          {&legacy_shipping_manager_, &clearcut_shipping_manager_})) {
  legacy_shipping_manager_.Start();
  clearcut_shipping_manager_.Start();
  event_aggregator_.Start();

  // Load the global metrics registry.
  std::ifstream registry_file_stream;
  registry_file_stream.open(kMetricsRegistryPath);
  FXL_CHECK(registry_file_stream && registry_file_stream.good())
      << "Could not open the Cobalt global metrics registry: "
      << kMetricsRegistryPath;
  std::string metrics_registry_bytes;
  metrics_registry_bytes.assign(
      (std::istreambuf_iterator<char>(registry_file_stream)),
      std::istreambuf_iterator<char>());
  FXL_CHECK(!metrics_registry_bytes.empty())
      << "Could not read the Cobalt global metrics registry: "
      << kMetricsRegistryPath;

  // Parse the data as a ClientConfig
  client_config_.reset(
      ClientConfig::CreateFromCobaltRegistryBytes(metrics_registry_bytes)
          .release());
  FXL_CHECK(client_config_)
      << "Could not parse the Cobalt global metrics registry: "
      << kMetricsRegistryPath;

  // Parse the data as a ProjectConfigs
  project_configs_.reset(
      ProjectConfigs::CreateFromCobaltRegistryBytes(metrics_registry_bytes)
          .release());
  FXL_CHECK(project_configs_)
      << "Could not parse the Cobalt global metrics registry: "
      << kMetricsRegistryPath;

  logger_factory_impl_.reset(new LoggerFactoryImpl(
      getClientSecret(), &legacy_observation_store_,
      &legacy_encrypt_to_analyzer_, &legacy_shipping_manager_, &system_data_,
      &timer_manager_, &logger_encoder_, &observation_writer_,
      &event_aggregator_, client_config_, project_configs_));

  context_->outgoing().AddPublicService(
      logger_factory_bindings_.GetHandler(logger_factory_impl_.get()));

  system_data_updater_impl_.reset(new SystemDataUpdaterImpl(&system_data_));
  context_->outgoing().AddPublicService(
      system_data_updater_bindings_.GetHandler(
          system_data_updater_impl_.get()));

  context_->outgoing().AddPublicService(
      controller_bindings_.GetHandler(controller_impl_.get()));
}

ClientSecret CobaltApp::getClientSecret() {
  // TODO(rudominer): Generate a client secret only once, store it
  // persistently and reuse it in future instances.
  return ClientSecret::GenerateNewSecret();
}
}  // namespace cobalt
