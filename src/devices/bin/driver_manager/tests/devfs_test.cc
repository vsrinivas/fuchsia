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
  std::optional<Devnode> root_slot;
  Devfs devfs(root_slot, nullptr);
  ASSERT_TRUE(root_slot.has_value());
  Devnode root_node(devfs, nullptr);
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(root_node.export_dir({}, "svc", "one/two", 0, {}, out));

  const Devnode* node_one = root_node.lookup("one");
  ASSERT_NE(node_one, nullptr);
  EXPECT_EQ("one", node_one->name());
  const Devnode* node_two = node_one->lookup("two");
  ASSERT_NE(node_two, nullptr);
  EXPECT_EQ("two", node_two->name());
}

TEST(Devfs, Export_ExcessSeparators) {
  std::optional<Devnode> root_slot;
  Devfs devfs(root_slot, nullptr);
  ASSERT_TRUE(root_slot.has_value());
  Devnode root_node(devfs, nullptr);
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_STATUS(root_node.export_dir({}, "svc", "one////two", 0, {}, out), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(root_node.lookup("one"), nullptr);
  ASSERT_EQ(root_node.lookup("two"), nullptr);
}

TEST(Devfs, Export_OneByOne) {
  std::optional<Devnode> root_slot;
  Devfs devfs(root_slot, nullptr);
  ASSERT_TRUE(root_slot.has_value());
  Devnode root_node(devfs, nullptr);
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(root_node.export_dir({}, "svc", "one", 0, {}, out));

  const Devnode* node_one = root_node.lookup("one");
  ASSERT_NE(node_one, nullptr);
  EXPECT_EQ("one", node_one->name());

  ASSERT_OK(root_node.export_dir({}, "svc", "one/two", 0, {}, out));

  const Devnode* node_two = node_one->lookup("two");
  ASSERT_NE(node_two, nullptr);
  EXPECT_EQ("two", node_two->name());
}

TEST(Devfs, Export_InvalidPath) {
  std::optional<Devnode> root_slot;
  Devfs devfs(root_slot, nullptr);
  ASSERT_TRUE(root_slot.has_value());
  Devnode root_node(devfs, nullptr);
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, root_node.export_dir({}, "", "one", 0, {}, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, root_node.export_dir({}, "/svc", "one", 0, {}, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, root_node.export_dir({}, "svc/", "one", 0, {}, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, root_node.export_dir({}, "/svc/", "one", 0, {}, out));

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, root_node.export_dir({}, "svc", "", 0, {}, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, root_node.export_dir({}, "svc", "/one/two", 0, {}, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, root_node.export_dir({}, "svc", "one/two/", 0, {}, out));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, root_node.export_dir({}, "svc", "/one/two/", 0, {}, out));
}

TEST(Devfs, Export_WithProtocol) {
  const async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  std::optional<Devnode> root_slot;
  Devfs devfs(root_slot, nullptr);
  ASSERT_TRUE(root_slot.has_value());
  Devnode root_node(devfs, nullptr);

  const Devnode* proto_node = devfs.proto_node(ZX_PROTOCOL_BLOCK);
  ASSERT_NE(proto_node, nullptr);
  EXPECT_EQ("block", proto_node->name());
  EXPECT_EQ(proto_node->lookup("000"), nullptr);

  std::vector<std::unique_ptr<Devnode>> out;
  auto outgoing = component::OutgoingDirectory::Create(loop.dispatcher());
  auto endpoints = fidl::CreateEndpoints<fio::Directory>();
  ASSERT_OK(endpoints.status_value());
  ASSERT_OK(outgoing.Serve(std::move(endpoints->server)));

  ASSERT_OK(root_node.export_dir(std::move(endpoints->client), "svc", "one/two", ZX_PROTOCOL_BLOCK,
                                 {}, out));

  const Devnode* node_one = root_node.lookup("one");
  ASSERT_NE(node_one, nullptr);
  EXPECT_EQ("one", node_one->name());

  const Devnode* node_two = node_one->lookup("two");
  ASSERT_NE(node_two, nullptr);
  EXPECT_EQ("two", node_two->name());

  const Devnode* node_000 = proto_node->lookup("000");
  ASSERT_NE(node_000, nullptr);
  EXPECT_EQ("000", node_000->name());
}

TEST(Devfs, Export_AlreadyExists) {
  std::optional<Devnode> root_slot;
  Devfs devfs(root_slot, nullptr);
  ASSERT_TRUE(root_slot.has_value());
  Devnode root_node(devfs, nullptr);
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(root_node.export_dir({}, "svc", "one/two", 0, {}, out));
  ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, root_node.export_dir({}, "svc", "one/two", 0, {}, out));
}

TEST(Devfs, Export_FailedToClone) {
  std::optional<Devnode> root_slot;
  Devfs devfs(root_slot, nullptr);
  ASSERT_TRUE(root_slot.has_value());
  Devnode root_node(devfs, nullptr);
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_EQ(ZX_ERR_BAD_HANDLE,
            root_node.export_dir({}, "svc", "one/two", ZX_PROTOCOL_BLOCK, {}, out));
}

TEST(Devfs, Export_DropDevfs) {
  std::optional<Devnode> root_slot;
  Devfs devfs(root_slot, nullptr);
  ASSERT_TRUE(root_slot.has_value());
  Devnode root_node(devfs, nullptr);
  std::vector<std::unique_ptr<Devnode>> out;

  ASSERT_OK(root_node.export_dir({}, "svc", "one/two", 0, {}, out));

  {
    const Devnode* node_one = root_node.lookup("one");
    ASSERT_NE(node_one, nullptr);
    EXPECT_EQ("one", node_one->name());

    const Devnode* node_two = node_one->lookup("two");
    ASSERT_NE(node_two, nullptr);
    EXPECT_EQ("two", node_two->name());
  }

  out.clear();

  ASSERT_EQ(root_node.lookup("one"), nullptr);
}

TEST(Devfs, ExportWatcher_Export) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  std::optional<Devnode> root_slot;
  Devfs devfs(root_slot, nullptr);
  ASSERT_TRUE(root_slot.has_value());
  Devnode root_node(devfs, nullptr);

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
  {
    const Devnode* node_one = root_node.lookup("one");
    ASSERT_NE(node_one, nullptr);
    EXPECT_EQ("one", node_one->name());

    const Devnode* node_two = node_one->lookup("two");
    ASSERT_NE(node_two, nullptr);
    EXPECT_EQ("two", node_two->name());
  }

  // Run the loop and make sure ExportWatcher connected to our service.
  loop.Run();
  loop.ResetQuit();
  ASSERT_NE(service_channel.get(), ZX_HANDLE_INVALID);
  ASSERT_FALSE(did_close);
  ASSERT_NE(root_node.lookup("one"), nullptr);

  // Close the server end and check that ExportWatcher noticed.
  service_channel.reset();
  loop.Run();
  ASSERT_TRUE(did_close);
  ASSERT_NE(root_node.lookup("one"), nullptr);

  // Drop ExportWatcher and make sure the devfs nodes disappeared.
  result.value().reset();
  ASSERT_EQ(root_node.lookup("one"), nullptr);
}

TEST(Devfs, ExportWatcher_Export_Invisible) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  std::optional<Devnode> root_slot;
  Devfs devfs(root_slot, nullptr);
  ASSERT_TRUE(root_slot.has_value());
  Devnode root_node(devfs, nullptr);

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
  {
    const Devnode* node_one = root_node.lookup("one");
    ASSERT_NE(node_one, nullptr);
    EXPECT_EQ("one", node_one->name());
    EXPECT_EQ(fuchsia_device_fs::wire::ExportOptions::kInvisible, node_one->export_options());

    const Devnode* node_two = node_one->lookup("two");
    ASSERT_NE(node_two, nullptr);
    EXPECT_EQ("two", node_two->name());
    EXPECT_EQ(fuchsia_device_fs::wire::ExportOptions::kInvisible, node_two->export_options());
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
  {
    const Devnode* node_one = root_node.lookup("one");
    ASSERT_NE(node_one, nullptr);
    EXPECT_EQ("one", node_one->name());
    EXPECT_EQ(fuchsia_device_fs::wire::ExportOptions(), node_one->export_options());

    const Devnode* node_two = node_one->lookup("two");
    ASSERT_NE(node_two, nullptr);
    EXPECT_EQ("two", node_two->name());
    EXPECT_EQ(fuchsia_device_fs::wire::ExportOptions(), node_two->export_options());
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
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  std::optional<Devnode> root_slot;
  Devfs devfs(root_slot, nullptr);
  ASSERT_TRUE(root_slot.has_value());
  Devnode root_node(devfs, nullptr);

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

  const fidl::WireSyncClient<fuchsia_device_fs::Exporter> client(
      std::move(exporter_endpoints->client));

  // ExportWatcher::Create will fail because we closed the server end of the channel.
  auto result =
      client->Export(std::move(endpoints->client), "svc/test", "one/two", ZX_PROTOCOL_BLOCK);
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(result->is_error());
}
