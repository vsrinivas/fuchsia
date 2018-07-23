// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/cobalt_app.h"

#include "garnet/bin/cobalt/app/utils.h"
#include "garnet/bin/cobalt/utils/fuchsia_http_client.h"
#include "lib/backoff/exponential_backoff.h"
#include "third_party/cobalt/encoder/memory_observation_store.h"

namespace cobalt {

namespace http = ::fuchsia::net::oldhttp;

using clearcut::ClearcutUploader;
using config::ClientConfig;
using encoder::ClearcutV1ShippingManager;
using encoder::ClientSecret;
using encoder::CobaltEncoderFactoryImpl;
using encoder::LegacyShippingManager;
using encoder::MemoryObservationStore;
using encoder::ShippingManager;
using utils::FuchsiaHTTPClient;

// Each "send attempt" is actually a cycle of potential retries. These
// two parameters configure the SendRetryer.
const std::chrono::seconds kInitialRpcDeadline(10);
const std::chrono::seconds kDeadlinePerSendAttempt(60);

const size_t kMaxBytesPerEnvelope = 512 * 1024;  // 0.5 MiB.
const size_t kMaxBytesTotal = 1024 * 1024;       // 1 MiB
const size_t kMinEnvelopeSendSize = 10 * 1024;   // 10 K

constexpr char kCloudShufflerUri[] = "shuffler.cobalt-api.fuchsia.com:443";
const char kClearcutServerUri[] = "https://jmt17.google.com/log";

constexpr char kConfigBinProtoPath[] = "/pkg/data/cobalt_config.binproto";
constexpr char kAnalyzerPublicKeyPemPath[] =
    "/pkg/data/certs/cobaltv0.1/analyzer_public.pem";
constexpr char kShufflerPublicKeyPemPath[] =
    "/pkg/data/certs/cobaltv0.1/shuffler_public.pem";

CobaltApp::CobaltApp(async_dispatcher_t* dispatcher,
                     std::chrono::seconds schedule_interval,
                     std::chrono::seconds min_interval,
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
      encrypt_to_analyzer_(ReadPublicKeyPem(kAnalyzerPublicKeyPemPath),
                           EncryptedMessage::HYBRID_ECDH_V1),
      encrypt_to_shuffler_(ReadPublicKeyPem(kShufflerPublicKeyPemPath),
                           EncryptedMessage::HYBRID_ECDH_V1),
      timer_manager_(dispatcher),
      controller_impl_(
          new CobaltControllerImpl(dispatcher, &shipping_dispatcher_)) {
  store_dispatcher_.Register(
      ObservationMetadata::LEGACY_BACKEND,
      std::make_unique<MemoryObservationStore>(
          fuchsia::cobalt::kMaxBytesPerObservation, kMaxBytesPerEnvelope,
          kMaxBytesTotal, kMinEnvelopeSendSize));
  store_dispatcher_.Register(
      ObservationMetadata::V1_BACKEND,
      std::make_unique<MemoryObservationStore>(
          fuchsia::cobalt::kMaxBytesPerObservation, kMaxBytesPerEnvelope,
          kMaxBytesTotal, kMinEnvelopeSendSize));

  auto schedule_params =
      ShippingManager::ScheduleParams(schedule_interval, min_interval);
  shipping_dispatcher_.Register(
      ObservationMetadata::LEGACY_BACKEND,
      std::make_unique<LegacyShippingManager>(
          schedule_params,
          store_dispatcher_.GetStore(ObservationMetadata::LEGACY_BACKEND)
              .ConsumeValueOrDie(),
          &encrypt_to_shuffler_,
          LegacyShippingManager::SendRetryerParams(kInitialRpcDeadline,
                                                   kDeadlinePerSendAttempt),
          &send_retryer_));
  shipping_dispatcher_.Register(
      ObservationMetadata::V1_BACKEND,
      std::make_unique<ClearcutV1ShippingManager>(
          schedule_params,
          store_dispatcher_.GetStore(ObservationMetadata::V1_BACKEND)
              .ConsumeValueOrDie(),
          &encrypt_to_shuffler_,
          std::make_unique<ClearcutUploader>(
              kClearcutServerUri, std::make_unique<FuchsiaHTTPClient>(
                                      &network_wrapper_, dispatcher))));
  shipping_dispatcher_.Start();

  // Open the cobalt config file.
  std::ifstream config_file_stream;
  config_file_stream.open(kConfigBinProtoPath);
  FXL_CHECK(config_file_stream && config_file_stream.good())
      << "Could not open the Cobalt config file: " << kConfigBinProtoPath;
  std::string cobalt_config_bytes;
  cobalt_config_bytes.assign(
      (std::istreambuf_iterator<char>(config_file_stream)),
      std::istreambuf_iterator<char>());
  FXL_CHECK(!cobalt_config_bytes.empty())
      << "Could not read the Cobalt config file: " << kConfigBinProtoPath;

  // Parse the data as a CobaltConfig, then extract the metric and encoding
  // configs and construct a ClientConfig to house them.
  client_config_.reset(
      ClientConfig::CreateFromCobaltConfigBytes(cobalt_config_bytes).release());
  FXL_CHECK(client_config_)
      << "Could not parse the Cobalt config file: " << kConfigBinProtoPath;

  factory_impl_.reset(new CobaltEncoderFactoryImpl(
      client_config_, getClientSecret(), &store_dispatcher_,
      &encrypt_to_analyzer_, &shipping_dispatcher_, &system_data_,
      &timer_manager_));

  context_->outgoing().AddPublicService(
      factory_bindings_.GetHandler(factory_impl_.get()));

  context_->outgoing().AddPublicService(
      controller_bindings_.GetHandler(controller_impl_.get()));
}

ClientSecret CobaltApp::getClientSecret() {
  // TODO(rudominer): Generate a client secret only once, store it
  // persistently and reuse it in future instances.
  return ClientSecret::GenerateNewSecret();
}
}  // namespace cobalt
