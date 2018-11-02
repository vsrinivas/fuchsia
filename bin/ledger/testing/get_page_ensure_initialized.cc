// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/get_page_ensure_initialized.h"

#include <lib/fit/function.h>
#include <lib/fxl/logging.h>

#include "peridot/bin/ledger/fidl/include/types.h"

namespace ledger {
void GetPageEnsureInitialized(
    LedgerPtr* ledger, PageIdPtr requested_id,
    fit::function<void()> error_handler,
    fit::function<void(Status, PagePtr, PageId)> callback) {
  auto page = std::make_unique<PagePtr>();
  auto request = page->NewRequest();
  (*ledger)->GetPage(
      std::move(requested_id), std::move(request),
      [page = std::move(page), error_handler = std::move(error_handler),
       callback = std::move(callback)](Status status) mutable {
        if (status != Status::OK) {
          FXL_LOG(ERROR) << "Failure while getting a page.";
          callback(status, nullptr, {});
          return;
        }

        page->set_error_handler(
            [error_handler = std::move(error_handler)](zx_status_t status) {
              FXL_LOG(ERROR) << "The page connection was closed, quitting.";
              error_handler();
            });

        auto page_ptr = (*page).get();
        page_ptr->GetId([page = std::move(page),
                         callback = std::move(callback)](PageId page_id) {
          callback(Status::OK, std::move(*page), page_id);
        });
      });
}
}  // namespace ledger
