// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/link_impl.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/story/fidl/link_change.fidl.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "lib/async/cpp/operation.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/storage.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/test_with_ledger.h"

namespace modular {

// Defined in incremental_link.cc.
void XdrLinkChange(XdrContext* const xdr, LinkChange* const data);

namespace {

LinkPathPtr GetTestLinkPath() {
  LinkPathPtr link_path = LinkPath::New();
  link_path->module_path.push_back("root");
  link_path->module_path.push_back("photos");
  link_path->link_name = "theLinkName";
  return link_path;
}

// TODO(mesch): Duplicated from ledger_client.cc.
bool HasPrefix(const std::string& value, const std::string& prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }

  for (size_t i = 0; i < prefix.size(); ++i) {
    if (value[i] != prefix[i]) {
      return false;
    }
  }

  return true;
}

class PageClientPeer : modular::PageClient {
 public:
  PageClientPeer(LedgerClient* const ledger_client,
                 LedgerPageId page_id,
                 std::string expected_prefix)
      : PageClient("PageClientPeer", ledger_client, std::move(page_id)),
        expected_prefix_(std::move(expected_prefix)) {}

  void OnPageChange(const std::string& key, const std::string& value) {
    EXPECT_TRUE(HasPrefix(key, expected_prefix_))
        << " key=" << key << " expected_prefix=" << expected_prefix_;
    changes.push_back(std::make_pair(key, value));
    EXPECT_TRUE(XdrRead(value, &last_change, XdrLinkChange))
        << key << " " << value;
    FXL_LOG(INFO) << "PageChange " << key << " = " << value;
  };

  std::vector<std::pair<std::string, std::string>> changes;
  LinkChangePtr last_change;

 private:
  std::string expected_prefix_;
};

class LinkImplTest : public testing::TestWithLedger, modular::LinkWatcher {
 public:
  LinkImplTest() : watcher_binding_(this) {}

  void SetUp() override {
    TestWithLedger::SetUp();

    OperationBase::set_observer([this](const char* const operation_name) {
      FXL_LOG(INFO) << "Operation " << operation_name;
      operations_[operation_name]++;
    });

    auto page_id = to_array("0123456789123456");
    auto link_path = GetTestLinkPath();

    link_impl_ = std::make_unique<LinkImpl>(ledger_client(), page_id.Clone(),
                                            link_path->Clone());

    link_impl_->Connect(link_.NewRequest());

    ledger_client_peer_ = ledger_client()->GetLedgerClientPeer();
    page_client_peer_ = std::make_unique<PageClientPeer>(
        ledger_client_peer_.get(), page_id.Clone(), MakeLinkKey(link_path));
  }

  void TearDown() override {
    if (watcher_binding_.is_bound()) {
      watcher_binding_.Close();
    }

    link_impl_.reset();
    link_.reset();

    page_client_peer_.reset();
    ledger_client_peer_.reset();

    OperationBase::set_observer(nullptr);

    TestWithLedger::TearDown();
  }

  int ledger_change_count() const { return page_client_peer_->changes.size(); }

  LinkChangePtr& last_change() { return page_client_peer_->last_change; }

  void ExpectOneCall(const std::string& operation_name) {
    EXPECT_EQ(1u, operations_.count(operation_name)) << operation_name;
    operations_.erase(operation_name);
  }

  void ExpectNoOtherCalls() {
    EXPECT_TRUE(operations_.empty());
    for (const auto& c : operations_) {
      FXL_LOG(INFO) << "    Unexpected call: " << c.first;
    }
  }

  void ClearCalls() { operations_.clear(); }

  void Notify(const fidl::String& json) override {
    step_++;
    last_json_notify_ = json;
    continue_();
  };

  std::unique_ptr<LinkImpl> link_impl_;
  LinkPtr link_;

  std::unique_ptr<LedgerClient> ledger_client_peer_;
  std::unique_ptr<PageClientPeer> page_client_peer_;

  fidl::Binding<modular::LinkWatcher> watcher_binding_;
  int step_{};
  std::string last_json_notify_;
  std::function<void()> continue_;

  std::map<std::string, int> operations_;
};

TEST_F(LinkImplTest, Constructor) {
  bool finished{};
  continue_ = [this, &finished] { finished = true; };

  link_->WatchAll(watcher_binding_.NewBinding());

  EXPECT_TRUE(RunLoopUntil([&finished] { return finished; }));
  EXPECT_EQ("null", last_json_notify_);
  ExpectOneCall("LinkImpl::ReloadCall");
  ExpectOneCall("ReadAllDataCall");
  ExpectOneCall("LinkImpl::WatchCall");
  ExpectNoOtherCalls();
}

TEST_F(LinkImplTest, Set) {
  continue_ = [this] { EXPECT_TRUE(step_ <= 2); };

  link_->WatchAll(watcher_binding_.NewBinding());
  link_->Set(nullptr, "{ \"value\": 7 }");

  EXPECT_TRUE(RunLoopUntil([this] { return ledger_change_count() == 1; }));
  // Calls from constructor and setup.
  ExpectOneCall("LinkImpl::ReloadCall");
  ExpectOneCall("ReadAllDataCall");
  ExpectOneCall("LinkImpl::WatchCall");
  // Calls from Set().
  ExpectOneCall("LinkImpl::IncrementalChangeCall");
  ExpectOneCall("LinkImpl::IncrementalWriteCall");
  ExpectOneCall("WriteDataCall");
  ExpectNoOtherCalls();
  EXPECT_EQ("{\"value\":7}", last_json_notify_);
}

TEST_F(LinkImplTest, Update) {
  continue_ = [this] { EXPECT_TRUE(step_ <= 3); };

  link_->WatchAll(watcher_binding_.NewBinding());

  link_->Set(nullptr, "{ \"value\": 8 }");
  link_->UpdateObject(nullptr, "{ \"value\": 50 }");

  EXPECT_TRUE(RunLoopUntil([this] { return ledger_change_count() == 2; }));
  EXPECT_EQ("{\"value\":50}", last_change()->json);
  EXPECT_EQ("{\"value\":50}", last_json_notify_);
}

TEST_F(LinkImplTest, UpdateNewKey) {
  continue_ = [this] { EXPECT_TRUE(step_ <= 3); };

  link_->WatchAll(watcher_binding_.NewBinding());

  link_->Set(nullptr, "{ \"value\": 9 }");
  link_->UpdateObject(nullptr, "{ \"century\": 100 }");

  EXPECT_TRUE(RunLoopUntil([this] { return ledger_change_count() == 2; }));
  EXPECT_EQ("{\"century\":100}", last_change()->json);
  EXPECT_EQ("{\"value\":9,\"century\":100}", last_json_notify_);
}

TEST_F(LinkImplTest, Erase) {
  continue_ = [this] { EXPECT_TRUE(step_ <= 3); };

  link_->WatchAll(watcher_binding_.NewBinding());

  link_->Set(nullptr, "{ \"value\": 4 }");

  std::vector<std::string> segments{"value"};
  link_->Erase(fidl::Array<fidl::String>::From(segments));

  EXPECT_TRUE(RunLoopUntil([this] { return ledger_change_count() == 2; }));
  EXPECT_TRUE(last_change()->json.is_null());
  EXPECT_EQ("{}", last_json_notify_);
}

// TODO(jimbe) Still many tests to be written, including:
//
// * testing that setting a schema prevents WriteLinkData from being called if
//   the json is bad,
//
// * Specific behavior of LinkWatcher notification (Watch() not called for own
//   changes, Watch() and WatchAll() only called for changes that really occur,
//   and only once.

}  // namespace
}  // namespace modular
