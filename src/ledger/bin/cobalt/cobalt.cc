// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cobalt/cobalt.h"

#include <lib/fit/function.h>
#include <lib/fsl/vmo/file.h>

#include "src/lib/cobalt/cpp/cobalt_logger.h"

namespace ledger {
namespace {
constexpr char kConfigBinProtoPath[] = "/pkg/data/ledger_cobalt_config.pb";
constexpr int32_t kCobaltMetricId = 2;

cobalt::CobaltLogger* g_cobalt_logger = nullptr;

}  // namespace

fit::deferred_action<fit::closure> InitializeCobalt(
    async_dispatcher_t* dispatcher, sys::ComponentContext* context) {
  std::unique_ptr<cobalt::CobaltLogger> cobalt_logger;
  FXL_DCHECK(!g_cobalt_logger);

  cobalt_logger =
      cobalt::NewCobaltLogger(dispatcher, context, kConfigBinProtoPath);

  g_cobalt_logger = cobalt_logger.get();
  return fit::defer<fit::closure>([cobalt_logger = std::move(cobalt_logger)] {
    g_cobalt_logger = nullptr;
  });
}

void ReportEvent(CobaltEvent event) {
  // Do not do anything if cobalt reporting is disabled.
  if (!g_cobalt_logger) {
    return;
  }
  g_cobalt_logger->LogEvent(kCobaltMetricId, static_cast<uint32_t>(event));
}

}  // namespace ledger
