// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/devfs.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/driver.h>
#include <lib/svc/outgoing.h>

#include <zxtest/zxtest.h>

namespace fio = fuchsia_io;

TEST(Devfs, Export) {
  Devnode root_node("root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(devfs_export(&root_node, {}, "one/two", 0, out));

  ASSERT_EQ(1, root_node.children.size_slow());
  auto& node_one = root_node.children.front();
  EXPECT_EQ("one", node_one.name);
  ASSERT_EQ(1, node_one.children.size_slow());
  auto& node_two = node_one.children.front();
  EXPECT_EQ("two", node_two.name);
}

TEST(Devfs, Export_ExcessSeparators) {
  Devnode root_node("root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(devfs_export(&root_node, {}, "one////two", 0, out));

  ASSERT_EQ(1, root_node.children.size_slow());
  auto& node_one = root_node.children.front();
  EXPECT_EQ("one", node_one.name);
  ASSERT_EQ(1, node_one.children.size_slow());
  auto& node_two = node_one.children.front();
  EXPECT_EQ("two", node_two.name);
}

TEST(Devfs, Export_OneByOne) {
  Devnode root_node("root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(devfs_export(&root_node, {}, "one", 0, out));

  ASSERT_EQ(1, root_node.children.size_slow());
  auto& node_one = root_node.children.front();
  EXPECT_EQ("one", node_one.name);

  ASSERT_OK(devfs_export(&root_node, {}, "one/two", 0, out));

  ASSERT_EQ(1, node_one.children.size_slow());
  auto& node_two = node_one.children.front();
  EXPECT_EQ("two", node_two.name);
}

TEST(Devfs, Export_InvalidPath) {
  Devnode root_node("root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs_export(&root_node, {}, "", 0, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs_export(&root_node, {}, "/one/two", 0, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs_export(&root_node, {}, "one/two/", 0, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs_export(&root_node, {}, "/one/two/", 0, out));
}

TEST(Devfs, Export_WithProtocol) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  Devnode class_node("class");
  devfs_prepopulate_class(&class_node);
  auto proto_node = devfs_proto_node(ZX_PROTOCOL_BLOCK);
  EXPECT_EQ("block", proto_node->name);
  EXPECT_EQ(0, proto_node->children.size_slow());

  Devnode root_node("root");
  std::vector<std::unique_ptr<Devnode>> out;
  svc::Outgoing outgoing(loop.dispatcher());
  auto endpoints = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(outgoing.Serve(std::move(endpoints->server)));

  fidl::ClientEnd<fio::Node> client(endpoints->client.TakeChannel());
  ASSERT_OK(devfs_export(&root_node, std::move(client), "one/two", ZX_PROTOCOL_BLOCK, out));

  ASSERT_EQ(1, root_node.children.size_slow());
  auto& node_one = root_node.children.front();
  EXPECT_EQ("one", node_one.name);
  ASSERT_EQ(1, node_one.children.size_slow());
  auto& node_two = node_one.children.front();
  EXPECT_EQ("two", node_two.name);

  ASSERT_EQ(1, proto_node->children.size_slow());
  auto& node_000 = proto_node->children.front();
  EXPECT_EQ("000", node_000.name);
}

TEST(Devfs, Export_AlreadyExists) {
  Devnode root_node("root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(devfs_export(&root_node, {}, "one/two", 0, out));
  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, devfs_export(&root_node, {}, "one/two", 0, out));
}

TEST(Devfs, Export_FailedToClone) {
  Devnode class_node("class");
  devfs_prepopulate_class(&class_node);

  Devnode root_node("root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_EQ(ZX_ERR_BAD_HANDLE, devfs_export(&root_node, {}, "one/two", ZX_PROTOCOL_BLOCK, out));
}
