// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/link_impl.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/story/fidl/link_change.fidl.h"
#include "peridot/bin/story_runner/story_storage_impl.h"
#include "peridot/lib/ledger/storage.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/mock_base.h"
#include "peridot/lib/testing/test_with_message_loop.h"

namespace modular {
namespace {

// LinkStorage is not a fidl interface.
class LinkStorageMock : LinkStorage, public testing::MockBase {
 public:
  LinkStorageMock() = default;
  ~LinkStorageMock() override = default;

  const LinkChangePtr& write_link_change() const {
    return changes_.rbegin()->second;
  }

  const std::string& write_link_path() const { return write_link_path_; }

  const std::string& read_link_path() const { return read_link_path_; }

  LinkStorage* interface() { return this; }

 private:
  // Sends back whatever we most recently wrote
  void ReadLinkData(const LinkPathPtr& link_path,
                    const DataCallback& callback) override {
    ++counts["ReadLinkData"];
    read_link_path_ = EncodeLinkPath(link_path);
    callback(write_link_change()->json);
  }

  void ReadAllLinkData(const LinkPathPtr& link_path,
                       const AllLinkChangeCallback& callback) override {
    ++counts["ReadAllLinkData"];
    read_link_path_ = EncodeLinkPath(link_path);
    auto arr = fidl::Array<LinkChangePtr>::New(0);
    for (auto& c : changes_) {
      arr.push_back(c.second.Clone());
    }

    callback(std::move(arr));
  }

  void WriteLinkData(const LinkPathPtr& link_path,
                     const fidl::String& data,
                     const SyncCallback& callback) override {
    ++counts["WriteLinkData"];
    write_link_path_ = EncodeLinkPath(link_path);
    callback();
  }

  void WriteIncrementalLinkData(const LinkPathPtr& link_path,
                                fidl::String key,
                                LinkChangePtr link_change,
                                const SyncCallback& callback) override {
    ++counts["WriteIncrementalLinkData"];
    write_link_path_ = EncodeLinkPath(link_path);
    changes_[key] = std::move(link_change);

    callback();
  }

  void FlushWatchers(const SyncCallback& callback) override {
    ++counts["FlushWatchers"];
    callback();
  }

  void WatchLink(const LinkPathPtr& /*link_path*/,
                 LinkImpl* /*impl*/,
                 const DataCallback& /*watcher*/) override {
    ++counts["WatchLink"];
  }

  void DropWatcher(LinkImpl* /*impl*/) override { ++counts["DropWatcher"]; }

  void Sync(const SyncCallback& /*callback*/) override { ++counts["Sync"]; }

 private:
  std::string read_link_path_;
  std::string write_link_path_;
  std::map<std::string, LinkChangePtr> changes_;
};

LinkPathPtr GetTestLinkPath() {
  LinkPathPtr link_path = LinkPath::New();
  link_path->module_path.push_back("root");
  link_path->module_path.push_back("photos");
  link_path->link_name = "theLinkName";
  return link_path;
}

constexpr char kPrettyTestLinkPath[] = "root:photos/theLinkName";

class LinkImplTest : public testing::TestWithMessageLoop, modular::LinkWatcher {
 public:
  LinkImplTest() : binding_(this) { link_impl_->Connect(std::move(request_)); }

  ~LinkImplTest() {
    if (binding_.is_bound()) {
      binding_.Close();  // Disconnect from Watch()
    }
  }

  virtual void Notify(const fidl::String& json) {
    last_json_notify_ = json;
    continue_();
  };

  int step_{};
  std::string last_json_notify_;
  std::function<void()> continue_;
  LinkPathPtr link_path = GetTestLinkPath();
  LinkStorageMock storage_mock;
  LinkPtr link_ptr_;
  fidl::InterfaceRequest<Link> request_ = link_ptr_.NewRequest();
  std::unique_ptr<LinkImpl> link_impl_ =
      std::make_unique<LinkImpl>(storage_mock.interface(),
                                 std::move(link_path));
  fidl::Binding<modular::LinkWatcher> binding_;
};

TEST_F(LinkImplTest, Constructor) {
  bool finished{};
  continue_ = [this, &finished] {
    EXPECT_EQ("null", last_json_notify_);
    EXPECT_EQ(kPrettyTestLinkPath, storage_mock.read_link_path());
    storage_mock.ExpectCalledOnce("ReadAllLinkData");
    storage_mock.ExpectCalledOnce("WatchLink");
    storage_mock.ExpectNoOtherCalls();

    binding_.Close();  // Disconnect from Watch()
    link_impl_.reset();
    storage_mock.ExpectCalledOnce("DropWatcher");
    storage_mock.ExpectNoOtherCalls();
    finished = true;
  };

  link_ptr_->WatchAll(binding_.NewBinding());

  RunLoopUntil([&finished] { return finished; });
  ASSERT_TRUE(finished);
  ASSERT_FALSE(binding_.is_bound());
}

TEST_F(LinkImplTest, Set) {
  continue_ = [this] {
    switch (++step_) {
      case 1:
        storage_mock.ExpectCalledOnce("ReadAllLinkData");
        storage_mock.ExpectCalledOnce("WatchLink");
        storage_mock.ExpectNoOtherCalls();
        break;
      case 2:
        storage_mock.ExpectCalledOnce("WriteIncrementalLinkData");
        storage_mock.ExpectNoOtherCalls();

        EXPECT_NE(nullptr, storage_mock.write_link_change().get());
        EXPECT_EQ(kPrettyTestLinkPath, storage_mock.write_link_path());
        EXPECT_EQ("{\"value\":7}", last_json_notify_);
        break;
      default:
        EXPECT_TRUE(step_ <= 2);
        break;
    }
  };

  link_ptr_->WatchAll(binding_.NewBinding());

  link_ptr_->Set(nullptr, "{ \"value\": 7 }");

  RunLoopUntil([this] { return step_ == 2; });
  ASSERT_EQ(2, step_);
}

TEST_F(LinkImplTest, Update) {
  continue_ = [this] {
    switch (++step_) {
      case 1:
      case 2:
        storage_mock.ClearCalls();
        break;
      case 3:
        EXPECT_EQ(kPrettyTestLinkPath, storage_mock.write_link_path());
        EXPECT_EQ("{\"value\":50}", storage_mock.write_link_change()->json);
        break;
      default:
        EXPECT_TRUE(step_ <= 3);
        break;
    }
  };

  link_ptr_->WatchAll(binding_.NewBinding());

  link_ptr_->Set(nullptr, "{ \"value\": 8 }");

  link_ptr_->UpdateObject(nullptr, "{ \"value\": 50 }");

  RunLoopUntil([this] { return step_ == 3; });
  ASSERT_EQ(3, step_);
}

TEST_F(LinkImplTest, UpdateNewKey) {
  continue_ = [this] {
    switch (++step_) {
      case 1:
      case 2:
        storage_mock.ClearCalls();
        break;
      case 3:
        EXPECT_EQ(kPrettyTestLinkPath, storage_mock.write_link_path());
        EXPECT_EQ("{\"value\":9,\"century\":100}", last_json_notify_);
        break;
      default:
        EXPECT_TRUE(step_ <= 3);
        break;
    }
  };

  link_ptr_->WatchAll(binding_.NewBinding());

  link_ptr_->Set(nullptr, "{ \"value\": 9 }");

  link_ptr_->UpdateObject(nullptr, "{ \"century\": 100 }");

  RunLoopUntil([this] { return step_ == 3; });
  ASSERT_EQ(3, step_);
}

TEST_F(LinkImplTest, Erase) {
  continue_ = [this] {
    switch (++step_) {
      case 1:
      case 2:
        storage_mock.ClearCalls();
        break;
      case 3:
        EXPECT_TRUE(storage_mock.write_link_change()->json.is_null());
        EXPECT_EQ("{}", last_json_notify_);
        break;
      default:
        EXPECT_TRUE(step_ <= 3);
        break;
    }
  };

  link_ptr_->WatchAll(binding_.NewBinding());

  link_ptr_->Set(nullptr, "{ \"value\": 4 }");

  std::vector<std::string> segments{"value"};
  link_ptr_->Erase(fidl::Array<fidl::String>::From(segments));

  RunLoopUntil([this] { return step_ == 3; });
  ASSERT_EQ(3, step_);
}

// TODO(jimbe) Still many tests to be written, including testing that setting
// a schema prevents WriteLinkData from being called if the json is bad.

}  // namespace
}  // namespace modular
