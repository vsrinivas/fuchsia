// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_COBALT_COBALT_H_
#define APPS_LEDGER_SRC_COBALT_COBALT_H_

#include "application/lib/app/application_context.h"
#include "lib/ftl/functional/auto_call.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ledger {

// The events to report.
enum class CobaltEvent {
  LEDGER_STARTED,
  COMMITS_RECEIVED_OUT_OF_ORDER,
  COMMITS_MERGED,
  MERGED_COMMITS_MERGED,
};

ftl::AutoCall<ftl::Closure> InitializeCobalt(
    ftl::RefPtr<ftl::TaskRunner> task_runner,
    app::ApplicationContext* app_context);

void ReportEvent(CobaltEvent event);

};  // namespace ledger

#endif  // APPS_LEDGER_SRC_COBALT_COBALT_H_
