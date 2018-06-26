// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/story_runner/link_impl.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/async/cpp/operation.h"
#include "lib/fidl/cpp/array.h"
#include "peridot/bin/user_runner/storage/constants_and_utils.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/test_with_ledger.h"
#include "peridot/public/lib/entity/cpp/json.h"

using fuchsia::modular::CreateLinkInfo;
using fuchsia::modular::CreateLinkInfoPtr;
using fuchsia::modular::Link;
using fuchsia::modular::LinkPath;
using fuchsia::modular::LinkPtr;
using fuchsia::modular::LinkWatcher;
using fuchsia::modular::LinkWatcherPtr;

namespace modular {
namespace {

class TestLinkWatcher : public LinkWatcher {
 public:
  TestLinkWatcher(std::function<void(fidl::StringPtr)> fn) : fn_(fn) {}

 private:
  void Notify(fidl::StringPtr json) override { fn_(json); }

  std::function<void(fidl::StringPtr)> fn_;
};

class LinkImplTest : public testing::TestWithLedger {
 public:
  LinkPtr MakeLink(std::string ledger_page, std::string name,
                   std::string initial_data = "") {
    auto page_id = MakePageId(ledger_page);
    LinkPath link_path;
    link_path.link_name = name;

    CreateLinkInfoPtr create_link_info;
    if (!initial_data.empty()) {
      create_link_info = CreateLinkInfo::New();
      create_link_info->initial_data = initial_data;
      // |create_link_info->allowed_types| is already null.
    }

    LinkPtr ptr;
    auto impl =
        std::make_unique<LinkImpl>(ledger_client(), CloneStruct(page_id),
                                   link_path, std::move(create_link_info));
    impl->Connect(ptr.NewRequest());
    impls_.push_back(std::move(impl));

    return ptr;
  }

  void Watch(Link* link, std::function<void(fidl::StringPtr)> fn) {
    auto watcher = std::make_unique<TestLinkWatcher>(fn);
    LinkWatcherPtr ptr;
    watchers_.AddBinding(std::move(watcher), ptr.NewRequest());
    link->Watch(std::move(ptr));
  }

  void WatchAll(Link* link, std::function<void(fidl::StringPtr)> fn) {
    auto watcher = std::make_unique<TestLinkWatcher>(fn);
    LinkWatcherPtr ptr;
    watchers_.AddBinding(std::move(watcher), ptr.NewRequest());
    link->WatchAll(std::move(ptr));
  }

  std::vector<std::unique_ptr<LinkImpl>> impls_;
  fidl::BindingSet<LinkWatcher, std::unique_ptr<TestLinkWatcher>> watchers_;
};

TEST_F(LinkImplTest, WatchDefaultBehavior) {
  // When we ask to watch a link, we should be notified immediately (on the
  // next iteration of the event loop) of its current value.
  auto link = MakeLink("page", "mylink");

  int notified_count{0};
  Watch(link.get(), [&](const fidl::StringPtr& value) {
    ++notified_count;
    EXPECT_EQ("null", value);
  });
  // We are only notified after the event loop runs.
  EXPECT_EQ(0, notified_count);
  EXPECT_TRUE(RunLoopUntilWithTimeout([&] { return notified_count == 1; }));

  // Same for WatchAll.
  WatchAll(link.get(), [&](const fidl::StringPtr& value) {
    ++notified_count;
    EXPECT_EQ("null", value);
  });
  EXPECT_TRUE(RunLoopUntilWithTimeout([&] { return notified_count == 2; }));
}

TEST_F(LinkImplTest, InitialValue) {
  // When constructing a LinkImpl with an initial value, we should see it
  // populated, but only the first time: if the link has a value already
  // in the ledger, we should not change it.
  auto link = MakeLink("page", "mylink", "1337");

  int notified_count{0};
  Watch(link.get(), [&](const fidl::StringPtr& value) {
    ++notified_count;
    EXPECT_EQ("1337", value);
  });
  EXPECT_TRUE(RunLoopUntilWithTimeout([&] { return notified_count == 1; }));

  // We don't expect to see a change to "10".
  auto same_link = MakeLink("page", "mylink", "10");
  bool same_link_got_value{};
  Watch(same_link.get(), [&](const fidl::StringPtr& value) {
    same_link_got_value = true;
    EXPECT_EQ("1337", value);
  });
  EXPECT_TRUE(RunLoopUntilWithTimeout([&] { return same_link_got_value; }));
}

TEST_F(LinkImplTest, SetAndWatch) {
  auto link = MakeLink("page", "mylink");

  // Watch for our own changes, which we shouldn't see. The initial value
  // ("null"), is sent immediately, though.
  Watch(link.get(),
        [&](const fidl::StringPtr& value) { EXPECT_EQ("null", value); });

  // Also use WatchAll(), on which we should see our own changes.
  fidl::StringPtr notified_value;
  int notified_count{0};
  WatchAll(link.get(), [&](const fidl::StringPtr& value) {
    notified_value = value;
    ++notified_count;
  });

  link->Set(nullptr, "42");

  bool synced{};
  link->Sync([&synced] { synced = true; });
  EXPECT_TRUE(RunLoopUntilWithTimeout([&synced] { return synced; }));

  EXPECT_EQ(2, notified_count);
  EXPECT_EQ("42", notified_value);
}

TEST_F(LinkImplTest, SetAndWatchAndGet) {
  auto link = MakeLink("page", "mylink");

  fidl::StringPtr notified_value;
  int notified_count{0};
  WatchAll(link.get(), [&](const fidl::StringPtr& value) {
    notified_value = value;
    ++notified_count;
  });

  link->Set(nullptr, R"({
    "one": 1,
    "two": 2
  })");
  fidl::VectorPtr<fidl::StringPtr> path;
  path->push_back("two");
  link->Set(std::move(path), R"("two")");

  path->clear();
  path->push_back("three");
  link->Set(std::move(path), R"(3)");

  bool synced{};
  link->Sync([&synced] { synced = true; });
  EXPECT_TRUE(RunLoopUntilWithTimeout([&] { return synced; }));

  const std::string expected_value = R"({"one":1,"two":"two","three":3})";
  EXPECT_EQ(4, notified_count);  // initial, 3x Set
  EXPECT_EQ(expected_value, notified_value);

  bool get_done{};
  link->Get(nullptr /* path */, [&](fidl::StringPtr value) {
    get_done = true;
    EXPECT_EQ(expected_value, value);
  });
  EXPECT_TRUE(RunLoopUntilWithTimeout([&] { return get_done; }));
}

TEST_F(LinkImplTest, Erase) {
  auto link = MakeLink("page", "mylink");

  link->Set(nullptr, R"({
    "one": 1,
    "two": 2
  })");
  fidl::VectorPtr<fidl::StringPtr> path;
  path.push_back("two");
  link->Erase(std::move(path));

  const std::string expected_value = R"({"one":1})";
  bool get_done{};
  link->Get(nullptr /* path */, [&](fidl::StringPtr value) {
    get_done = true;
    EXPECT_EQ(expected_value, value);
  });
  EXPECT_TRUE(RunLoopUntilWithTimeout([&] { return get_done; }));
}

TEST_F(LinkImplTest, SetAndGetEntity) {
  auto link = MakeLink("page", "mylink");

  link->SetEntity("ref");
  bool done{};
  link->GetEntity([&](const fidl::StringPtr value) {
    EXPECT_EQ("ref", value);
    done = true;
  });
  EXPECT_TRUE(RunLoopUntilWithTimeout([&] { return done; }));
}

}  // namespace
}  // namespace modular
