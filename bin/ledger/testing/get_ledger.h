// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_GET_LEDGER_H_
#define PERIDOT_BIN_LEDGER_TESTING_GET_LEDGER_H_

#include <functional>
#include <string>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/function.h>

#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/filesystem/detached_path.h"

namespace ledger {

// Creates a new Ledger application instance and returns a LedgerPtr connection
// to it. This method will call |Sync| on the Repository to ensure that the
// Ledger is ready to be used for performance benchmark.
Status GetLedger(component::StartupContext* context,
                 fidl::InterfaceRequest<fuchsia::sys::ComponentController>
                     controller_request,
                 cloud_provider::CloudProviderPtr cloud_provider,
                 std::string user_id, std::string ledger_name,
                 const DetachedPath& ledger_repository_path,
                 fit::function<void()> error_handler, LedgerPtr* ledger);

// Kills the remote ledger process controlled by |controller|.
void KillLedgerProcess(fuchsia::sys::ComponentControllerPtr* controller);

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_GET_LEDGER_H_
