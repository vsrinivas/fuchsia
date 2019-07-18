// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fcntl.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fs/vmo-file.h>
#include <fuchsia/debugdata/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/debugdata/debugdata.h>
#include <lib/fdio/io.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>
#include <zxtest/zxtest.h>

namespace {

constexpr char kTestSink[] = "test";
constexpr uint8_t kTestData[] = {0x00, 0x11, 0x22, 0x33};

TEST(DebugDataTest, PublishData) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToThread};
  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));
  debugdata::DebugData svc(fbl::unique_fd{open("/", O_RDONLY)});
  fidl::Bind(loop.dispatcher(), std::move(server), &svc);

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  ASSERT_OK(vmo.write(kTestData, 0, sizeof(kTestData)));

  ASSERT_OK(::llcpp::fuchsia::debugdata::DebugData::Call::Publish_Deprecated(
      zx::unowned_channel(client), fidl::StringView(strlen(kTestSink), kTestSink), std::move(vmo)));

  ASSERT_OK(loop.RunUntilIdle());
  loop.Shutdown();

  const auto& data = svc.GetData();
  ASSERT_EQ(data.size(), 1);

  const auto& dump = data[0];
  ASSERT_STR_EQ(dump.sink_name.c_str(), kTestSink);

  uint8_t content[sizeof(kTestData)];
  ASSERT_OK(dump.file_data.read(content, 0, sizeof(content)));
  ASSERT_EQ(memcmp(content, kTestData, sizeof(kTestData)), 0);
}

TEST(DebugDataTest, LoadConfig) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToThread};
  std::unique_ptr<fs::SynchronousVfs> vfs;

  zx::vmo data;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &data));
  ASSERT_OK(data.write(kTestData, 0, sizeof(kTestData)));

  auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
  dir->AddEntry("config", fbl::MakeRefCounted<fs::VmoFile>(data, 0, sizeof(kTestData)));

  zx::channel c1, c2;
  ASSERT_OK(zx::channel::create(0, &c1, &c2));

  vfs = std::make_unique<fs::SynchronousVfs>(loop.dispatcher());
  ASSERT_OK(vfs->ServeDirectory(std::move(dir), std::move(c1)));
  ASSERT_OK(loop.StartThread());

  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_get_installed(&ns));
  ASSERT_OK(fdio_ns_bind(ns, "/dir", c2.release()));

  zx::channel client, server;
  ASSERT_OK(zx::channel::create(0, &client, &server));
  debugdata::DebugData svc(fbl::unique_fd{fdio_ns_opendir(ns)});
  fidl::Bind(loop.dispatcher(), std::move(server), &svc);
  ASSERT_OK(loop.StartThread());

  constexpr char kTestPath[] = "/dir/config";

  zx::vmo vmo;
  ASSERT_OK(::llcpp::fuchsia::debugdata::DebugData::Call::LoadConfig_Deprecated(
      zx::unowned_channel(client), fidl::StringView(strlen(kTestPath), kTestPath), &vmo));

  loop.Shutdown();
  vfs.reset();

  uint8_t config[sizeof(kTestData)];
  ASSERT_OK(vmo.read(config, 0, sizeof(config)));
  ASSERT_EQ(memcmp(config, kTestData, sizeof(config)), 0);
}

}  // anonymous namespace
