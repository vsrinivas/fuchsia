// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>

#include <utility>

#include <cobalt-client/cpp/collector-internal.h>
#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/counter-internal.h>
#include <cobalt-client/cpp/histogram-internal.h>
#include <cobalt-client/cpp/types-internal.h>

namespace cobalt_client {
namespace internal {
namespace {

internal::CobaltOptions MakeCobaltOptions(CollectorOptions options) {
  ZX_DEBUG_ASSERT_MSG(options.load_config || !options.project_name.empty(),
                      "Must define a load_config function or a valid project_name.");
  internal::CobaltOptions cobalt_options;
  cobalt_options.logger_deadline_first_attempt = options.initial_response_deadline;
  cobalt_options.logger_deadline = options.response_deadline;
  cobalt_options.project_name = options.project_name;
  cobalt_options.config_reader = std::move(options.load_config);
  cobalt_options.service_connect = [](const char* service_path,
                                      zx::channel service) -> zx_status_t {
    return fdio_service_connect(service_path, service.release());
  };
  cobalt_options.service_path.AppendPrintf("/svc/%s", fuchsia_cobalt_LoggerFactory_Name);
  cobalt_options.release_stage = static_cast<internal::ReleaseStage>(options.release_stage);
  return cobalt_options;
}
}  // namespace
}  // namespace internal

Collector::Collector(CollectorOptions options)
    : logger_(std::make_unique<internal::CobaltLogger>(
          internal::MakeCobaltOptions(std::move(options)))) {
  flushing_.store(false);
}

Collector::Collector(std::unique_ptr<internal::Logger> logger) : logger_(std::move(logger)) {
  flushing_.store(false);
}

Collector::~Collector() {
  if (logger_ != nullptr) {
    Flush();
  }
}

bool Collector::Flush() {
  // If we are already flushing we just return and do nothing.
  // First come first serve.
  if (flushing_.exchange(true)) {
    return false;
  }

  bool all_flushed = true;
  for (internal::FlushInterface* flushable : flushables_) {
    if (!flushable->Flush(logger_.get())) {
      all_flushed = false;
      flushable->UndoFlush();
    }
  }

  // Once we are finished we allow flushing again.
  flushing_.store(false);

  return all_flushed;
}

void Collector::UnSubscribe(internal::FlushInterface* flushable) {
  // TODO(gevalentino): Replace the vector for an unordered_map/hash_map.
  for (size_t i = 0; i < flushables_.size(); ++i) {
    if (flushable == flushables_[i]) {
      flushables_.erase(i);
      break;
    }
  }
}

void Collector::Subscribe(internal::FlushInterface* flushable) { flushables_.push_back(flushable); }

CollectorOptions CollectorOptions::GeneralAvailability() {
  CollectorOptions options;
  options.release_stage = static_cast<uint32_t>(internal::ReleaseStage::kGa);
  return options;
}

CollectorOptions CollectorOptions::Dogfood() {
  CollectorOptions options;
  options.release_stage = static_cast<uint32_t>(internal::ReleaseStage::kDogfood);
  return options;
}

CollectorOptions CollectorOptions::Fishfood() {
  CollectorOptions options;
  options.release_stage = static_cast<uint32_t>(internal::ReleaseStage::kFishfood);
  return options;
}

CollectorOptions CollectorOptions::Debug() {
  CollectorOptions options;
  options.release_stage = static_cast<uint32_t>(internal::ReleaseStage::kDebug);
  return options;
}

}  // namespace cobalt_client
