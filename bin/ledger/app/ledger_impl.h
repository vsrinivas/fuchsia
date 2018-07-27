// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_LEDGER_IMPL_H_
#define PERIDOT_BIN_LEDGER_APP_LEDGER_IMPL_H_

#include <memory>

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/app/page_manager.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/storage/public/ledger_storage.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {

// An implementation of the |Ledger| FIDL interface.
class LedgerImpl : public Ledger {
 public:
  // Delegate capable of actually performing the page operations.
  class Delegate {
   public:
    // State of a new page. If the state is |NEW|, it is known that it doesn't
    // have any content on the cloud or on another device.
    enum class PageState {
      // The page is new and has been created locally.
      NEW,
      // The page has been named by the client.
      // well known name
      NAMED,
    };

    Delegate() {}
    virtual ~Delegate() = default;

    virtual void GetPage(convert::ExtendedStringView page_id,
                         PageState page_state,
                         fidl::InterfaceRequest<Page> page_request,
                         fit::function<void(Status)> callback) = 0;

    virtual void SetConflictResolverFactory(
        fidl::InterfaceHandle<ConflictResolverFactory> factory) = 0;

   private:
    FXL_DISALLOW_COPY_AND_ASSIGN(Delegate);
  };

  // |delegate| outlives LedgerImpl.
  explicit LedgerImpl(Delegate* delegate);
  ~LedgerImpl() override;

 private:
  // Ledger:
  void GetRootPage(fidl::InterfaceRequest<Page> page_request,
                   GetRootPageCallback callback) override;
  void GetPage(PageIdPtr id, fidl::InterfaceRequest<Page> page_request,
               GetPageCallback callback) override;

  void SetConflictResolverFactory(
      fidl::InterfaceHandle<ConflictResolverFactory> factory,
      SetConflictResolverFactoryCallback callback) override;

  Delegate* const delegate_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerImpl);
};
}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_LEDGER_IMPL_H_
