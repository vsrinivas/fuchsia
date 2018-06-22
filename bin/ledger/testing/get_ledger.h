// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_GET_LEDGER_H_
#define PERIDOT_BIN_LEDGER_TESTING_GET_LEDGER_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include <functional>
#include <string>

#include "lib/app/cpp/startup_context.h"
#include "lib/fit/function.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/ledger/fidl/include/types.h"

namespace test {

// Creates a new Ledger application instance and returns a LedgerPtr connection
// to it.
void GetLedger(fuchsia::sys::StartupContext* context,
               fidl::InterfaceRequest<fuchsia::sys::ComponentController>
                   controller_request,
               cloud_provider::CloudProviderPtr cloud_provider,
               std::string ledger_name, std::string ledger_repository_path,
               fit::function<void()> error_handler,
               fit::function<void(ledger::Status, ledger::LedgerPtr)> callback);

// Retrieves the requested page of the given Ledger instance amd returns after
// ensuring that it is initialized. If |id| is nullptr, a new page with a unique
// id is created.
void GetPageEnsureInitialized(
    ledger::LedgerPtr* ledger, ledger::PageIdPtr requested_id,
    fit::function<void()> error_handler,
    fit::function<void(ledger::Status, ledger::PagePtr, ledger::PageId)>
        callback);

// Kills the remote ledger process controlled by |controller|.
void KillLedgerProcess(fuchsia::sys::ComponentControllerPtr* controller);

}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTING_GET_LEDGER_H_
