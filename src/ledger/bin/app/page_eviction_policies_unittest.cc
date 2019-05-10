// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_eviction_policies.h"

#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ledger/bin/testing/test_with_environment.h"

namespace ledger {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// A wrapper storage::Iterator for the elements of an std::vector<T>.
template <class T>
class VectorIterator : public storage::Iterator<T> {
 public:
  VectorIterator(const std::vector<T>& v) : it_(v.begin()), end_(v.end()) {}

  ~VectorIterator() {}

  storage::Iterator<T>& Next() override {
    ++it_;
    return *this;
  }

  bool Valid() const override { return it_ != end_; }

  Status GetStatus() const override { return Status::OK; }

  T& operator*() const override { return *it_; }

  T* operator->() const override { return &*it_; }

 private:
  typename std::vector<T>::iterator it_;
  typename std::vector<T>::iterator end_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VectorIterator);
};

// A fake PageEvictionDelegate, that stores the set of pages that were evicted.
class FakePageEvictionDelegate : public PageEvictionDelegate {
 public:
  FakePageEvictionDelegate() {}
  ~FakePageEvictionDelegate() {}

  void TryEvictPage(fxl::StringView ledger_name, storage::PageIdView page_id,
                    PageEvictionCondition condition,
                    fit::function<void(Status, PageWasEvicted)> callback) {
    if (try_evict_page_status_ != Status::OK) {
      callback(try_evict_page_status_, PageWasEvicted(false));
      return;
    }
    if (pages_not_to_evict_.find(page_id.ToString()) !=
        pages_not_to_evict_.end()) {
      callback(Status::OK, PageWasEvicted(false));
      return;
    }
    evicted_pages_.push_back(page_id.ToString());
    callback(Status::OK, PageWasEvicted(true));
  }

  const std::vector<storage::PageId>& GetEvictedPages() {
    return evicted_pages_;
  }

  void SetPagesNotToEvict(std::set<storage::PageId> pages_not_to_evict) {
    pages_not_to_evict_ = std::move(pages_not_to_evict);
  }

  void SetTryEvictPageStatus(Status status) { try_evict_page_status_ = status; }

 private:
  // The vector of pages for which |TryEvictPage| returned PageWasEvicted(true).
  std::vector<storage::PageId> evicted_pages_;
  // Pages in this set will return PageWasEvicted(false) if TryEvictPage is
  // called on them.
  std::set<storage::PageId> pages_not_to_evict_;
  // The status to be returned by |TryEvictPage|.
  Status try_evict_page_status_ = Status::OK;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakePageEvictionDelegate);
};

using PageEvictionPoliciesTest = TestWithEnvironment;

TEST_F(PageEvictionPoliciesTest, LeastRecentyUsed) {
  FakePageEvictionDelegate delegate;
  std::string ledger_name = "ledger";
  std::vector<const PageInfo> pages = {
      {ledger_name, "page1", zx::time_utc(1)},
      {ledger_name, "page2", zx::time_utc(2)},
      {ledger_name, "page3", zx::time_utc(3)},
      {ledger_name, "page4", zx::time_utc(4)},
  };

  std::unique_ptr<PageEvictionPolicy> policy =
      NewLeastRecentyUsedPolicy(environment_.coroutine_service(), &delegate);

  // Expect to only evict the least recently used page, i.e. "page1".
  bool called;
  Status status;
  policy->SelectAndEvict(
      std::make_unique<VectorIterator<const PageInfo>>(pages),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_THAT(delegate.GetEvictedPages(), ElementsAre("page1"));
}

TEST_F(PageEvictionPoliciesTest, LeastRecentyUsedWithOpenPages) {
  FakePageEvictionDelegate delegate;
  std::string ledger_name = "ledger";
  std::vector<const PageInfo> pages = {
      {ledger_name, "page1", PageInfo::kOpenedPageTimestamp},
      {ledger_name, "page2", zx::time_utc(2)},
      {ledger_name, "page3", zx::time_utc(3)},
      {ledger_name, "page4", zx::time_utc(4)},
  };

  std::unique_ptr<PageEvictionPolicy> policy =
      NewLeastRecentyUsedPolicy(environment_.coroutine_service(), &delegate);

  // "page1" should not be evicted as it is marked as open. Expect to only evict
  // the least recently used page, i.e. "page2".
  bool called;
  Status status;
  policy->SelectAndEvict(
      std::make_unique<VectorIterator<const PageInfo>>(pages),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_THAT(delegate.GetEvictedPages(), ElementsAre("page2"));
}

TEST_F(PageEvictionPoliciesTest, LeastRecentyUsedNoPagesToEvict) {
  FakePageEvictionDelegate delegate;
  std::string ledger_name = "ledger";
  std::vector<const PageInfo> pages = {
      {ledger_name, "page1", PageInfo::kOpenedPageTimestamp},
      {ledger_name, "page2", zx::time_utc(2)},
      {ledger_name, "page3", zx::time_utc(3)},
      {ledger_name, "page4", zx::time_utc(4)},
  };

  delegate.SetPagesNotToEvict({"page2", "page3", "page4"});

  std::unique_ptr<PageEvictionPolicy> policy =
      NewLeastRecentyUsedPolicy(environment_.coroutine_service(), &delegate);

  // "page1" is marked as open, and pages 2-4 will fail to be evicted. The
  // returned status should be ok, and not pages will be evicted.
  bool called;
  Status status;
  policy->SelectAndEvict(
      std::make_unique<VectorIterator<const PageInfo>>(pages),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::OK, status);
  EXPECT_THAT(delegate.GetEvictedPages(), IsEmpty());
}

TEST_F(PageEvictionPoliciesTest, LeastRecentyUsedErrorWhileEvicting) {
  FakePageEvictionDelegate delegate;
  std::string ledger_name = "ledger";
  std::vector<const PageInfo> pages = {
      {ledger_name, "page1", zx::time_utc(1)},
      {ledger_name, "page2", zx::time_utc(2)},
      {ledger_name, "page3", zx::time_utc(3)},
      {ledger_name, "page4", zx::time_utc(4)},
  };
  delegate.SetTryEvictPageStatus(Status::INTERNAL_ERROR);

  // If |TryEvictPage| fails, so should |SelectAndEvict|. Expect to find the
  // same error status.
  std::unique_ptr<PageEvictionPolicy> policy =
      NewLeastRecentyUsedPolicy(environment_.coroutine_service(), &delegate);
  bool called;
  Status status;
  policy->SelectAndEvict(
      std::make_unique<VectorIterator<const PageInfo>>(pages),
      callback::Capture(callback::SetWhenCalled(&called), &status));
  EXPECT_TRUE(called);
  EXPECT_EQ(Status::INTERNAL_ERROR, status);
}

}  // namespace
}  // namespace ledger
