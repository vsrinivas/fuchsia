// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_BENCHMARK_LIB_GET_LEDGER_H_
#define APPS_LEDGER_BENCHMARK_LIB_GET_LEDGER_H_

#include <functional>
#include <string>

#include "application/lib/app/application_context.h"
#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "lib/ftl/strings/string_view.h"

namespace benchmark {

// TODO(ppi): take the server_id as std::optional<std::string> and drop bool
// sync once we're on C++17.
ledger::LedgerPtr GetLedger(
    app::ApplicationContext* context,
    app::ApplicationControllerPtr* controller,
    std::string ledger_name,
    std::string ledger_repository_path,
    bool sync,
    std::string server_id,
    ledger::LedgerControllerPtr* ledger_controller = nullptr);

// Retrieves the requested page of the given Ledger instance and calls the
// callback only after executing a GetId() call on the page, ensuring that it is
// already initialized. If |id| is nullptr, a new page with a unique id is
// created.
void GetPageEnsureInitialized(
    ledger::Ledger* ledger,
    fidl::Array<uint8_t> id,
    std::function<void(ledger::PagePtr page, fidl::Array<uint8_t> id)>
        callback);

}  // namespace benchmark

#endif  // APPS_LEDGER_BENCHMARK_LIB_GET_LEDGER_H_
