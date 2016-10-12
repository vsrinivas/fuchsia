// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_APP_LEDGER_IMPL_H_
#define APPS_LEDGER_APP_LEDGER_IMPL_H_

#include <memory>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/app/page_manager.h"
#include "apps/ledger/storage/public/ledger_storage.h"
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

    virtual void CreatePage(std::function<void(Status, PagePtr)> callback) = 0;

    enum class CreateIfNotFound { YES, NO };
    virtual void GetPage(ftl::StringView page_id,
                         CreateIfNotFound create_if_not_found,
                         std::function<void(Status, PagePtr)> callback) = 0;

    virtual Status DeletePage(ftl::StringView page_id) = 0;

   private:
    FTL_DISALLOW_COPY_AND_ASSIGN(Delegate);
  };

  // |delegate| outlives LedgerImpl.
  LedgerImpl(Delegate* delegate);
  ~LedgerImpl() override;

 private:
  // Ledger:
  void GetRootPage(const GetRootPageCallback& callback) override;
  void GetPage(mojo::Array<uint8_t> id,
               const GetPageCallback& callback) override;
  void NewPage(const NewPageCallback& callback) override;
  void DeletePage(mojo::Array<uint8_t> id,
                  const DeletePageCallback& callback) override;

  void SetConflictResolverFactory(
      mojo::InterfaceHandle<ConflictResolverFactory> factory,
      const SetConflictResolverFactoryCallback& callback) override;

  Delegate* const delegate_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerImpl);
};
}

#endif  // APPS_LEDGER_APP_LEDGER_IMPL_H_
