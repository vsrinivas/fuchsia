// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/devfs.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/driver.h>
#include <lib/sys/component/cpp/outgoing_directory.h>

#include <zxtest/zxtest.h>

#include "src/devices/bin/driver_manager/coordinator.h"
#include "src/devices/bin/driver_manager/devfs_exporter.h"

namespace fio = fuchsia_io;

TEST(Devfs, Export) {
  Devfs devfs(nullptr);
  Devnode root_node(devfs, nullptr, nullptr, "root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(devfs.export_dir(&root_node, {}, "svc", "one/two", 0, {}, out));

  ASSERT_EQ(1, root_node.children.size_slow());
  auto& node_one = root_node.children.front();
  EXPECT_EQ("one", node_one.name());
  ASSERT_EQ(1, node_one.children.size_slow());
  auto& node_two = node_one.children.front();
  EXPECT_EQ("two", node_two.name());
}

TEST(Devfs, Export_ExcessSeparators) {
  Devfs devfs(nullptr);
  Devnode root_node(devfs, nullptr, nullptr, "root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(devfs.export_dir(&root_node, {}, "svc", "one////two", 0, {}, out));

  ASSERT_EQ(1, root_node.children.size_slow());
  auto& node_one = root_node.children.front();
  EXPECT_EQ("one", node_one.name());
  ASSERT_EQ(1, node_one.children.size_slow());
  auto& node_two = node_one.children.front();
  EXPECT_EQ("two", node_two.name());
}

TEST(Devfs, Export_OneByOne) {
  Devfs devfs(nullptr);
  Devnode root_node(devfs, nullptr, nullptr, "root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(devfs.export_dir(&root_node, {}, "svc", "one", 0, {}, out));

  ASSERT_EQ(1, root_node.children.size_slow());
  auto& node_one = root_node.children.front();
  EXPECT_EQ("one", node_one.name());

  ASSERT_OK(devfs.export_dir(&root_node, {}, "svc", "one/two", 0, {}, out));

  ASSERT_EQ(1, node_one.children.size_slow());
  auto& node_two = node_one.children.front();
  EXPECT_EQ("two", node_two.name());
}

TEST(Devfs, Export_InvalidPath) {
  Devfs devfs(nullptr);
  Devnode root_node(devfs, nullptr, nullptr, "root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs.export_dir(&root_node, {}, "", "one", 0, {}, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs.export_dir(&root_node, {}, "/svc", "one", 0, {}, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs.export_dir(&root_node, {}, "svc/", "one", 0, {}, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs.export_dir(&root_node, {}, "/svc/", "one", 0, {}, out));

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs.export_dir(&root_node, {}, "svc", "", 0, {}, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs.export_dir(&root_node, {}, "svc", "/one/two", 0, {}, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs.export_dir(&root_node, {}, "svc", "one/two/", 0, {}, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, devfs.export_dir(&root_node, {}, "svc", "/one/two/", 0, {}, out));
}

TEST(Devfs, Export_WithProtocol) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  Devfs devfs(nullptr);
  Devnode* proto_node = devfs.proto_node(ZX_PROTOCOL_BLOCK);
  ASSERT_NE(proto_node, nullptr);
  EXPECT_EQ("block", proto_node->name());
  EXPECT_EQ(0, proto_node->children.size_slow());

  Devnode root_node(devfs, nullptr, nullptr, "root");
  std::vector<std::unique_ptr<Devnode>> out;
  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  auto endpoints = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(outgoing.Serve(std::move(endpoints->server)));

  ASSERT_OK(devfs.export_dir(&root_node, std::move(endpoints->client), "svc", "one/two",
                             ZX_PROTOCOL_BLOCK, {}, out));

  ASSERT_EQ(1, root_node.children.size_slow());
  auto& node_one = root_node.children.front();
  EXPECT_EQ("one", node_one.name());
  ASSERT_EQ(1, node_one.children.size_slow());
  auto& node_two = node_one.children.front();
  EXPECT_EQ("two", node_two.name());

  ASSERT_EQ(1, proto_node->children.size_slow());
  auto& node_000 = proto_node->children.front();
  EXPECT_EQ("000", node_000.name());
}

TEST(Devfs, Export_AlreadyExists) {
  Devfs devfs(nullptr);
  Devnode root_node(devfs, nullptr, nullptr, "root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(devfs.export_dir(&root_node, {}, "svc", "one/two", 0, {}, out));
  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, devfs.export_dir(&root_node, {}, "svc", "one/two", 0, {}, out));
}

TEST(Devfs, Export_FailedToClone) {
  Devfs devfs(nullptr);
  Devnode root_node(devfs, nullptr, nullptr, "root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_EQ(ZX_ERR_BAD_HANDLE,
            devfs.export_dir(&root_node, {}, "svc", "one/two", ZX_PROTOCOL_BLOCK, {}, out));
}

TEST(Devfs, Export_DropDevfs) {
  Devfs devfs(nullptr);
  Devnode root_node(devfs, nullptr, nullptr, "root");
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(devfs.export_dir(&root_node, {}, "svc", "one/two", 0, {}, out));

  ASSERT_EQ(1, root_node.children.size_slow());
  {
    auto& node_one = root_node.children.front();
    EXPECT_EQ("one", node_one.name());
    ASSERT_EQ(1, node_one.children.size_slow());
    auto& node_two = node_one.children.front();
    EXPECT_EQ("two", node_two.name());
  }

  out.clear();

  ASSERT_EQ(0, root_node.children.size_slow());
}

TEST(Devfs, ExportWatcher_Export) {
  Devfs devfs(nullptr);
  Devnode root_node(devfs, nullptr, nullptr, "root");
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  // Create a fake service at svc/test.
  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  zx::channel service_channel;
  auto handler = [&service_channel, &loop](zx::channel server) {
    service_channel = std::move(server);
    loop.Quit();
  };
  ASSERT_EQ(outgoing.AddProtocol(std::move(handler), "test").status_value(), ZX_OK);

  // Export the svc/test.
  auto endpoints = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(outgoing.Serve(std::move(endpoints->server)).status_value());

  auto result = driver_manager::ExportWatcher::Create(
      loop.dispatcher(), devfs, &root_node, std::move(endpoints->client), "svc/test", "one/two",
      ZX_PROTOCOL_BLOCK, fuchsia_device_fs::wire::ExportOptions());
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
    EXPECT_EQ("one", node_one.name());
    ASSERT_EQ(1, node_one.children.size_slow());
    auto& node_two = node_one.children.front();
    EXPECT_EQ("two", node_two.name());
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

TEST(Devfs, ExportWatcher_Export_Invisible) {
  Devfs devfs(nullptr);
  Devnode root_node(devfs, nullptr, nullptr, "root");
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  // Create the export server and client.
  auto exporter = driver_manager::DevfsExporter(devfs, &root_node, loop.dispatcher());
  fidl::WireClient<fuchsia_device_fs::Exporter> exporter_client;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_device_fs::Exporter>();
    ASSERT_OK(endpoints.status_value());
    fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), &exporter);
    exporter_client.Bind(std::move(endpoints->client), loop.dispatcher());
  }

  // Create a fake service at svc/test.
  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  zx::channel service_channel;
  auto handler = [&service_channel](zx::channel server) { service_channel = std::move(server); };
  ASSERT_EQ(outgoing.AddProtocol(std::move(handler), "test").status_value(), ZX_OK);

  // Export the svc/test.
  auto endpoints = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(outgoing.Serve(std::move(endpoints->server)).status_value());

  exporter_client
      ->ExportOptions(std::move(endpoints->client), "svc/test", "one/two", ZX_PROTOCOL_BLOCK,
                      fuchsia_device_fs::wire::ExportOptions::kInvisible)
      .Then([](auto& result) { ASSERT_EQ(ZX_OK, result.status()); });
  ASSERT_EQ(ZX_OK, loop.RunUntilIdle());

  // Make sure the directories were set up correctly.
  ASSERT_EQ(1, root_node.children.size_slow());
  {
    auto& node_one = root_node.children.front();
    EXPECT_EQ("one", node_one.name());
    EXPECT_EQ(fuchsia_device_fs::wire::ExportOptions::kInvisible, node_one.service_options);
    ASSERT_EQ(1, node_one.children.size_slow());

    auto& node_two = node_one.children.front();
    EXPECT_EQ("two", node_two.name());
    EXPECT_EQ(fuchsia_device_fs::wire::ExportOptions::kInvisible, node_two.service_options);
  }

  // Try and make a subdir visible, this will fail because the devfs path has to match exactly with
  // Export.
  exporter_client->MakeVisible("one").Then(
      [](fidl::WireUnownedResult<fuchsia_device_fs::Exporter::MakeVisible>& result) {
        ASSERT_EQ(ZX_OK, result.status());
        ASSERT_TRUE(!result->is_ok());
        ASSERT_EQ(ZX_ERR_NOT_FOUND, result->error_value());
      });

  // Make the directories visible.
  exporter_client->MakeVisible("one/two").Then([](auto& result) {
    ASSERT_EQ(ZX_OK, result.status());
    ASSERT_TRUE(result->is_ok());
  });
  ASSERT_EQ(ZX_OK, loop.RunUntilIdle());

  // Make sure the directories were set up correctly.
  ASSERT_EQ(1, root_node.children.size_slow());
  {
    auto& node_one = root_node.children.front();
    EXPECT_EQ(fuchsia_device_fs::wire::ExportOptions(), node_one.service_options);
    ASSERT_EQ(1, node_one.children.size_slow());

    auto& node_two = node_one.children.front();
    EXPECT_EQ(fuchsia_device_fs::wire::ExportOptions(), node_two.service_options);
  }

  // Try and make visible again, this will cause an error.
  exporter_client->MakeVisible("one/two").Then([](auto& result) {
    ASSERT_EQ(ZX_OK, result.status());
    ASSERT_TRUE(!result->is_ok());
    ASSERT_EQ(ZX_ERR_BAD_STATE, result->error_value());
  });

  ASSERT_EQ(ZX_OK, loop.RunUntilIdle());
}

TEST(Devfs, ExportWatcherCreateFails) {
  Devfs devfs(nullptr);
  Devnode root_node(devfs, nullptr, nullptr, "root");
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  // Create a fake service at svc/test.
  // Export the svc/test.
  auto endpoints = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(endpoints.status_value());
  // Close the server end, so that the eventual call to Open() fails.
  endpoints->server.Close(ZX_ERR_PEER_CLOSED);

  driver_manager::DevfsExporter exporter(devfs, &root_node, loop.dispatcher());
  auto exporter_endpoints = fidl::CreateEndpoints<fuchsia_device_fs::Exporter>();
  ASSERT_OK(exporter_endpoints.status_value());

  fidl::BindServer(loop.dispatcher(), std::move(exporter_endpoints->server), &exporter);

  ASSERT_OK(loop.StartThread("export-watcher-test-thread"));

  fidl::WireSyncClient<fuchsia_device_fs::Exporter> client(std::move(exporter_endpoints->client));

  // ExportWatcher::Create will fail because we closed the server end of the channel.
  auto result =
      client->Export(std::move(endpoints->client), "svc/test", "one/two", ZX_PROTOCOL_BLOCK);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
}
