// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_ABAX_LEDGER_IMPL_H_
#define APPS_LEDGER_ABAX_LEDGER_IMPL_H_

#include <map>
#include <memory>
#include <unordered_map>

#include "apps/ledger/abax/page_impl.h"
#include "apps/ledger/api/ledger.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace ledger {

class LedgerImpl : public Ledger {
 public:
  LedgerImpl(mojo::InterfaceRequest<Ledger> request);
  ~LedgerImpl() override;

  // Initializes the ledger. This method must be called before any other method
  // if this class is called.
  Status Init();

  void OnPageError(const mojo::Array<uint8_t>& id);

 private:
  // Returns a pointer to the cached PageImpl object or nullptr if it doesn't
  // exist.
  PageImpl* GetPageImpl(const mojo::Array<uint8_t>& page_id);

  // Maps the given id to the given page in the local cache and returns a
  // raw pointer to the page.
  PageImpl* CachePageImpl(const mojo::Array<uint8_t>& page_id,
                          std::unique_ptr<PageImpl> page);

  std::unique_ptr<PageImpl> NewPageImpl(const mojo::Array<uint8_t>& page_id);

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

  struct PageIdHash {
    size_t operator()(const mojo::Array<uint8_t>& id) const;
  };

  struct PageIdEquals {
    size_t operator()(const mojo::Array<uint8_t>& id1,
                      const mojo::Array<uint8_t>& id2) const;
  };

  std::unordered_map<mojo::Array<uint8_t>,
                     std::unique_ptr<PageImpl>,
                     PageIdHash,
                     PageIdEquals>
      page_map_;
  std::map<std::string, std::string> db_;

  mojo::StrongBinding<Ledger> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerImpl);
};
}

#endif  // APPS_LEDGER_ABAX_LEDGER_IMPL_H_
