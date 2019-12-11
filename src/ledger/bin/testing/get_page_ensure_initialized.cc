// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/get_page_ensure_initialized.h"

#include <lib/fit/function.h>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/lib/fxl/logging.h"

namespace ledger {
namespace {
// The delay before returning the page, after a successful initialization.
constexpr zx::duration kDelay = zx::msec(500);
}  // namespace

void GetPageEnsureInitialized(LedgerPtr* ledger, PageIdPtr requested_id,
                              DelayCallback delay_callback, fit::function<void()> error_handler,
                              fit::function<void(Status, PagePtr, PageId)> callback) {
  auto page = std::make_unique<PagePtr>();
  auto request = page->NewRequest();
  (*ledger)->GetPage(std::move(requested_id), std::move(request));
  page->set_error_handler([error_handler = std::move(error_handler)](zx_status_t status) {
    LEDGER_LOG(ERROR) << "The page connection was closed, quitting.";
    error_handler();
  });

  auto page_ptr = (*page).get();
  page_ptr->GetId(
      [delay_callback, page = std::move(page), callback = std::move(callback)](PageId page_id) {
        if (delay_callback == DelayCallback::YES) {
          zx_nanosleep(zx_deadline_after(kDelay.get()));
        }
        callback(Status::OK, std::move(*page), page_id);
      });
}
}  // namespace ledger
