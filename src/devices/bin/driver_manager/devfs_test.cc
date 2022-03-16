// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/devfs.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/driver.h>
#include <lib/service/llcpp/outgoing_directory.h>
#include <lib/svc/outgoing.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>

#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_manager/devfs_exporter.h"

namespace fio = fuchsia_io;

TEST(Devfs, Export) {
  Devnode root_node("root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(devfs_export(&root_node, {}, "svc", "one/two", 0, out));

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

  ASSERT_OK(devfs_export(&root_node, {}, "svc", "one////two", 0, out));

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

  ASSERT_OK(devfs_export(&root_node, {}, "svc", "one", 0, out));

  ASSERT_EQ(1, root_node.children.size_slow());
  auto& node_one = root_node.children.front();
  EXPECT_EQ("one", node_one.name);

  ASSERT_OK(devfs_export(&root_node, {}, "svc", "one/two", 0, out));

  ASSERT_EQ(1, node_one.children.size_slow());
  auto& node_two = node_one.children.front();
  EXPECT_EQ("two", node_two.name);
}

TEST(Devfs, Export_InvalidPath) {
  Devnode root_node("root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs_export(&root_node, {}, "", "one", 0, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs_export(&root_node, {}, "/svc", "one", 0, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs_export(&root_node, {}, "svc/", "one", 0, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs_export(&root_node, {}, "/svc/", "one", 0, out));

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs_export(&root_node, {}, "svc", "", 0, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs_export(&root_node, {}, "svc", "/one/two", 0, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs_export(&root_node, {}, "svc", "one/two/", 0, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs_export(&root_node, {}, "svc", "/one/two/", 0, out));
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

  ASSERT_OK(devfs_export(&root_node, std::move(endpoints->client), "svc", "one/two",
                         ZX_PROTOCOL_BLOCK, out));

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

  ASSERT_OK(devfs_export(&root_node, {}, "svc", "one/two", 0, out));
  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, devfs_export(&root_node, {}, "svc", "one/two", 0, out));
}

TEST(Devfs, Export_FailedToClone) {
  Devnode class_node("class");
  devfs_prepopulate_class(&class_node);

  Devnode root_node("root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_EQ(ZX_ERR_BAD_HANDLE,
            devfs_export(&root_node, {}, "svc", "one/two", ZX_PROTOCOL_BLOCK, out));
}

TEST(Devfs, Export_DropDevfs) {
  Devnode root_node("root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(devfs_export(&root_node, {}, "svc", "one/two", 0, out));

  ASSERT_EQ(1, root_node.children.size_slow());
  {
    auto& node_one = root_node.children.front();
    EXPECT_EQ("one", node_one.name);
    ASSERT_EQ(1, node_one.children.size_slow());
    auto& node_two = node_one.children.front();
    EXPECT_EQ("two", node_two.name);
  }

  out.clear();

  ASSERT_EQ(0, root_node.children.size_slow());
}

TEST(Devfs, ExportWatcher_Export) {
  Devnode root_node("root");
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  // Create a fake service at svc/test.
  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  zx::channel service_channel;
  auto handler = [&service_channel, &loop](zx::channel server) {
    service_channel = std::move(server);
    loop.Quit();
  };
  ASSERT_EQ(outgoing.AddNamedProtocol(std::move(handler), "test").status_value(), ZX_OK);

  // Export the svc/test.
  auto endpoints = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(outgoing.Serve(std::move(endpoints->server)).status_value());

  auto result = driver_manager::ExportWatcher::Create(loop.dispatcher(), &root_node,
                                                      std::move(endpoints->client), "svc/test",
                                                      "one/two", ZX_PROTOCOL_BLOCK);
  ASSERT_EQ(ZX_OK, result.status_value());

  // Set our ExportWatcher to let us know if the service was closed.
  bool did_close = false;
  result.value()->set_on_close_callback([&did_close, &loop]() {
    did_close = true;
    loop.Quit();
  });

  // Make sure the directories were set up correctly.
  ASSERT_EQ(1, root_node.children.size_slow());
  {
    auto& node_one = root_node.children.front();
    EXPECT_EQ("one", node_one.name);
    ASSERT_EQ(1, node_one.children.size_slow());
    auto& node_two = node_one.children.front();
    EXPECT_EQ("two", node_two.name);
  }

  // Run the loop and make sure ExportWatcher connected to our service.
  loop.Run();
  loop.ResetQuit();
  ASSERT_NE(service_channel.get(), ZX_HANDLE_INVALID);
  ASSERT_FALSE(did_close);
  ASSERT_EQ(1, root_node.children.size_slow());

  // Close the server end and check that ExportWatcher noticed.
  service_channel.reset();
  loop.Run();
  ASSERT_TRUE(did_close);
  ASSERT_EQ(1, root_node.children.size_slow());

  // Drop ExportWatcher and make sure the devfs nodes disappeared.
  result.value().reset();
  ASSERT_EQ(0, root_node.children.size_slow());
}

TEST(Devfs, ExportWatcherCreateFails) {
  Devnode root_node("root");
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  // Create a fake service at svc/test.
  // Export the svc/test.
  auto endpoints = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(endpoints.status_value());
  // Close the server end, so that the eventual call to Open() fails.
  endpoints->server.Close(ZX_ERR_PEER_CLOSED);

  driver_manager::DevfsExporter exporter(&root_node, loop.dispatcher());
  auto exporter_endpoints = fidl::CreateEndpoints<fuchsia_device_fs::Exporter>();
  ASSERT_OK(exporter_endpoints.status_value());

  fidl::BindServer(loop.dispatcher(), std::move(exporter_endpoints->server), &exporter);

  ASSERT_OK(loop.StartThread("export-watcher-test-thread"));

  fidl::WireSyncClient<fuchsia_device_fs::Exporter> client(std::move(exporter_endpoints->client));

  // ExportWatcher::Create will fail because we closed the server end of the channel.
  auto result =
      client->Export(std::move(endpoints->client), "svc/test", "one/two", ZX_PROTOCOL_BLOCK);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->result.is_err());
}
