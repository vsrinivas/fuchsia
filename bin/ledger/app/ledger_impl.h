// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_LEDGER_IMPL_H_
#define APPS_LEDGER_SRC_APP_LEDGER_IMPL_H_

#include <memory>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/src/app/page_manager.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/public/ledger_storage.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"

namespace ledger {

class LedgerImpl : public Ledger {
 public:
  // Delegate capable of actually performing the page operations.
  class Delegate {
   public:
    Delegate() {}
    ~Delegate() {}

    virtual void CreatePage(mojo::InterfaceRequest<Page> page_request,
                            std::function<void(Status)> callback) = 0;

    enum class CreateIfNotFound { YES, NO };
    virtual void GetPage(convert::ExtendedStringView page_id,
                         CreateIfNotFound create_if_not_found,
                         mojo::InterfaceRequest<Page> page_request,
                         std::function<void(Status)> callback) = 0;

    virtual Status DeletePage(convert::ExtendedStringView page_id) = 0;

   private:
    FTL_DISALLOW_COPY_AND_ASSIGN(Delegate);
  };

  // |delegate| outlives LedgerImpl.
  LedgerImpl(Delegate* delegate);
  ~LedgerImpl() override;

 private:
  // Ledger:
  void GetRootPage(mojo::InterfaceRequest<Page> page_request,
                   const GetRootPageCallback& callback) override;
  void GetPage(mojo::Array<uint8_t> id,
               mojo::InterfaceRequest<Page> page_request,
               const GetPageCallback& callback) override;
  void NewPage(mojo::InterfaceRequest<Page> page_request,
               const NewPageCallback& callback) override;
  void DeletePage(mojo::Array<uint8_t> id,
                  const DeletePageCallback& callback) override;

  void SetConflictResolverFactory(
      mojo::InterfaceHandle<ConflictResolverFactory> factory,
      const SetConflictResolverFactoryCallback& callback) override;

  Delegate* const delegate_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerImpl);
};
}

#endif  // APPS_LEDGER_SRC_APP_LEDGER_IMPL_H_
