// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/app/cobalt_app.h"

#include "lib/backoff/exponential_backoff.h"
#include "src/cobalt/bin/app/utils.h"
#include "src/cobalt/bin/utils/fuchsia_http_client.h"
#include "third_party/cobalt/encoder/file_observation_store.h"
#include "third_party/cobalt/encoder/memory_observation_store.h"
#include "third_party/cobalt/encoder/upload_scheduler.h"
#include "third_party/cobalt/util/posix_file_system.h"

namespace cobalt {

namespace http = ::fuchsia::net::oldhttp;

using clearcut::ClearcutUploader;
using encoder::ClearcutV1ShippingManager;
using encoder::ClientSecret;
using encoder::FileObservationStore;
using encoder::LegacyShippingManager;
using encoder::MemoryObservationStore;
using encoder::ObservationStore;
using encoder::ShippingManager;
using encoder::UploadScheduler;
using util::PosixFileSystem;
using utils::FuchsiaHTTPClient;

// Each "send attempt" is actually a cycle of potential retries. These
// two parameters configure the SendRetryer.
const std::chrono::seconds kInitialRpcDeadline(10);
const std::chrono::seconds kDeadlinePerSendAttempt(60);

const size_t kMaxBytesPerEnvelope = 512 * 1024;  // 0.5 MiB.

constexpr char kCloudShufflerUri[] = "shuffler.cobalt-api.fuchsia.com:443";
constexpr char kClearcutEndpoint[] = "https://jmt17.google.com/log";

constexpr char kAnalyzerPublicKeyPemPath[] =
    "/pkg/data/certs/cobaltv0.1/analyzer_public.pem";
constexpr char kShufflerPublicKeyPemPath[] =
    "/pkg/data/certs/cobaltv0.1/shuffler_public.pem";
constexpr char kAnalyzerTinkPublicKeyPath[] = "/pkg/data/keys/analyzer_public";
constexpr char kShufflerTinkPublicKeyPath[] = "/pkg/data/keys/shuffler_public";
constexpr char kMetricsRegistryPath[] = "/pkg/data/global_metrics_registry.pb";

constexpr char kLegacyObservationStorePath[] = "/data/legacy_observation_store";
constexpr char kObservationStorePath[] = "/data/observation_store";
constexpr char kLocalAggregateProtoStorePath[] = "/data/local_aggregate_store";
constexpr char kObsHistoryProtoStorePath[] = "/data/obs_history_store";

namespace {

std::unique_ptr<ObservationStore> NewObservationStore(
    size_t max_bytes_per_event, size_t max_bytes_per_envelope,
    size_t max_bytes_total, std::string root_directory, std::string name_prefix,
    bool use_memory_observation_store) {
  std::unique_ptr<ObservationStore> store;
  if (use_memory_observation_store) {
    store.reset(new MemoryObservationStore(
        max_bytes_per_event, max_bytes_per_envelope, max_bytes_total));
  } else {
    store.reset(new FileObservationStore(
        max_bytes_per_event, max_bytes_per_envelope, max_bytes_total,
        std::make_unique<PosixFileSystem>(), std::move(root_directory),
        name_prefix + " FileObservationStore"));
  }

  return store;
}

}  // namespace

CobaltApp::CobaltApp(
    async_dispatcher_t* dispatcher, std::chrono::seconds target_interval,
    std::chrono::seconds min_interval, std::chrono::seconds initial_interval,
    size_t event_aggregator_backfill_days, bool start_event_aggregator_worker,
    bool use_memory_observation_store, size_t max_bytes_per_observation_store,
    const std::string& product_name, const std::string& board_name,
    const std::string& version)
    : system_data_(product_name, board_name, version),
      context_(sys::ComponentContext::Create()),
      shuffler_client_(kCloudShufflerUri, true),
      send_retryer_(&shuffler_client_),
      network_wrapper_(
          dispatcher, std::make_unique<backoff::ExponentialBackoff>(),
          [this] { return context_->svc()->Connect<http::HttpService>(); }),
      // NOTE: Currently all observations are immediate observations and so it
      // makes sense to use MAX_BYTES_PER_EVENT as the value of
      // max_bytes_per_observation. But when we start implementing non-immediate
      // observations this needs to be revisited.
      // TODO(pesk): Observations for UniqueActives reports are of comparable
      // size to the events logged for them, so no change is needed now. Update
      // this comment as we add more non-immediate report types.
      legacy_observation_store_(NewObservationStore(
          fuchsia::cobalt::MAX_BYTES_PER_EVENT, kMaxBytesPerEnvelope,
          max_bytes_per_observation_store, kLegacyObservationStorePath,
          "Legacy", use_memory_observation_store)),
      observation_store_(NewObservationStore(
          fuchsia::cobalt::MAX_BYTES_PER_EVENT, kMaxBytesPerEnvelope,
          max_bytes_per_observation_store, kObservationStorePath, "V1",
          use_memory_observation_store)),
      legacy_encrypt_to_analyzer_(
          util::EncryptedMessageMaker::MakeHybridEcdh(
              ReadPublicKeyPem(kAnalyzerPublicKeyPemPath))
              .ValueOrDie()),
      legacy_encrypt_to_shuffler_(
          util::EncryptedMessageMaker::MakeHybridEcdh(
              ReadPublicKeyPem(kShufflerPublicKeyPemPath))
              .ValueOrDie()),
      encrypt_to_analyzer_(
          util::EncryptedMessageMaker::MakeHybridTinkForObservations(
              ReadPublicKeyPem(kAnalyzerTinkPublicKeyPath))
              .ValueOrDie()),
      encrypt_to_shuffler_(
          util::EncryptedMessageMaker::MakeHybridTinkForEnvelopes(
              ReadPublicKeyPem(kShufflerTinkPublicKeyPath))
              .ValueOrDie()),

      legacy_shipping_manager_(
          UploadScheduler(target_interval, min_interval, initial_interval),
          legacy_observation_store_.get(), legacy_encrypt_to_shuffler_.get(),
          LegacyShippingManager::SendRetryerParams(kInitialRpcDeadline,
                                                   kDeadlinePerSendAttempt),
          &send_retryer_),

      clearcut_shipping_manager_(
          UploadScheduler(target_interval, min_interval, initial_interval),
          observation_store_.get(), encrypt_to_shuffler_.get(),
          std::make_unique<clearcut::ClearcutUploader>(
              kClearcutEndpoint, std::make_unique<FuchsiaHTTPClient>(
                                     &network_wrapper_, dispatcher))),
      timer_manager_(dispatcher),
      local_aggregate_proto_store_(kLocalAggregateProtoStorePath,
                                   std::make_unique<PosixFileSystem>()),
      obs_history_proto_store_(kObsHistoryProtoStorePath,
                               std::make_unique<PosixFileSystem>()),
      logger_encoder_(getClientSecret(), &system_data_),
      observation_writer_(observation_store_.get(), &clearcut_shipping_manager_,
                          encrypt_to_analyzer_.get()),
      // Construct an EventAggregator using default values for the snapshot
      // intervals.
      event_aggregator_(
          &logger_encoder_, &observation_writer_, &local_aggregate_proto_store_,
          &obs_history_proto_store_, event_aggregator_backfill_days),
      controller_impl_(new CobaltControllerImpl(
          dispatcher,
          {&legacy_shipping_manager_, &clearcut_shipping_manager_})) {
  legacy_shipping_manager_.Start();
  clearcut_shipping_manager_.Start();
  if (start_event_aggregator_worker) {
    event_aggregator_.Start();
  }

  // Load the global metrics registry.
  std::ifstream registry_file_stream;
  registry_file_stream.open(kMetricsRegistryPath);
  FXL_CHECK(registry_file_stream && registry_file_stream.good())
      << "Could not open the Cobalt global metrics registry: "
      << kMetricsRegistryPath;
  std::string global_metrics_registry_bytes;
  global_metrics_registry_bytes.assign(
      (std::istreambuf_iterator<char>(registry_file_stream)),
      std::istreambuf_iterator<char>());
  FXL_CHECK(!global_metrics_registry_bytes.empty())
      << "Could not read the Cobalt global metrics registry: "
      << kMetricsRegistryPath;

  logger_factory_impl_.reset(new LoggerFactoryImpl(
      global_metrics_registry_bytes, getClientSecret(),
      legacy_observation_store_.get(), legacy_encrypt_to_analyzer_.get(),
      &legacy_shipping_manager_, &system_data_, &timer_manager_,
      &logger_encoder_, &observation_writer_, &event_aggregator_));

  context_->outgoing()->AddPublicService(
      logger_factory_bindings_.GetHandler(logger_factory_impl_.get()));

  system_data_updater_impl_.reset(new SystemDataUpdaterImpl(&system_data_));
  context_->outgoing()->AddPublicService(
      system_data_updater_bindings_.GetHandler(
          system_data_updater_impl_.get()));

  context_->outgoing()->AddPublicService(
      controller_bindings_.GetHandler(controller_impl_.get()));
}

ClientSecret CobaltApp::getClientSecret() {
  // TODO(rudominer): Generate a client secret only once, store it
  // persistently and reuse it in future instances.
  return ClientSecret::GenerateNewSecret();
}
}  // namespace cobalt
