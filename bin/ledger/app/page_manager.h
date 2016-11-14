// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_PAGE_MANAGER_H_
#define APPS_LEDGER_SRC_APP_PAGE_MANAGER_H_

#include <memory>
#include <vector>

#include "apps/ledger/src/app/auto_cleanable.h"
#include "apps/ledger/src/app/page_impl.h"
#include "apps/ledger/src/app/page_snapshot_impl.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/functional/closure.h"

namespace ledger {

template <class Interface, class Impl>
class BoundInterface {
 public:
  template <class... Args>
  BoundInterface(fidl::InterfaceRequest<Interface> request, Args&&... args)
      : impl_(std::forward<Args>(args)...),
        binding_(&impl_, std::move(request)) {}

  void set_on_empty(const ftl::Closure& on_empty_callback) {
    binding_.set_connection_error_handler([this, on_empty_callback]() {
      binding_.Close();
      if (on_empty_callback)
        on_empty_callback();
    });
  }

  bool is_bound() { return binding_.is_bound(); }

 private:
  Impl impl_;
  fidl::Binding<Interface> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(BoundInterface);
};

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
  PageManager(std::unique_ptr<storage::PageStorage> page_storage);
  ~PageManager();

  // Creates a new PageImpl managed by this PageManager, and binds it to the
  // request.
  void BindPage(fidl::InterfaceRequest<Page> page_request);

  // Creates a new PageSnapshotImpl managed by this PageManager, and binds it to
  // the request.
  void BindPageSnapshot(std::unique_ptr<storage::CommitContents> contents,
                        fidl::InterfaceRequest<PageSnapshot> snapshot_request);

  void set_on_empty(const ftl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

 private:
  class PageHolder;

  void CheckEmpty();

  std::unique_ptr<storage::PageStorage> page_storage_;
  AutoCleanableSet<PageHolder> pages_;
  AutoCleanableSet<BoundInterface<PageSnapshot, PageSnapshotImpl>> snapshots_;
  ftl::Closure on_empty_callback_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageManager);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_PAGE_MANAGER_H_
