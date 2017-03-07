// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_BENCHMARK_LIB_GET_LEDGER_H_
#define APPS_LEDGER_BENCHMARK_LIB_GET_LEDGER_H_

#include <functional>
#include <string>

#include "application/lib/app/application_context.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "lib/ftl/strings/string_view.h"

namespace benchmark {

ledger::LedgerPtr GetLedger(app::ApplicationContext* context,
                            app::ApplicationControllerPtr* controller,
                            std::string ledger_name,
                            std::string ledger_repository_path);

// Retrieves the root page of the given Ledger instance, calls the callback only
// after executing a GetId() call on the page, ensuring that it is already
// initialized.
void GetRootPageEnsureInitialized(
    ledger::Ledger* ledger,
    std::function<void(ledger::PagePtr page)> callback);

}  // namespace benchmark

#endif  // APPS_LEDGER_BENCHMARK_LIB_GET_LEDGER_H_
