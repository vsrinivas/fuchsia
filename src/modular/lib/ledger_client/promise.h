// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_LEDGER_CLIENT_PROMISE_H_
#define SRC_MODULAR_LIB_LEDGER_CLIENT_PROMISE_H_

#include <fuchsia/ledger/cpp/fidl.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>

#include "src/lib/fxl/logging.h"
#include "src/modular/lib/fidl/array_to_string.h"  // for to_string(), to_array()

namespace modular {

// These wrapper functions wrap fuchsia.ledger APIs such that they return
// a fit::promise<> representing the result of the call. They are useful to
// incorporate into tasks themselves expressed as fit::promises.
//
// The wrapper classes act as namespaces for static methods. For a given api
// (such as fuchsia.ledger.Page), the wrapper class is called PagePromise, and
// each method has the same signature as that in fuchsia::ledger::Page, with the
// addition that the first parameter is always a fuchsia::ledger::Page* and the
// last parameter (the result callback) is omitted.
//
// Note that these wrapper methods IMMEDIATELY call the underlying FIDL
// function (meaning that a message is dispatched immediately on the underlying
// channel). The returned promise will block until a response message is
// received.
//
// EXAMPLE
//
//   auto p = fit::make_promise([snapshot = page_snapshot_ptr.get()] () {
//     return PageSnapshotPromise::GetInline(snapshot, key);
//   }).and_then([] (const std::vector<uint8_t> bytes) {
//     // Decode and use |bytes|.
//   });

// fit::promise wrapper functions for fuchsia.ledger.PageSnspahot.
//
// These methods match the signatures in fuchsia.ledger.PageSnapshot with the
// exception that the first parameter is always a
// fuchsia::ledger::PageSnapshot*.
class PageSnapshotPromise {
 public:
  // fit::promise wrapper function for PageSnapshot.GetInline().
  //
  // Falls back to PageSnapshot::Get() if the value is too large.
  // TODO(thatguy): Implement the fallback.
  static fit::promise<std::unique_ptr<std::vector<uint8_t>>> GetInline(
      fuchsia::ledger::PageSnapshot* snapshot, std::string key) {
    fit::bridge<std::unique_ptr<std::vector<uint8_t>>> bridge;
    snapshot->GetInline(
        to_array(key), [completer = std::move(bridge.completer)](
                           fuchsia::ledger::PageSnapshot_GetInline_Result result) mutable {
          if (result.is_response()) {
            completer.complete_ok(
                std::make_unique<std::vector<uint8_t>>(std::move(result.response().value.value)));
            return;
          }
          switch (result.err()) {
            case fuchsia::ledger::Error::KEY_NOT_FOUND:
              completer.complete_ok(nullptr);
              return;
            case fuchsia::ledger::Error::NEEDS_FETCH:
            case fuchsia::ledger::Error::NETWORK_ERROR:
              FXL_LOG(ERROR) << "PageSnapshotPromise::GetInline() failed with "
                             << fidl::ToUnderlying(result.err());
              completer.complete_error();
          }
        });
    return bridge.consumer.promise();
  }
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_LEDGER_CLIENT_PROMISE_H_
