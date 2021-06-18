// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/debugdata/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/debugdata/debugdata.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>

#include <filesystem>
#include <unordered_map>

#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/storage/vfs/cpp/vmo_file.h"

namespace {

constexpr char kTestSink[] = "test";
constexpr uint8_t kTestData[] = {0x00, 0x11, 0x22, 0x33};

TEST(DebugDataTest, PublishData) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));
  std::unordered_map<std::string, std::vector<zx::vmo>> data;
  debugdata::DebugData svc(
      loop.dispatcher(), fbl::unique_fd{open("/", O_RDONLY)},
      [&](std::string data_sink, zx::vmo vmo) { data[data_sink].push_back(std::move(vmo)); });
  fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server), &svc);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  ASSERT_OK(vmo.write(kTestData, 0, sizeof(kTestData)));

  zx::channel token_client, token_server;
  ASSERT_OK(zx::channel::create(0, &token_client, &token_server));
  ASSERT_OK(fidl::WireCall<fuchsia_debugdata::DebugData>(zx::unowned_channel(client))
                .Publish(kTestSink, std::move(vmo), std::move(token_server))
                .status());
  // close the client channel to indicate the VMO is ready to process.
  token_client.reset();

  ASSERT_OK(loop.RunUntilIdle());
  loop.Shutdown();

  ASSERT_EQ(data.size(), 1);

  ASSERT_NE(data.find(kTestSink), data.end());
  const auto& dump = data.at(kTestSink);
  ASSERT_EQ(dump.size(), 1);

  uint8_t content[sizeof(kTestData)];
  ASSERT_OK(dump[0].read(content, 0, sizeof(content)));
  ASSERT_EQ(memcmp(content, kTestData, sizeof(kTestData)), 0);
}

TEST(DebugDataTest, DrainData) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));
  std::unordered_map<std::string, std::vector<zx::vmo>> data;
  debugdata::DebugData svc(
      loop.dispatcher(), fbl::unique_fd{open("/", O_RDONLY)},
      [&](std::string data_sink, zx::vmo vmo) { data[data_sink].push_back(std::move(vmo)); });
  fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(server), &svc);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  ASSERT_OK(vmo.write(kTestData, 0, sizeof(kTestData)));

  zx::channel token_client, token_server;
  ASSERT_OK(zx::channel::create(0, &token_client, &token_server));
  ASSERT_OK(fidl::WireCall<fuchsia_debugdata::DebugData>(zx::unowned_channel(client))
                .Publish(kTestSink, std::move(vmo), std::move(token_server))
                .status());

  ASSERT_OK(loop.RunUntilIdle());
  // As token_client is held open the data is not processed.
  ASSERT_EQ(data.size(), 0);
  // After draining data the VMO is processed anyway.
  svc.DrainData();

  ASSERT_EQ(data.size(), 1);

  ASSERT_NE(data.find(kTestSink), data.end());
  const auto& dump = data.at(kTestSink);
  ASSERT_EQ(dump.size(), 1);

  uint8_t content[sizeof(kTestData)];
  ASSERT_OK(dump[0].read(content, 0, sizeof(content)));
  ASSERT_EQ(memcmp(content, kTestData, sizeof(kTestData)), 0);
}

TEST(DebugDataTest, LoadConfig) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<fs::SynchronousVfs> vfs;

  zx::vmo data;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &data));
  ASSERT_OK(data.write(kTestData, 0, sizeof(kTestData)));

  const std::filesystem::path directory = "/dir";
  const std::filesystem::path filename = "config";

  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  dir->AddEntry(filename.c_str(), fbl::MakeRefCounted<fs::VmoFile>(data, 0, sizeof(kTestData)));

  zx::channel c1, c2;
  ASSERT_OK(zx::channel::create(0, &c1, &c2));

  vfs = std::make_unique<fs::SynchronousVfs>(loop.dispatcher());
  ASSERT_OK(vfs->ServeDirectory(std::move(dir), std::move(c1)));
  ASSERT_OK(loop.StartThread());

  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_get_installed(&ns));
  ASSERT_OK(fdio_ns_bind(ns, directory.c_str(), c2.release()));
  auto unbind = fit::defer([&]() { fdio_ns_unbind(ns, directory.c_str()); });

  async::Loop svc_loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));
  debugdata::DebugData svc(loop.dispatcher(), fbl::unique_fd{fdio_ns_opendir(ns)},
                           [&](std::string, zx::vmo) {});
  fidl::BindSingleInFlightOnly(svc_loop.dispatcher(), std::move(server), &svc);
  ASSERT_OK(svc_loop.StartThread());

  const auto path = (directory / filename).string();

  auto result = fidl::WireCall<fuchsia_debugdata::DebugData>(zx::unowned_channel(client))
                    .LoadConfig(fidl::StringView::FromExternal(path));
  ASSERT_OK(result.status());
  zx::vmo vmo = std::move(result->config);

  svc_loop.Shutdown();
  loop.Shutdown();
  vfs.reset();

  uint8_t config[sizeof(kTestData)];
  ASSERT_OK(vmo.read(config, 0, sizeof(config)));
  ASSERT_EQ(memcmp(config, kTestData, sizeof(config)), 0);
}

}  // anonymous namespace
