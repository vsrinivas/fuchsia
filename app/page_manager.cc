// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/app/page_manager.h"

#include <algorithm>

#include "lib/ftl/logging.h"

namespace ledger {

struct PageManager::BoundPage {
  BoundPage(storage::PageStorage* page_storage,
            mojo::InterfaceRequest<Page> request);

  PageImpl page;
  mojo::Binding<Page> binding;
};

PageManager::BoundPage::BoundPage(storage::PageStorage* page_storage,
                                  mojo::InterfaceRequest<Page> request)
    : page(page_storage), binding(&page, std::move(request)) {}

PageManager::PageManager(std::unique_ptr<storage::PageStorage> page_storage,
                         ftl::Closure on_empty_callback)
    : page_storage_(std::move(page_storage)),
      on_empty_callback_(on_empty_callback) {}

PageManager::~PageManager() {}

PagePtr PageManager::GetPagePtr() {
  PagePtr page;

  pages_.push_back(
      std::make_unique<BoundPage>(page_storage_.get(), GetProxy(&page)));
  auto* binding = &pages_.back()->binding;
  // Remove the binding and delete the impl on connection error.
  binding->set_connection_error_handler([this, binding] {
    auto it = std::find_if(pages_.begin(), pages_.end(),
                           [binding](const std::unique_ptr<BoundPage>& page) {
                             return (&page->binding == binding);
                           });
    FTL_DCHECK(it != pages_.end());
    pages_.erase(it);

    if (pages_.empty()) {
      on_empty_callback_();
    }
  });

  return page;
}

}  // namespace ledger
