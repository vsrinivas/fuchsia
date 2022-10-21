// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/vfs/cpp/internal/directory_connection.h>
#include <lib/vfs/cpp/internal/file.h>
#include <lib/vfs/cpp/internal/file_connection.h>
#include <lib/vfs/cpp/internal/node.h>
#include <lib/vfs/cpp/internal/node_connection.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <gtest/gtest.h>

namespace {

class DummyTestFile : public vfs::internal::File {
 public:
  zx_status_t ReadAt(uint64_t count, uint64_t offset, std::vector<uint8_t>* out_data) override {
    return ZX_OK;
  }

  zx_status_t WriteAt(std::vector<uint8_t> data, uint64_t offset, uint64_t* out_actual) override {
    return ZX_OK;
  }

  uint64_t GetLength() override { return 0; }

  size_t GetCapacity() override { return 0; }
};

void CallBindTwiceAndTest(vfs::internal::Connection* connection) {
  fuchsia::io::NodePtr ptr1;
  fuchsia::io::NodePtr ptr2;
  ASSERT_EQ(ZX_OK,
            connection->Bind(ptr1.NewRequest().TakeChannel(), async_get_default_dispatcher()));
  ASSERT_EQ(ZX_ERR_BAD_STATE,
            connection->Bind(ptr2.NewRequest().TakeChannel(), async_get_default_dispatcher()));
}

TEST(ConnectionBindCalledTwice, DirectoryConnection) {
  vfs::PseudoDir dir;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  vfs::internal::DirectoryConnection connection({}, &dir);

  CallBindTwiceAndTest(&connection);
}

TEST(ConnectionBindCalledTwice, FileConnection) {
  DummyTestFile file;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  vfs::internal::FileConnection connection({}, &file);

  CallBindTwiceAndTest(&connection);
}

TEST(ConnectionBindCalledTwice, NodeConnection) {
  vfs::PseudoDir dir;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  vfs::internal::NodeConnection connection({}, &dir);

  CallBindTwiceAndTest(&connection);
}

class DummyTestNode : public vfs::internal::Node {
 public:
  zx_status_t PreClose(vfs::internal::Connection* connection) override {
    return ZX_ERR_UNAVAILABLE;
  }

  void Describe(fuchsia::io::NodeInfoDeprecated* out_info) override {}
  void GetConnectionInfo(fuchsia::io::ConnectionInfo* out_info) override {}

  fuchsia::io::OpenFlags GetAllowedFlags() const override { return {}; }

  fuchsia::io::OpenFlags GetProhibitiveFlags() const override { return {}; }

  zx_status_t CreateConnection(fuchsia::io::OpenFlags flags,
                               std::unique_ptr<vfs::internal::Connection>* connection) override {
    return Node::CreateConnection(flags, connection);
  }
};

TEST(ConenctionTest, ConnectionPassedErrorInClose) {
  DummyTestNode node;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  loop.StartThread("vfs test thread");
  fuchsia::io::NodeSyncPtr ptr;
  ASSERT_EQ(ZX_OK, node.Serve({}, ptr.NewRequest().TakeChannel(), loop.dispatcher()));
  fuchsia::unknown::Closeable_Close_Result result;
  ASSERT_EQ(ZX_OK, ptr->Close(&result));
  ASSERT_TRUE(result.is_err());
  ASSERT_EQ(ZX_ERR_UNAVAILABLE, result.err());
}

}  // namespace
