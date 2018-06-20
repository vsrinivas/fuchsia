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
//
// The first instance is asynchronous and expects that an async
// dispatcher will be run to make remote FIDL calls. The second and third
// variation will run the given loop and return the values synchronously.
//
// TODO(ppi): take the server_id as std::optional<std::string> and drop bool
// sync once we're on C++17.
void GetLedger(fuchsia::sys::StartupContext* context,
               fidl::InterfaceRequest<fuchsia::sys::ComponentController>
                   controller_request,
               cloud_provider::CloudProviderPtr cloud_provider,
               std::string ledger_name, std::string ledger_repository_path,
               fit::function<void()> error_handler,
               fit::function<void(ledger::Status, ledger::LedgerPtr)> callback);
ledger::Status GetLedger(
    async::Loop* loop, fuchsia::sys::StartupContext* context,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController>
        controller_request,
    cloud_provider::CloudProviderPtr cloud_provider, std::string ledger_name,
    std::string ledger_repository_path, ledger::LedgerPtr* ledger_ptr);
// Deprecated version of |GetLedger| using a fsl::MessageLoop.
// TODO(qsr): Remove when all usage are removed.
ledger::Status GetLedger(
    fsl::MessageLoop* loop, fuchsia::sys::StartupContext* context,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController>
        controller_request,
    cloud_provider::CloudProviderPtr cloud_provider, std::string ledger_name,
    std::string ledger_repository_path, ledger::LedgerPtr* ledger_ptr);

// Retrieves the requested page of the given Ledger instance amd returns after
// ensuring that it is initialized. If |id| is nullptr, a new page with a unique
// id is created.
//
// The first instance is asynchronous and expects that an async
// dispatcher will be run to make remote FIDL calls. The second and third
// variation will run the given loop and return the values synchronously.
void GetPageEnsureInitialized(
    ledger::LedgerPtr* ledger, ledger::PageIdPtr requested_id,
    fit::function<void()> error_handler,
    fit::function<void(ledger::Status, ledger::PagePtr, ledger::PageId)>
        callback);
ledger::Status GetPageEnsureInitialized(async::Loop* loop,
                                        ledger::LedgerPtr* ledger,
                                        ledger::PageIdPtr requested_id,
                                        ledger::PagePtr* page,
                                        ledger::PageId* page_id);
// Deprecated version of |GetPageEnsureInitialized| using a fsl::MessageLoop.
// TODO(qsr): Remove when all usage are removed.
ledger::Status GetPageEnsureInitialized(fsl::MessageLoop* loop,
                                        ledger::LedgerPtr* ledger,
                                        ledger::PageIdPtr requested_id,
                                        ledger::PagePtr* page,
                                        ledger::PageId* page_id);

// Kills the remote ledger process controlled by |controller|.
void KillLedgerProcess(fuchsia::sys::ComponentControllerPtr* controller);

}  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TESTING_GET_LEDGER_H_
