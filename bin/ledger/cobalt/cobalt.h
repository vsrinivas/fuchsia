// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_COBALT_COBALT_H_
#define PERIDOT_BIN_LEDGER_COBALT_COBALT_H_

#include <lib/async/dispatcher.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/function.h>
#include <lib/fxl/functional/auto_call.h>
#include <lib/fxl/memory/ref_ptr.h>

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
fxl::AutoCall<fit::closure> InitializeCobalt(
    async_dispatcher_t* dispatcher, component::StartupContext* context);

// Report an event to Cobalt. he AutoCall object returned by |InitializeCobalt|
// must be live throughout every call to this function. This is
// thread-compatible, as long as the previous requirement is ensured across
// threads.
void ReportEvent(CobaltEvent event);

};  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_COBALT_COBALT_H_
