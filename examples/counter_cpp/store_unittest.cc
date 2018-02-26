// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/examples/counter_cpp/store.h"

#include <string>

#include "garnet/lib/gtest/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/story/fidl/link.fidl.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/mock_base.h"

namespace modular {
namespace {

class LinkMockBase : protected Link, public testing::MockBase {
 public:
  LinkMockBase() = default;

  ~LinkMockBase() override = default;

  void SetSchema(const f1dl::String& /*json_schema*/) override {
    ++counts["SetSchema"];
  }

  void Get(f1dl::Array<f1dl::String> /*path*/,
           const GetCallback& /*callback*/) override {
    ++counts["Get"];
  }

  void Set(f1dl::Array<f1dl::String> /*path*/,
           const f1dl::String& /*json*/) override {
    ++counts["Set"];
  }

  void UpdateObject(f1dl::Array<f1dl::String> /*path*/,
                    const f1dl::String& /*json*/) override {
    ++counts["UpdateObject"];
  }

  void Erase(f1dl::Array<f1dl::String> /*path*/) override { ++counts["Erase"]; }

  void GetEntity(const Link::GetEntityCallback& callback) override {
    ++counts["GetEntity"];
    callback("");
  }

  void SetEntity(const f1dl::String& /*entity_reference*/) override {
    ++counts["SetEntity"];
  }

  void Watch(f1dl::InterfaceHandle<LinkWatcher> /*watcher_handle*/) override {
    ++counts["Watch"];
  }

  void WatchAll(f1dl::InterfaceHandle<LinkWatcher> /*watcher*/) override {
    ++counts["WatchAll"];
  }

  void Sync(const SyncCallback& /*callback*/) override { ++counts["Sync"]; }
};

class LinkMock : public LinkMockBase {
 public:
  LinkMock() : binding_(this) {}

  void Bind(f1dl::InterfaceRequest<Link> request) {
    binding_.Bind(std::move(request));
  }

  void Watch(f1dl::InterfaceHandle<LinkWatcher> watcher_handle) override {
    watcher.Bind(std::move(watcher_handle));
    LinkMockBase::Watch(std::move(watcher_handle));
  }

  void UpdateObject(f1dl::Array<f1dl::String> path,
                    const f1dl::String& json) override {
    LinkMockBase::UpdateObject(std::move(path), json);
    fsl::MessageLoop::GetCurrent()->QuitNow();
  }

  f1dl::InterfacePtr<modular::LinkWatcher> watcher;

 private:
  f1dl::Binding<Link> binding_;
};

const std::string module_name{"store_unittest"};

TEST(Counter, Constructor_Invalid) {
  modular_example::Counter counter;
  EXPECT_FALSE(counter.is_valid());
}

TEST(Counter, ToDocument_Success) {
  modular_example::Counter counter;
  counter.counter = 3;
  EXPECT_TRUE(counter.is_valid());

  rapidjson::Document doc = counter.ToDocument(module_name);
  std::string json = JsonValueToString(doc);
  EXPECT_EQ(
      "{\"http://schema.domokit.org/counter\":3,"
      "\"http://schema.org/sender\":\"store_unittest\"}",
      json);
}

class StoreTest : public gtest::TestWithMessageLoop {};

TEST_F(StoreTest, Store_ModelChanged) {
  LinkMock link_mock;
  modular::LinkPtr link;
  link_mock.Bind(link.NewRequest());

  modular_example::Store store{module_name};
  store.Initialize(link.Unbind());
  store.counter.sender = module_name;
  store.counter.counter = 3;

  link_mock.ExpectNoOtherCalls();

  store.MarkDirty();
  store.ModelChanged();

  EXPECT_FALSE(RunLoopWithTimeout());

  // Initialize() calls Watch()
  // and ModelChanged calls UpdateObject()
  link_mock.ExpectCalledOnce("Watch");
  link_mock.ExpectCalledOnce("UpdateObject");
  link_mock.ExpectNoOtherCalls();
}

}  // namespace
}  // namespace modular
