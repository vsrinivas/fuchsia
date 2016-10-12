// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_APP_PAGE_MANAGER_H_
#define APPS_LEDGER_APP_PAGE_MANAGER_H_

#include <memory>
#include <vector>

#include "apps/ledger/app/page_impl.h"
#include "apps/ledger/storage/public/page_storage.h"
#include "lib/ftl/functional/closure.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_request.h"

namespace ledger {

// Manages a ledger page.
//
// PageManager owns all page-level objects related to a single page: page
// storage, and a set of mojo PageImpls backed by the page storage. It is safe
// to delete it at any point - this closes all message pipes, deletes PageImpls
// and tears down the storage.
//
// When the set of PageImpls becomes empty, client is notified through
// |on_empty_callback|.
class PageManager {
 public:
  // |page_storage| becomes owned by PageManager and is deleted when it goes
  //   away
  // |on_empty_callback| is called each time the set of managed PageImpls
  //   becomes empty. It is valid to delete PageManager synchronously within
  //   that callback.
  PageManager(std::unique_ptr<storage::PageStorage> page_storage,
              ftl::Closure on_empty_callback);
  ~PageManager();

  // Creates a new PageImpl managed by this PageManager, and returns a proxy
  // bound to it.
  PagePtr GetPagePtr();

 private:
  struct BoundPage;

  std::unique_ptr<storage::PageStorage> page_storage_;

  // TODO(ppi): switch to something like a (Strong)BindingSet when they grow
  // facilities to notify the client when the bindings shut down, so that we can
  // implement |on_empty_callback|.
  std::vector<std::unique_ptr<BoundPage>> pages_;

  ftl::Closure on_empty_callback_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageManager);
};

}  // namespace ledger

#endif  // APPS_LEDGER_APP_PAGE_MANAGER_H_
