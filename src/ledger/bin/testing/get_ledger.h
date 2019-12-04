// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_GET_LEDGER_H_
#define SRC_LEDGER_BIN_TESTING_GET_LEDGER_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <functional>
#include <string>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/platform/detached_path.h"
#include "src/ledger/bin/public/status.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

// Creates a new Ledger application instance and returns a LedgerPtr connection
// to it. This method will call |Sync| on the Repository to ensure that the
// Ledger is ready to be used for performance benchmark.
// |close_repository|, if set, will be populated with a function that can be executed to safely
// close the underlying LedgerRepository. Its parameter will be executed when the closing is
// complete.
Status GetLedger(sys::ComponentContext* context,
                 fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller_request,
                 cloud_provider::CloudProviderPtr cloud_provider, std::string user_id,
                 std::string ledger_name, const DetachedPath& ledger_repository_path,
                 fit::function<void()> error_handler, LedgerPtr* ledger,
                 storage::GarbageCollectionPolicy gc_policy,
                 fit::function<void(fit::closure)>* close_repository = nullptr);

// Kills the remote ledger process controlled by |controller|.
void KillLedgerProcess(fuchsia::sys::ComponentControllerPtr* controller);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_GET_LEDGER_H_
