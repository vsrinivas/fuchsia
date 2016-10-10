// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_APP_LEDGER_IMPL_H_
#define APPS_LEDGER_APP_LEDGER_IMPL_H_

#include <memory>
#include <type_traits>
#include <unordered_map>

#include "apps/ledger/api/ledger.mojom.h"
#include "apps/ledger/app/page_manager.h"
#include "apps/ledger/storage/public/ledger_storage.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace ledger {

class LedgerImpl : public Ledger {
 public:
  LedgerImpl(mojo::InterfaceRequest<Ledger> request,
             std::unique_ptr<storage::LedgerStorage> storage);
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

  // Adds a new PageManager for |page_id| and configures its so that we delete
  // it from |page_managers_| automatically when the last local client
  // disconnects from the page.
  PageManager& AddPageManager(
      const storage::PageId& page_id,
      std::unique_ptr<storage::PageStorage> page_storage);

  mojo::StrongBinding<Ledger> binding_;
  std::unique_ptr<storage::LedgerStorage> storage_;

  // Comparator that allows heterogeneous lookup by StringView and
  // storage::PageId in a container with the key type of storage::PageId.
  struct Comparator {
    using is_transparent = std::true_type;
    bool operator()(const storage::PageId& lhs,
                    const storage::PageId& rhs) const {
      return lhs < rhs;
    }

    bool operator()(const storage::PageId& lhs, ftl::StringView rhs) const {
      return ftl::StringView(lhs) < rhs;
    }
    bool operator()(ftl::StringView lhs, const storage::PageId& rhs) const {
      return lhs < ftl::StringView(rhs);
    }
  };
  // Mapping from page id to the manager of that page.
  std::map<storage::PageId, std::unique_ptr<PageManager>, Comparator>
      page_managers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerImpl);
};
}

#endif  // APPS_LEDGER_APP_LEDGER_IMPL_H_
