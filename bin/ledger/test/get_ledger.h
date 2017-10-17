// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_GET_LEDGER_H_
#define PERIDOT_BIN_LEDGER_TEST_GET_LEDGER_H_

#include <functional>
#include <string>

#include "lib/app/cpp/application_context.h"
#include "lib/auth/fidl/token_provider.fidl.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/fidl_helpers/boundable.h"

namespace test {
enum SyncState { DISABLED = 0, CLOUD_SYNC_ENABLED };
enum Erase { KEEP_DATA = 0, ERASE_CLOUD };

// TODO(ppi): take the server_id as std::optional<std::string> and drop bool
// sync once we're on C++17.
// Creates a new Ledger application instance and returns a LedgerPtr connection
// to it. If |erase_first| is true, an EraseRepository command is issued first
// before connecting, ensuring a clean state before proceeding.
ledger::Status GetLedger(
    fsl::MessageLoop* loop,
    app::ApplicationContext* context,
    app::ApplicationControllerPtr* controller,
    fxl::RefPtr<fxl::TaskRunner> services_task_runner,
    ledger::fidl_helpers::SetBoundable<modular::auth::TokenProvider>*
        token_provider_impl,
    std::string ledger_name,
    std::string ledger_repository_path,
    SyncState sync,
    std::string server_id,
    ledger::LedgerPtr* ledger_ptr,
    Erase erase = KEEP_DATA);

// Retrieves the requested page of the given Ledger instance and calls the
// callback only after executing a GetId() call on the page, ensuring that it is
// already initialized. If |id| is nullptr, a new page with a unique id is
// created.
ledger::Status GetPageEnsureInitialized(fsl::MessageLoop* loop,
                                        ledger::LedgerPtr* ledger,
                                        fidl::Array<uint8_t> requested_id,
                                        ledger::PagePtr* page,
                                        fidl::Array<uint8_t>* page_id);

}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TEST_GET_LEDGER_H_
