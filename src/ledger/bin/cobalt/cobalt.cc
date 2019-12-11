// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cobalt/cobalt.h"

#include <lib/fit/function.h>

#include "src/ledger/bin/cobalt/ledger_metrics_registry.cb.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/vmo/file.h"
#include "src/lib/cobalt/cpp/cobalt_logger.h"

namespace ledger {
namespace {

cobalt::CobaltLogger* g_cobalt_logger = nullptr;

}  // namespace

fit::deferred_action<fit::closure> InitializeCobalt(async_dispatcher_t* dispatcher,
                                                    sys::ComponentContext* context) {
  std::unique_ptr<cobalt::CobaltLogger> cobalt_logger;
  LEDGER_DCHECK(!g_cobalt_logger);

  cobalt_logger = cobalt::NewCobaltLoggerFromProjectName(dispatcher, context->svc(),
                                                         cobalt_registry::kProjectName);

  g_cobalt_logger = cobalt_logger.get();
  return fit::defer<fit::closure>(
      [cobalt_logger = std::move(cobalt_logger)] { g_cobalt_logger = nullptr; });
}

void ReportEvent(CobaltEvent event) {
  // Do not do anything if cobalt reporting is disabled.
  if (!g_cobalt_logger) {
    return;
  }
  g_cobalt_logger->LogEvent(cobalt_registry::kRareEventOccurrenceMetricId,
                            static_cast<uint32_t>(event));
}

}  // namespace ledger
