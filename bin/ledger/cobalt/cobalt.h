// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_COBALT_COBALT_H_
#define APPS_LEDGER_SRC_COBALT_COBALT_H_

#include "lib/app/cpp/application_context.h"
#include "lib/ftl/functional/auto_call.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ledger {

// The events to report.
// Next enum value: 6
enum class CobaltEvent : uint32_t {
  LEDGER_STARTED = 0,
  COMMITS_RECEIVED_OUT_OF_ORDER = 1,
  COMMITS_RECEIVED_OUT_OF_ORDER_NOT_RECOVERED = 4,
  COMMITS_MERGED = 2,
  MERGED_COMMITS_MERGED = 3,
  LEDGER_LEVELDB_STATE_CORRUPTED = 5,
};

// Cobalt initialization. When cobalt is not need, the returned object must be
// deleted. This method must not be called again until then.
ftl::AutoCall<ftl::Closure> InitializeCobalt(
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    app::ApplicationContext* app_context);

// Report an event to Cobalt.
void ReportEvent(CobaltEvent event);

};  // namespace ledger

#endif  // APPS_LEDGER_SRC_COBALT_COBALT_H_
