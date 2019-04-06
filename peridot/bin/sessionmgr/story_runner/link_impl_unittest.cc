// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story_runner/link_impl.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include <lib/entity/cpp/json.h>
#include <lib/fsl/vmo/strings.h>

#include "gtest/gtest.h"
#include "peridot/bin/sessionmgr/storage/constants_and_utils.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/test_with_ledger.h"

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
  TestLinkWatcher(fit::function<void(fidl::StringPtr)> fn)
      : fn_(std::move(fn)) {}

 private:
  void Notify(fuchsia::mem::Buffer json) override {
    std::string json_string;
    FXL_CHECK(fsl::StringFromVmo(json, &json_string));
    fn_(json_string);
  }

  fit::function<void(fidl::StringPtr)> fn_;
};

class LinkImplTest : public testing::TestWithLedger {
 public:
  std::unique_ptr<StoryStorage> MakeStorage(std::string ledger_page) {
    auto page_id = MakePageId(ledger_page);
    return std::make_unique<StoryStorage>(ledger_client(), page_id);
  }

  LinkPtr MakeLink(StoryStorage* const storage, std::string name) {
    LinkPath link_path;
    link_path.link_name = name;

    LinkPtr ptr;
    auto impl = std::make_unique<LinkImpl>(storage, std::move(link_path));
    links_.AddBinding(std::move(impl), ptr.NewRequest());
    return ptr;
  }

  void Watch(Link* link, fit::function<void(fidl::StringPtr)> fn) {
    auto watcher = std::make_unique<TestLinkWatcher>(std::move(fn));
    LinkWatcherPtr ptr;
    watchers_.AddBinding(std::move(watcher), ptr.NewRequest());
    link->Watch(std::move(ptr));
  }

  void WatchAll(Link* link, fit::function<void(fidl::StringPtr)> fn) {
    auto watcher = std::make_unique<TestLinkWatcher>(std::move(fn));
    LinkWatcherPtr ptr;
    watchers_.AddBinding(std::move(watcher), ptr.NewRequest());
    link->WatchAll(std::move(ptr));
  }

  void SetLink(Link* link, std::vector<std::string> path,
               const std::string& value) {
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(value, &vmo));
    link->Set(fidl::VectorPtr(std::move(path)), std::move(vmo).ToTransport());
  }

  fidl::BindingSet<Link, std::unique_ptr<LinkImpl>> links_;
  fidl::BindingSet<LinkWatcher, std::unique_ptr<TestLinkWatcher>> watchers_;
};

TEST_F(LinkImplTest, GetNull) {
  auto storage = MakeStorage("page");
  auto link = MakeLink(storage.get(), "foo");

  bool get_done{};
  link->Get(nullptr /* path */,
            [&](std::unique_ptr<fuchsia::mem::Buffer> value) {
              std::string content_string;
              FXL_CHECK(fsl::StringFromVmo(*value, &content_string));
              get_done = true;
              EXPECT_EQ("null", content_string);
            });
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return get_done; }));

  get_done = false;
  fidl::VectorPtr<std::string> path;
  path->push_back("one");
  link->Get(std::move(path), [&](std::unique_ptr<fuchsia::mem::Buffer> value) {
    std::string content_string;
    FXL_CHECK(fsl::StringFromVmo(*value, &content_string));
    get_done = true;
    EXPECT_EQ("null", content_string);
  });
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return get_done; }));
}

TEST_F(LinkImplTest, WatchDefaultBehavior) {
  // When we ask to watch a link, we should be notified immediately (on the
  // next iteration of the event loop) of its current value.
  auto storage = MakeStorage("page");
  auto link = MakeLink(storage.get(), "mylink");

  int notified_count{0};
  Watch(link.get(), [&](const fidl::StringPtr& value) {
    ++notified_count;
    EXPECT_EQ("null", value);
  });
  // We are only notified after the event loop runs.
  EXPECT_EQ(0, notified_count);
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return notified_count == 1; }));

  // Same for WatchAll.
  WatchAll(link.get(), [&](const fidl::StringPtr& value) {
    ++notified_count;
    EXPECT_EQ("null", value);
  });
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return notified_count == 2; }));
}

TEST_F(LinkImplTest, SetAndWatch) {
  auto storage = MakeStorage("page");
  auto link = MakeLink(storage.get(), "mylink");

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

  SetLink(link.get(), {}, "42");

  bool synced{};
  link->Sync([&synced] { synced = true; });
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&synced] { return synced; }));

  EXPECT_EQ(2, notified_count);
  EXPECT_EQ("42", notified_value);
}

TEST_F(LinkImplTest, SetAndWatchAndGet) {
  auto storage = MakeStorage("page");
  auto link = MakeLink(storage.get(), "mylink");

  fidl::StringPtr notified_value;
  int notified_count{0};
  WatchAll(link.get(), [&](const fidl::StringPtr& value) {
    notified_value = value;
    ++notified_count;
  });

  SetLink(link.get(), {}, R"({
    "one": 1,
    "two": 2
  })");
  fidl::VectorPtr<std::string> path;
  path->push_back("two");
  SetLink(link.get(), std::move(path), R"("two")");

  path->clear();
  path->push_back("three");
  SetLink(link.get(), std::move(path), R"(3)");

  bool synced{};
  link->Sync([&synced] { synced = true; });
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return synced; }));

  const std::string expected_value = R"({"one":1,"two":"two","three":3})";
  EXPECT_EQ(4, notified_count);  // initial, 3x Set
  EXPECT_EQ(expected_value, notified_value);

  bool get_done{};
  link->Get(nullptr /* path */,
            [&](std::unique_ptr<fuchsia::mem::Buffer> value) {
              std::string content_string;
              FXL_CHECK(fsl::StringFromVmo(*value, &content_string));
              get_done = true;
              EXPECT_EQ(expected_value, content_string);
            });
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return get_done; }));
}

TEST_F(LinkImplTest, SetNonJsonAndGetJsonPointer) {
  auto storage = MakeStorage("page");
  auto link = MakeLink(storage.get(), "mylink");

  SetLink(link.get(), {}, R"({
    "one": 1,
    "two": 2invalidjson
  })");

  bool synced{};
  link->Sync([&synced] { synced = true; });
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return synced; }));

  fidl::VectorPtr<std::string> path;
  path->push_back("one");
  bool get_done{};
  link->Get(std::move(path), [&](std::unique_ptr<fuchsia::mem::Buffer> value) {
    std::string content_string;
    FXL_CHECK(fsl::StringFromVmo(*value, &content_string));
    get_done = true;
    EXPECT_EQ("null", content_string);
  });
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return get_done; }));
}

TEST_F(LinkImplTest, Erase) {
  auto storage = MakeStorage("page");
  auto link = MakeLink(storage.get(), "mylink");

  SetLink(link.get(), {}, R"({
    "one": 1,
    "two": 2
  })");
  std::vector<std::string> path;
  path.push_back("two");
  link->Erase(std::move(path));

  const std::string expected_value = R"({"one":1})";
  bool get_done{};
  link->Get(nullptr /* path */,
            [&](std::unique_ptr<fuchsia::mem::Buffer> value) {
              std::string content_string;
              FXL_CHECK(fsl::StringFromVmo(*value, &content_string));
              get_done = true;
              EXPECT_EQ(expected_value, content_string);
            });
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return get_done; }));
}

TEST_F(LinkImplTest, SetAndGetEntity) {
  auto storage = MakeStorage("page");
  auto link = MakeLink(storage.get(), "mylink");

  link->SetEntity("ref");
  bool done{};
  link->GetEntity([&](const fidl::StringPtr value) {
    EXPECT_EQ("ref", value);
    done = true;
  });
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return done; }));
}

TEST_F(LinkImplTest, MultipleConnections) {
  // Create two storage instances, where each one gets its own connection to
  // the Ledger. Then, create a LinkImpl on top of each, and test end-to-end.
  auto storage1 = MakeStorage("page");
  auto link1 = MakeLink(storage1.get(), "mylink");

  auto storage2 = MakeStorage("page");
  auto link2 = MakeLink(storage2.get(), "mylink");

  int notified_count{0};
  fidl::StringPtr last_value;
  WatchAll(link1.get(), [&](const fidl::StringPtr& value) {
    ++notified_count;
    last_value = value;
  });

  // Set two values on |link2|. On |link1|, we are guaranteed to get a
  // notification about the second value, eventually.
  SetLink(link2.get(), {}, "3");
  SetLink(link2.get(), {}, "4");
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return last_value == "4"; }));
  // There is always an initial notification of the current state, so we
  // will have been notified either 2 or 3 times: 2 if we only got a
  // notification about the set to "4", and 3 if we got both the set to "3" and
  // "4".
  EXPECT_TRUE(notified_count == 2 || notified_count == 3) << notified_count;
}

}  // namespace
}  // namespace modular
