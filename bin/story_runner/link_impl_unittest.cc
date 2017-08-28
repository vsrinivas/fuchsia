// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/link_impl.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/lib/testing/mock_base.h"
#include "apps/modular/src/story_runner/story_storage_impl.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/array.h"

namespace modular {
namespace {

// LinkStorage is not a fidl interface.
class LinkStorageMock : LinkStorage, public testing::MockBase {
 public:
  LinkStorageMock() = default;
  ~LinkStorageMock() override = default;

  const std::string& write_data() const { return write_data_; }

  const std::string& write_link_path() const { return write_link_path_; }

  const std::string& read_link_path() const { return read_link_path_; }

  LinkStorage* interface() { return this; }

 private:
  // Sends back whatever we most recently wrote
  void ReadLinkData(const LinkPathPtr& link_path,
                    const DataCallback& callback) override {
    ++counts["ReadLinkData"];
    read_link_path_ = EncodeLinkPath(link_path);
    callback(write_data_);
  }

  void WriteLinkData(const LinkPathPtr& link_path,
                     const fidl::String& data,
                     const SyncCallback& callback) override {
    ++counts["WriteLinkData"];
    write_data_ = data;
    write_link_path_ = EncodeLinkPath(link_path);
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
  std::string write_data_;
};

LinkPathPtr GetTestLinkPath() {
  LinkPathPtr link_path = LinkPath::New();
  link_path->module_path.push_back("root");
  link_path->module_path.push_back("photos");
  link_path->link_name = "theLinkName";
  return link_path;
}

constexpr char kPrettyTestLinkPath[] = "root:photos/theLinkName";

TEST(LinkImpl, Constructor_Success) {
  LinkPathPtr link_path = GetTestLinkPath();
  LinkStorageMock storage_mock;

  {
    LinkImpl link_impl(storage_mock.interface(), std::move(link_path));
    EXPECT_EQ(kPrettyTestLinkPath, storage_mock.read_link_path());
    storage_mock.ExpectCalledOnce("ReadLinkData");
    storage_mock.ExpectCalledOnce("WatchLink");
    storage_mock.ExpectNoOtherCalls();
  }
  storage_mock.ExpectCalledOnce("DropWatcher");
  storage_mock.ExpectNoOtherCalls();
}

TEST(LinkImpl, Set_Success) {
  LinkPathPtr link_path = GetTestLinkPath();
  LinkStorageMock storage_mock;
  LinkImpl link_impl(storage_mock.interface(), std::move(link_path));
  storage_mock.ClearCalls();

  link_impl.Set(nullptr, "{ \"value\": 7 }", 2);

  EXPECT_EQ(kPrettyTestLinkPath, storage_mock.write_link_path());
  EXPECT_EQ("{\"value\":7}", storage_mock.write_data());
  storage_mock.ExpectCalledOnce("WriteLinkData");
  storage_mock.ExpectCalledOnce("FlushWatchers");
  storage_mock.ExpectNoOtherCalls();
}

TEST(LinkImpl, Update_Success) {
  LinkPathPtr link_path = GetTestLinkPath();
  LinkStorageMock storage_mock;
  LinkImpl link_impl(storage_mock.interface(), std::move(link_path));

  link_impl.Set(nullptr, "{ \"value\": 7 }", 2);
  storage_mock.ClearCalls();

  link_impl.UpdateObject(nullptr, "{ \"value\": 50 }", 2);

  EXPECT_EQ(kPrettyTestLinkPath, storage_mock.write_link_path());
  EXPECT_EQ("{\"value\":50}", storage_mock.write_data());
  storage_mock.ExpectCalledOnce("WriteLinkData");
  storage_mock.ExpectCalledOnce("FlushWatchers");
  storage_mock.ExpectNoOtherCalls();
}

TEST(LinkImpl, UpdateNewKey_Success) {
  LinkPathPtr link_path = GetTestLinkPath();
  LinkStorageMock storage_mock;
  LinkImpl link_impl(storage_mock.interface(), std::move(link_path));

  link_impl.Set(nullptr, "{ \"value\": 7 }", 2);
  storage_mock.ClearCalls();

  link_impl.UpdateObject(nullptr, "{ \"century\": 100 }", 2);

  EXPECT_EQ(kPrettyTestLinkPath, storage_mock.write_link_path());
  EXPECT_EQ("{\"value\":7,\"century\":100}", storage_mock.write_data());
  storage_mock.ExpectCalledOnce("WriteLinkData");
  storage_mock.ExpectCalledOnce("FlushWatchers");
  storage_mock.ExpectNoOtherCalls();
}

TEST(LinkImpl, Erase_Success) {
  LinkPathPtr link_path = GetTestLinkPath();
  LinkStorageMock storage_mock;
  LinkImpl link_impl(storage_mock.interface(), std::move(link_path));

  link_impl.Set(nullptr, "{ \"value\": 7 }", 2);
  storage_mock.ClearCalls();

  std::vector<std::string> segments{"value"};
  link_impl.Erase(fidl::Array<fidl::String>::From(segments), 2);

  EXPECT_EQ("{}", storage_mock.write_data());
  storage_mock.ExpectCalledOnce("WriteLinkData");
  storage_mock.ExpectCalledOnce("FlushWatchers");
  storage_mock.ExpectNoOtherCalls();
}

// TODO(jimbe) Still many tests to be written, including testing that setting
// a schema prevents WriteLinkData from being called if the json is bad.

}  // namespace
}  // namespace modular
