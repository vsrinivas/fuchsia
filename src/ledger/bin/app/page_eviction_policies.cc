// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_eviction_policies.h"

#include "src/ledger/lib/coroutine/coroutine_manager.h"

namespace ledger {
namespace {

// Computes the list of PageInfo for all pages that are not currently open,
// ordered by the timestamp of their last usage, in ascending order.
Status GetPagesByTimestamp(
    std::unique_ptr<storage::Iterator<const PageInfo>> pages_it,
    std::vector<PageInfo>* sorted_pages) {
  std::vector<PageInfo> pages;
  while (pages_it->Valid()) {
    // Sort out pages that are currently in use, i.e. those for which timestamp
    // is |PageInfo::kOpenedPageTimestamp|.
    if ((*pages_it)->timestamp != PageInfo::kOpenedPageTimestamp) {
      pages.push_back(**pages_it);
    }
    pages_it->Next();
  }

  // Order pages by the last used timestamp.
  std::sort(
      pages.begin(), pages.end(),
      [](const PageInfo& info1, const PageInfo& info2) {
        return std::tie(info1.timestamp, info1.ledger_name, info1.page_id) <
               std::tie(info2.timestamp, info2.ledger_name, info2.page_id);
      });

  sorted_pages->swap(pages);
  return Status::OK;
}

class LeastRecentlyUsedPageEvictionPolicy : public PageEvictionPolicy {
 public:
  LeastRecentlyUsedPageEvictionPolicy(
      coroutine::CoroutineService* coroutine_service,
      PageEvictionDelegate* delegate);

  void SelectAndEvict(std::unique_ptr<storage::Iterator<const PageInfo>> pages,
                      fit::function<void(Status)> callback) override;

 private:
  PageEvictionDelegate* delegate_;
  coroutine::CoroutineManager coroutine_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LeastRecentlyUsedPageEvictionPolicy);
};

LeastRecentlyUsedPageEvictionPolicy::LeastRecentlyUsedPageEvictionPolicy(
    coroutine::CoroutineService* coroutine_service,
    PageEvictionDelegate* delegate)
    : delegate_(delegate), coroutine_manager_(coroutine_service) {}

void LeastRecentlyUsedPageEvictionPolicy::SelectAndEvict(
    std::unique_ptr<storage::Iterator<const PageInfo>> pages_it,
    fit::function<void(Status)> callback) {
  coroutine_manager_.StartCoroutine(
      std::move(callback), [this, pages_it = std::move(pages_it)](
                               coroutine::CoroutineHandler* handler,
                               fit::function<void(Status)> callback) mutable {
        std::vector<PageInfo> pages;
        Status status = GetPagesByTimestamp(std::move(pages_it), &pages);
        if (status != Status::OK) {
          callback(status);
          return;
        }
        for (const auto& page_info : pages) {
          PageWasEvicted was_evicted;
          auto sync_call_status = coroutine::SyncCall(
              handler,
              [this, ledger_name = std::move(page_info.ledger_name),
               page_id = std::move(page_info.page_id)](auto callback) {
                delegate_->TryEvictPage(ledger_name, page_id,
                                        PageEvictionCondition::IF_POSSIBLE,
                                        std::move(callback));
              },
              &status, &was_evicted);
          if (sync_call_status == coroutine::ContinuationStatus::INTERRUPTED) {
            callback(Status::INTERNAL_ERROR);
            return;
          }
          if (status != Status::OK || was_evicted) {
            callback(status);
            return;
          }
        }
        callback(Status::OK);
      });
}

}  // namespace

std::unique_ptr<PageEvictionPolicy> NewLeastRecentyUsedPolicy(
    coroutine::CoroutineService* coroutine_service,
    PageEvictionDelegate* delegate) {
  return std::make_unique<LeastRecentlyUsedPageEvictionPolicy>(
      coroutine_service, delegate);
}

}  // namespace ledger
