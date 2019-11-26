// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/fidl/cpp/fuzzing/server_provider.h>

#include <chrono>
#include <fstream>
#include <future>

#include "fuchsia/net/oldhttp/cpp/fidl.h"
#include "lib/async/default.h"
#include "lib/sys/cpp/component_context.h"
#include "src/cobalt/bin/app/logger_factory_impl.h"
#include "src/cobalt/bin/app/timer_manager.h"
#include "src/cobalt/bin/utils/fuchsia_http_client.h"
#include "third_party/cobalt/src/lib/clearcut/uploader.h"
#include "third_party/cobalt/src/lib/crypto_util/base64.h"
#include "third_party/cobalt/src/lib/util/posix_file_system.h"
#include "third_party/cobalt/src/local_aggregation/event_aggregator_mgr.h"
#include "third_party/cobalt/src/logger/observation_writer.h"
#include "third_party/cobalt/src/logger/project_context_factory.h"
#include "third_party/cobalt/src/observation_store/memory_observation_store.h"
#include "third_party/cobalt/src/observation_store/observation_store.h"
#include "third_party/cobalt/src/system_data/client_secret.h"
#include "third_party/cobalt/src/uploader/shipping_manager.h"
#include "third_party/cobalt/src/uploader/upload_scheduler.h"

// Source of cobalt::logger::kConfig
#include "third_party/cobalt/src/logger/internal_metrics_config.cb.h"

namespace {

::fidl::fuzzing::ServerProvider<::fuchsia::cobalt::LoggerFactory, ::cobalt::LoggerFactoryImpl>*
    fuzzer_server_provider;

auto secret = cobalt::encoder::ClientSecret::GenerateNewSecret();
cobalt::logger::Encoder encoder(secret, nullptr);
auto observation_store =
    std::make_unique<cobalt::observation_store::MemoryObservationStore>(100, 100, 1000);

class NoOpHTTPClient : public cobalt::lib::clearcut::HTTPClient {
  std::future<cobalt::lib::statusor::StatusOr<cobalt::lib::clearcut::HTTPResponse>> Post(
      cobalt::lib::clearcut::HTTPRequest request,
      std::chrono::steady_clock::time_point deadline) override {
    return std::async(std::launch::async,
                      []() mutable -> cobalt::lib::statusor::StatusOr<cobalt::lib::clearcut::HTTPResponse> {
                        return cobalt::util::Status::CANCELLED;
                      });
  }
};

std::unique_ptr<cobalt::util::EncryptedMessageMaker> encrypt_to_analyzer =
    cobalt::util::EncryptedMessageMaker::MakeUnencrypted();

cobalt::encoder::ClearcutV1ShippingManager clearcut_shipping_manager(
    cobalt::encoder::UploadScheduler(std::chrono::seconds(10), std::chrono::seconds(10)),
    observation_store.get(), encrypt_to_analyzer.get(),
    std::make_unique<cobalt::lib::clearcut::ClearcutUploader>("http://test.com",
                                                              std::make_unique<NoOpHTTPClient>()),
    nullptr, 5, "");

cobalt::logger::ObservationWriter observation_writer(observation_store.get(),
                                                     &clearcut_shipping_manager,
                                                     encrypt_to_analyzer.get());

cobalt::util::ConsistentProtoStore local_aggregate_proto_store(
    "/tmp/local_agg", std::make_unique<cobalt::util::PosixFileSystem>());
cobalt::util::ConsistentProtoStore obs_history_proto_store(
    "/tmp/obs_hist", std::make_unique<cobalt::util::PosixFileSystem>());

cobalt::local_aggregation::EventAggregatorManager event_aggregator_manager(
    &encoder, &observation_writer, &local_aggregate_proto_store, &obs_history_proto_store, 4);

auto undated_event_manager = std::make_shared<cobalt::logger::UndatedEventManager>(
    &encoder, event_aggregator_manager.GetEventAggregator(), &observation_writer, nullptr);

cobalt::TimerManager manager(nullptr);

}  // namespace

// See https://fuchsia.dev/fuchsia-src/development/workflows/libfuzzer_fidl for explanations and
// documentations for these functions.
extern "C" {
zx_status_t fuzzer_init() {
  if (fuzzer_server_provider == nullptr) {
    fuzzer_server_provider = new ::fidl::fuzzing::ServerProvider<::fuchsia::cobalt::LoggerFactory,
                                                                 ::cobalt::LoggerFactoryImpl>(
        ::fidl::fuzzing::ServerProviderDispatcherMode::kFromCaller);
  }

  std::string config;
  cobalt::crypto::Base64Decode(cobalt::logger::kConfig, &config);
  auto global_project_context_factory =
      std::make_shared<cobalt::logger::ProjectContextFactory>(config);

  return fuzzer_server_provider->Init(global_project_context_factory, secret, &manager, &encoder,
                                      &observation_writer,
                                      event_aggregator_manager.GetEventAggregator(), nullptr,
                                      undated_event_manager, nullptr, nullptr);
}

zx_status_t fuzzer_connect(zx_handle_t channel_handle, async_dispatcher_t* dispatcher) {
  manager.UpdateDispatcher(dispatcher);
  return fuzzer_server_provider->Connect(channel_handle, dispatcher);
}

zx_status_t fuzzer_disconnect(zx_handle_t channel_handle, async_dispatcher_t* dispatcher) {
  manager.UpdateDispatcher(nullptr);
  return fuzzer_server_provider->Disconnect(channel_handle, dispatcher);
}

zx_status_t fuzzer_clean_up() { return fuzzer_server_provider->CleanUp(); }
}
