// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.debugdata/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/debugdata/debugdata.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
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
  zx::status endpoints = fidl::CreateEndpoints<fuchsia_debugdata::Publisher>();
  ASSERT_OK(endpoints.status_value());
  std::unordered_map<std::string, std::vector<zx::vmo>> data;
  debugdata::Publisher publisher(loop.dispatcher(), fbl::unique_fd{open("/", O_RDONLY)},
                                 [&](const std::string& data_sink, zx::vmo vmo) {
                                   data[data_sink].push_back(std::move(vmo));
                                 });
  publisher.Bind(std::move(endpoints->server));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  ASSERT_OK(vmo.write(kTestData, 0, sizeof(kTestData)));

  zx::eventpair token1, token2;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &token1, &token2));
  ASSERT_OK(fidl::WireCall(endpoints->client)
                ->Publish(kTestSink, std::move(vmo), std::move(token1))
                .status());
  // close the client handle to indicate the VMO is ready to process.
  token2.reset();

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
  zx::status endpoints = fidl::CreateEndpoints<fuchsia_debugdata::Publisher>();
  ASSERT_OK(endpoints.status_value());
  std::unordered_map<std::string, std::vector<zx::vmo>> data;
  debugdata::Publisher publisher(loop.dispatcher(), fbl::unique_fd{open("/", O_RDONLY)},
                                 [&](const std::string& data_sink, zx::vmo vmo) {
                                   data[data_sink].push_back(std::move(vmo));
                                 });
  publisher.Bind(std::move(endpoints->server));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  ASSERT_OK(vmo.write(kTestData, 0, sizeof(kTestData)));

  zx::eventpair token1, token2;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &token1, &token2));
  ASSERT_OK(fidl::WireCall(endpoints->client)
                ->Publish(kTestSink, std::move(vmo), std::move(token1))
                .status());

  ASSERT_OK(loop.RunUntilIdle());
  // As token_client is held open the data is not processed.
  ASSERT_EQ(data.size(), 0);
  // After draining data the VMO is processed anyway.
  publisher.DrainData();

  ASSERT_EQ(data.size(), 1);

  ASSERT_NE(data.find(kTestSink), data.end());
  const auto& dump = data.at(kTestSink);
  ASSERT_EQ(dump.size(), 1);

  uint8_t content[sizeof(kTestData)];
  ASSERT_OK(dump[0].read(content, 0, sizeof(content)));
  ASSERT_EQ(memcmp(content, kTestData, sizeof(kTestData)), 0);
}

TEST(DeprecatedDebugDataTest, PublishData) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  zx::status endpoints = fidl::CreateEndpoints<fuchsia_debugdata::DebugData>();
  ASSERT_OK(endpoints.status_value());
  std::unordered_map<std::string, std::vector<zx::vmo>> data;
  debugdata::Publisher publisher(loop.dispatcher(), fbl::unique_fd{open("/", O_RDONLY)},
                                 [&](const std::string& data_sink, zx::vmo vmo) {
                                   data[data_sink].push_back(std::move(vmo));
                                 });
  publisher.BindDeprecatedDebugData(std::move(endpoints->server));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  ASSERT_OK(vmo.write(kTestData, 0, sizeof(kTestData)));

  zx::status token_endpoints = fidl::CreateEndpoints<fuchsia_debugdata::DebugDataVmoToken>();
  ASSERT_OK(token_endpoints.status_value());
  ASSERT_OK(fidl::WireCall(endpoints->client)
                ->Publish(kTestSink, std::move(vmo), std::move(token_endpoints->server))
                .status());
  // close the client channel to indicate the VMO is ready to process.
  token_endpoints->client.reset();

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

TEST(DeprecatedDebugDataTest, DrainData) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  zx::status endpoints = fidl::CreateEndpoints<fuchsia_debugdata::DebugData>();
  ASSERT_OK(endpoints.status_value());
  std::unordered_map<std::string, std::vector<zx::vmo>> data;
  debugdata::Publisher publisher(loop.dispatcher(), fbl::unique_fd{open("/", O_RDONLY)},
                                 [&](const std::string& data_sink, zx::vmo vmo) {
                                   data[data_sink].push_back(std::move(vmo));
                                 });
  publisher.BindDeprecatedDebugData(std::move(endpoints->server));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  ASSERT_OK(vmo.write(kTestData, 0, sizeof(kTestData)));

  zx::status token_endpoints = fidl::CreateEndpoints<fuchsia_debugdata::DebugDataVmoToken>();
  ASSERT_OK(token_endpoints.status_value());
  ASSERT_OK(fidl::WireCall(endpoints->client)
                ->Publish(kTestSink, std::move(vmo), std::move(token_endpoints->server))
                .status());

  ASSERT_OK(loop.RunUntilIdle());
  // As token_client is held open the data is not processed.
  ASSERT_EQ(data.size(), 0);
  // After draining data the VMO is processed anyway.
  publisher.DrainData();

  ASSERT_EQ(data.size(), 1);

  ASSERT_NE(data.find(kTestSink), data.end());
  const auto& dump = data.at(kTestSink);
  ASSERT_EQ(dump.size(), 1);

  uint8_t content[sizeof(kTestData)];
  ASSERT_OK(dump[0].read(content, 0, sizeof(content)));
  ASSERT_EQ(memcmp(content, kTestData, sizeof(kTestData)), 0);
}

TEST(DeprecatedDebugDataTest, LoadConfig) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<fs::SynchronousVfs> vfs;

  zx::vmo data;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &data));
  ASSERT_OK(data.write(kTestData, 0, sizeof(kTestData)));

  const std::filesystem::path directory = "/dir";
  const std::filesystem::path filename = "config";

  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  dir->AddEntry(filename.c_str(), fbl::MakeRefCounted<fs::VmoFile>(data, 0, sizeof(kTestData)));

  zx::status endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(endpoints.status_value());

  vfs = std::make_unique<fs::SynchronousVfs>(loop.dispatcher());
  ASSERT_OK(vfs->ServeDirectory(std::move(dir), std::move(endpoints->server)));
  ASSERT_OK(loop.StartThread());

  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_get_installed(&ns));
  ASSERT_OK(fdio_ns_bind(ns, directory.c_str(), endpoints->client.TakeChannel().release()));
  auto unbind = fit::defer([&]() { fdio_ns_unbind(ns, directory.c_str()); });

  async::Loop svc_loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  zx::status debugdata_endpoints = fidl::CreateEndpoints<fuchsia_debugdata::DebugData>();
  ASSERT_OK(debugdata_endpoints.status_value());
  debugdata::Publisher publisher(loop.dispatcher(), fbl::unique_fd{fdio_ns_opendir(ns)},
                                 [&](const std::string&, zx::vmo) {});
  publisher.BindDeprecatedDebugData(std::move(debugdata_endpoints->server), svc_loop.dispatcher());
  ASSERT_OK(svc_loop.StartThread());

  const auto path = (directory / filename).string();

  auto result =
      fidl::WireCall(debugdata_endpoints->client)->LoadConfig(fidl::StringView::FromExternal(path));
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
