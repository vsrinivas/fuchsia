// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_COBALT_COBALT_H_
#define SRC_LEDGER_BIN_COBALT_COBALT_H_

#include <lib/async/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

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
fit::deferred_action<fit::closure> InitializeCobalt(async_dispatcher_t* dispatcher,
                                                    sys::ComponentContext* context);

// Report an event to Cobalt. The callback returned by |InitializeCobalt|
// must be live throughout every call to this function. This is
// thread-compatible, as long as the previous requirement is ensured across
// threads.
void ReportEvent(CobaltEvent event);

};  // namespace ledger

#endif  // SRC_LEDGER_BIN_COBALT_COBALT_H_
