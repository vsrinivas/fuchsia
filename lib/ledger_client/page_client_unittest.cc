// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/ledger_client/page_client.h"
#include "gtest/gtest.h"
#include "lib/fxl/macros.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/testing/test_with_ledger.h"

namespace modular {
namespace testing {
namespace {

class PageClientImpl : PageClient {
 public:
  PageClientImpl(LedgerClient* ledger_client,
                 LedgerPageId page_id,
                 std::string prefix = "")
      : PageClient("PageClientImpl",
                   ledger_client,
                   std::move(page_id),
                   std::move(prefix)) {}

  ~PageClientImpl() override = default;

  void OnPageChange(const std::string& key, const std::string& value) override {
    ++change_count_;

    values_[key] = value;

    FXL_LOG(INFO) << "OnPageChange \"" << prefix() << "\""
                  << " " << change_count_ << " " << key << " " << value;
  }

  void OnPageConflict(Conflict* const conflict) override {
    ++conflict_count_;

    FXL_LOG(INFO) << "OnPageConflict " << prefix() << " " << conflict_count_
                  << " " << conflict->key << " " << conflict->left << " "
                  << conflict->right;

    conflict->resolution = MERGE;
    conflict->merged = "value3";
  }

  int change_count() const { return change_count_; }
  int conflict_count() const { return conflict_count_; }

  const std::string& value(const std::string& key) { return values_[key]; }
  ledger::Page* page() { return PageClient::page(); }

 private:
  std::map<std::string, std::string> values_;
  int change_count_{};
  int conflict_count_{};
};

class PageClientTest : public TestWithLedger {
 public:
  PageClientTest() : page_id_(to_array("0123456789123456")) {}

  ~PageClientTest() = default;

  void SetUp() override {
    TestWithLedger::SetUp();
    // We only handle one conflict resolution per test case for now.
    ledger_client()->add_watcher([this] { resolved_ = true; });

    // TODO(mesch): Registration order matters for overlapping prefixes. This
    // should not be like that.
    page_client_a_.reset(
        new PageClientImpl(ledger_client(), page_id_.Clone(), "a/"));
    page_client_b_.reset(
        new PageClientImpl(ledger_client(), page_id_.Clone(), "b/"));
    page_client_.reset(
        new PageClientImpl(ledger_client(), page_id_.Clone()));

    ledger_client()->ledger()->GetPage(
        page_id_.Clone(), page_.NewRequest(),
        [](ledger::Status status) { ASSERT_EQ(ledger::Status::OK, status); });
  }

  void TearDown() override {
    page_client_.reset();
    page_client_a_.reset();
    page_client_b_.reset();

    TestWithLedger::TearDown();
  }

  PageClientImpl* page_client() { return page_client_.get(); }
  ledger::Page* page() { return page_.get(); }

  PageClientImpl* page_client_a() { return page_client_a_.get(); }
  PageClientImpl* page_client_b() { return page_client_b_.get(); }

  // Shorthand for the two connections to the page, and the two clients on the
  // first connection.
  ledger::Page* page1() { return page_client_->page(); }
  ledger::Page* page1a() { return page_client_a_->page(); }
  ledger::Page* page1b() { return page_client_b_->page(); }
  ledger::Page* page2() { return page_.get(); }

  // Factory for a ledger callback function that just logs errors.
  std::function<void(ledger::Status)> log(std::string context) {
    return [context](ledger::Status status) {
      EXPECT_EQ(ledger::Status::OK, status) << context;
    };
  }

  void Finish() { finished_ = true; }

  // Runs the message loop until after Finish() completes, then optionally until
  // conflicts are resolved, then until the observed |condition| becomes true.
  void Run(const bool wait_for_resolved, std::function<bool()> condition) {
    RunLoopUntil(
        [ this, wait_for_resolved, condition = std::move(condition) ] {
          // First wait for Finish() to be called.
          if (!finished_) {
            return false;
          }

          // Then wait for conflict resolution to complete.
          if (wait_for_resolved && !resolved_) {
            return false;
          }

          // Finally, wait for the observed condition. TODO(mesch): This is
          // lame, because test failure now requires us to time out, however the
          // actual condition to wait for -- that the conflict is resolved and
          // watcher notifications are dispatched -- is not observable from
          // here.
          if (!condition()) {
            return false;
          }

          return true;
        },

        // NOTE(mesch): Test cases here take about 300ms when running in CI.
        // Occasionally they take much longer, presumably because of load on
        // shared machines. With the default timeout, we see flakiness. Cf.
        // FW-287.
        fxl::TimeDelta::FromSeconds(10));
  }

 private:
  const LedgerPageId page_id_;

  // Separate page clients for different prefixes all for the same page. They
  // share the same page connection since they are created on the same ledger
  // client.
  std::unique_ptr<PageClientImpl> page_client_;
  std::unique_ptr<PageClientImpl> page_client_a_;
  std::unique_ptr<PageClientImpl> page_client_b_;

  // A separate connection to the same page as page_client_, used to create
  // conflicts.
  ledger::PagePtr page_;

  // Used by Finish() and Run();
  bool finished_{};
  bool resolved_{};

  FXL_DISALLOW_COPY_AND_ASSIGN(PageClientTest);
};

TEST_F(PageClientTest, SimpleWrite) {
  page1()->Put(to_array("key"), to_array("value"), log("Put"));
  Finish();

  Run(false, [this] { return page_client()->value("key") == "value"; });

  EXPECT_EQ(0, page_client()->conflict_count());
  EXPECT_EQ("value", page_client()->value("key"));
}

TEST_F(PageClientTest, PrefixWrite) {
  page2()->Put(to_array("a/key"), to_array("value"), log("Put"));
  page2()->Put(to_array("b/key"), to_array("value"), log("Put"));
  Finish();

  Run(false, [this] {
    return page_client_a()->value("a/key") == "value" &&
           page_client_b()->value("b/key") == "value";
  });

  EXPECT_EQ(0, page_client()->conflict_count());
  EXPECT_EQ(0, page_client_a()->conflict_count());
  EXPECT_EQ(0, page_client_b()->conflict_count());
  EXPECT_EQ("value", page_client_a()->value("a/key"));
  EXPECT_EQ("value", page_client_b()->value("b/key"));
}

TEST_F(PageClientTest, ConcurrentWrite) {
  page1()->Put(to_array("key1"), to_array("value1"), log("Put key1"));
  page2()->Put(to_array("key2"), to_array("value2"), log("Put key2"));
  Finish();

  Run(false, [this] {
    return page_client()->value("key1") == "value1" &&
           page_client()->value("key2") == "value2";
  });

  EXPECT_EQ(0, page_client()->conflict_count());
  EXPECT_EQ("value1", page_client()->value("key1"));
  EXPECT_EQ("value2", page_client()->value("key2"));
}

TEST_F(PageClientTest, ConflictWrite) {
  page2()->StartTransaction([this](ledger::Status status) {
    EXPECT_EQ(ledger::Status::OK, status);
    page2()->Put(
        to_array("key"), to_array("value2"), [this](ledger::Status status) {
          EXPECT_EQ(ledger::Status::OK, status);
          page1()->StartTransaction([this](ledger::Status status) {
            EXPECT_EQ(ledger::Status::OK, status);
            page1()->Put(to_array("key"), to_array("value1"),
                         [this](ledger::Status status) {
                           EXPECT_EQ(ledger::Status::OK, status);
                           page2()->Commit([this](ledger::Status status) {
                             EXPECT_EQ(ledger::Status::OK, status);
                             page1()->Commit([this](ledger::Status status) {
                               EXPECT_EQ(ledger::Status::OK, status);
                               Finish();
                             });
                           });
                         });
          });
        });
  });

  Run(true, [this] { return page_client()->value("key") == "value3"; });

  EXPECT_EQ(1, page_client()->conflict_count());
  EXPECT_EQ("value3", page_client()->value("key"));
}

TEST_F(PageClientTest, ConflictPrefixWrite) {
  page2()->StartTransaction([this](ledger::Status status) {
    EXPECT_EQ(ledger::Status::OK, status);
    page2()->Put(
        to_array("a/key"), to_array("value2"), [this](ledger::Status status) {
          EXPECT_EQ(ledger::Status::OK, status);
          page1()->StartTransaction([this](ledger::Status status) {
            EXPECT_EQ(ledger::Status::OK, status);
            page1()->Put(to_array("a/key"), to_array("value1"),
                         [this](ledger::Status status) {
                           EXPECT_EQ(ledger::Status::OK, status);
                           page2()->Commit([this](ledger::Status status) {
                             EXPECT_EQ(ledger::Status::OK, status);
                             page1()->Commit([this](ledger::Status status) {
                               EXPECT_EQ(ledger::Status::OK, status);
                               Finish();
                             });
                           });
                         });
          });
        });
  });

  Run(true, [this] { return page_client_a()->value("a/key") == "value3"; });

  EXPECT_EQ(1, page_client_a()->conflict_count());
  EXPECT_EQ(0, page_client_b()->conflict_count());
  EXPECT_EQ("value3", page_client_a()->value("a/key"));
}

TEST_F(PageClientTest, ConcurrentConflictWrite) {
  page2()->StartTransaction([this](ledger::Status status) {
    EXPECT_EQ(ledger::Status::OK, status);
    page2()->Put(to_array("key2"), to_array("value2"), log("Put 2 key2"));
    page2()->Put(
        to_array("key"), to_array("value2"), [this](ledger::Status status) {
          EXPECT_EQ(ledger::Status::OK, status);
          page1()->StartTransaction([this](ledger::Status status) {
            EXPECT_EQ(ledger::Status::OK, status);
            page1()->Put(to_array("key1"), to_array("value1"),
                         log("Put 1 key1"));
            page1()->Put(to_array("key"), to_array("value1"),
                         [this](ledger::Status status) {
                           EXPECT_EQ(ledger::Status::OK, status);
                           page2()->Commit([this](ledger::Status status) {
                             EXPECT_EQ(ledger::Status::OK, status);
                             page1()->Commit([this](ledger::Status status) {
                               EXPECT_EQ(ledger::Status::OK, status);
                               Finish();
                             });
                           });
                         });
          });
        });
  });

  Run(true, [this] {
    return page_client()->value("key") == "value3" &&
           page_client()->value("key1") == "value1" &&
           page_client()->value("key2") == "value2";
  });

  EXPECT_EQ(1, page_client()->conflict_count());
  EXPECT_EQ("value1", page_client()->value("key1"));
  EXPECT_EQ("value2", page_client()->value("key2"));
  EXPECT_EQ("value3", page_client()->value("key"));
}

}  // namespace
}  // namespace testing
}  // namespace modular
