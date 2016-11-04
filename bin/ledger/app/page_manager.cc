// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/page_manager.h"

#include <algorithm>

#include "apps/ledger/src/app/branch_tracker.h"
#include "lib/ftl/logging.h"

namespace ledger {
// Holds a page and its watchers. A page and its watchers are tracking the same
// branch of the commit tree.
class PageManager::PageHolder {
 public:
  PageHolder(PageManager* manager,
             storage::PageStorage* storage,
             fidl::InterfaceRequest<Page> request,
             std::function<void(PageHolder*)> on_empty_callback)
      : tracker_(storage),
        interface_(
            std::make_unique<BoundInterface<Page, PageImpl>>(std::move(request),
                                                             manager,
                                                             storage,
                                                             &tracker_)),
        on_empty_callback_(on_empty_callback) {
    // Remove the binding and delete the impl on connection error.
    interface_->binding.set_connection_error_handler([this] {
      FTL_DCHECK(interface_);
      interface_.reset();

      if (watchers_.empty()) {
        on_empty_callback_(this);
      }
    });
  }

 private:
  BranchTracker tracker_;
  std::unique_ptr<BoundInterface<Page, PageImpl>> interface_;
  std::vector<PageWatcherPtr> watchers_;

  std::function<void(PageHolder*)> on_empty_callback_;
};

PageManager::PageManager(std::unique_ptr<storage::PageStorage> page_storage,
                         ftl::Closure on_empty_callback)
    : page_storage_(std::move(page_storage)),
      on_empty_callback_(on_empty_callback) {}

PageManager::~PageManager() {}

void PageManager::BindPage(fidl::InterfaceRequest<Page> page_request) {
  pages_.push_back(std::make_unique<PageHolder>(
      this, page_storage_.get(), std::move(page_request),
      [this](PageHolder* holder) {
        auto it = std::find_if(
            pages_.begin(), pages_.end(),
            [holder](const std::unique_ptr<PageHolder>& page_holder) {
              return (page_holder.get() == holder);
            });
        FTL_DCHECK(it != pages_.end());
        pages_.erase(it);

        if (pages_.empty() && snapshots_.empty()) {
          on_empty_callback_();
        }
      }));
}

void PageManager::BindPageSnapshot(
    std::unique_ptr<storage::CommitContents> contents,
    fidl::InterfaceRequest<PageSnapshot> snapshot_request) {
  snapshots_.push_back(
      std::make_unique<BoundInterface<PageSnapshot, PageSnapshotImpl>>(
          std::move(snapshot_request), page_storage_.get(),
          std::move(contents)));
  auto* binding = &snapshots_.back()->binding;
  // Remove the binding and delete the impl on connection error.
  binding->set_connection_error_handler([this, binding] {
    auto it = std::find_if(
        snapshots_.begin(), snapshots_.end(),
        [binding](const std::unique_ptr<
                  BoundInterface<PageSnapshot, PageSnapshotImpl>>& snapshot) {
          return (&snapshot->binding == binding);
        });
    FTL_DCHECK(it != snapshots_.end());
    snapshots_.erase(it);

    if (pages_.empty() && snapshots_.empty()) {
      on_empty_callback_();
    }
  });
}

}  // namespace ledger
