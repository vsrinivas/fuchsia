// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/metrics/cpp/fidl.h>
#include <lib/fidl/cpp/fuzzing/server_provider.h>

#include <chrono>
#include <fstream>

#include "lib/async/default.h"
#include "src/cobalt/bin/app/metric_event_logger_factory_impl.h"
#include "src/cobalt/bin/utils/base64.h"
#include "src/cobalt/bin/utils/fuchsia_http_client.h"
#include "third_party/cobalt/src/lib/util/posix_file_system.h"
#include "third_party/cobalt/src/public/cobalt_service.h"

// Source of cobalt::logger::kConfig
#include "third_party/cobalt/src/logger/internal_metrics_config.cb.h"

namespace {

::fidl::fuzzing::ServerProvider<::fuchsia::metrics::MetricEventLoggerFactory,
                                ::cobalt::MetricEventLoggerFactoryImpl>* fuzzer_server_provider;

std::unique_ptr<cobalt::CobaltRegistry> ToRegistry(const std::string& registry) {
  auto cobalt_registry = std::make_unique<cobalt::CobaltRegistry>();
  if (!cobalt_registry->ParseFromString(registry)) {
    FX_LOGS(ERROR) << "Unable to parse global metrics";
  }
  return cobalt_registry;
}

cobalt::CobaltConfig cfg = {
    .file_system = std::make_unique<cobalt::util::PosixFileSystem>(),
    .use_memory_observation_store = true,
    .max_bytes_per_event = 100,
    .max_bytes_per_envelope = 100,
    .max_bytes_total = 1000,

    .upload_schedule_cfg =
        {
            .target_interval = std::chrono::seconds(10),
            .min_interval = std::chrono::seconds(10),
            .initial_interval = std::chrono::seconds(10),
            .jitter = .2,
        },

    .target_pipeline = std::make_unique<cobalt::LocalPipeline>(),

    .api_key = "",
    .client_secret = cobalt::encoder::ClientSecret::GenerateNewSecret(),

    .global_registry = ToRegistry(cobalt::Base64Decode(cobalt::logger::kConfig)),

    .local_aggregation_backfill_days = 4,
};

std::unique_ptr<cobalt::CobaltService> cobalt_service;

}  // namespace

// See https://fuchsia.dev/fuchsia-src/development/workflows/libfuzzer_fidl for explanations and
// documentations for these functions.
extern "C" {
zx_status_t fuzzer_init() {
  if (fuzzer_server_provider == nullptr) {
    fuzzer_server_provider =
        new ::fidl::fuzzing::ServerProvider<::fuchsia::metrics::MetricEventLoggerFactory,
                                            ::cobalt::MetricEventLoggerFactoryImpl>(
            ::fidl::fuzzing::ServerProviderDispatcherMode::kFromCaller);
  }

  if (cobalt_service == nullptr) {
    cobalt_service = cobalt::CobaltService::Create(std::move(cfg)).ValueOrDie();
  }

  return fuzzer_server_provider->Init(cobalt_service.get());
}

zx_status_t fuzzer_connect(zx_handle_t channel_handle, async_dispatcher_t* dispatcher) {
  return fuzzer_server_provider->Connect(channel_handle, dispatcher);
}

zx_status_t fuzzer_disconnect(zx_handle_t channel_handle, async_dispatcher_t* dispatcher) {
  return fuzzer_server_provider->Disconnect(channel_handle, dispatcher);
}

zx_status_t fuzzer_clean_up() { return fuzzer_server_provider->CleanUp(); }
}
