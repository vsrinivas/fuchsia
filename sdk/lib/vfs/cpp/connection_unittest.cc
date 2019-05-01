// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/vfs/cpp/file.h>
#include <lib/vfs/cpp/internal/directory_connection.h>
#include <lib/vfs/cpp/internal/file_connection.h>
#include <lib/vfs/cpp/internal/node_connection.h>
#include <lib/vfs/cpp/node.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <zircon/errors.h>

#include "gtest/gtest.h"

namespace {

class DummyTestFile : public vfs::File {
 public:
  zx_status_t ReadAt(uint64_t count, uint64_t offset,
                     std::vector<uint8_t>* out_data) override {
    return ZX_OK;
  }

  zx_status_t WriteAt(std::vector<uint8_t> data, uint64_t offset,
                      uint64_t* out_actual) override {
    return ZX_OK;
  }

  uint64_t GetLength() override { return 0; }

  size_t GetCapacity() override { return 0; }
};

void CallBindTwiceAndTest(vfs::Connection* connection) {
  fuchsia::io::NodePtr ptr1;
  fuchsia::io::NodePtr ptr2;
  ASSERT_EQ(ZX_OK, connection->Bind(ptr1.NewRequest().TakeChannel(),
                                    async_get_default_dispatcher()));
  ASSERT_EQ(ZX_ERR_BAD_STATE, connection->Bind(ptr2.NewRequest().TakeChannel(),
                                               async_get_default_dispatcher()));
}

TEST(ConnectionBindCalledTwice, DirectoryConnection) {
  vfs::PseudoDir dir;
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  vfs::internal::DirectoryConnection connection(0, &dir);

  CallBindTwiceAndTest(&connection);
}

TEST(ConnectionBindCalledTwice, FileConnection) {
  DummyTestFile file;
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  vfs::internal::FileConnection connection(0, &file);

  CallBindTwiceAndTest(&connection);
}

TEST(ConnectionBindCalledTwice, NodeConnection) {
  vfs::PseudoDir dir;
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  vfs::internal::NodeConnection connection(0, &dir);

  CallBindTwiceAndTest(&connection);
}

}  // namespace