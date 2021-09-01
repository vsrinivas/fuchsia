// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugdata.h"

#include <fidl/fuchsia.debugdata/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/defer.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>

#include <string>
#include <unordered_map>
#include <vector>

#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "../sanitizers/fuchsia-io-constants.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

namespace {

const char kTestHelper[] = "/bin/debugdata-test-helper";

struct DebugData : public fidl::WireServer<fuchsia_debugdata::DebugData> {
  std::unordered_map<std::string, zx::vmo> data;
  std::unordered_map<std::string, zx::vmo> configs;

  void Publish(PublishRequestView request, PublishCompleter::Sync&) override {
    std::string name(request->data_sink.data(), request->data_sink.size());
    data.emplace(name, std::move(request->data));
  }

  void LoadConfig(LoadConfigRequestView request, LoadConfigCompleter::Sync& completer) override {
    std::string name(request->config_name.data(), request->config_name.size());
    if (auto it = configs.find(name); it != configs.end()) {
      completer.Reply(std::move(it->second));
    } else {
      completer.Close(ZX_ERR_NOT_FOUND);
    }
  }

  void Serve(async_dispatcher_t* dispatcher, std::unique_ptr<fs::SynchronousVfs>* vfs,
             zx::channel* client) {
    auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
    auto node = fbl::MakeRefCounted<fs::Service>([dispatcher, this](zx::channel channel) {
      return fidl::BindSingleInFlightOnly(dispatcher, std::move(channel), this);
    });
    dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_debugdata::DebugData>, node);

    zx::channel server;
    ASSERT_OK(zx::channel::create(0, client, &server));

    *vfs = std::make_unique<fs::SynchronousVfs>(dispatcher);
    ASSERT_OK((*vfs)->ServeDirectory(std::move(dir), std::move(server), fs::Rights::ReadWrite()));
  }
};

void RunHelper(const char* mode, const size_t action_count, const fdio_spawn_action_t* fdio_actions,
               int expected_return_code) {
  zx::job test_job;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &test_job));
  auto auto_call_kill_job = fit::defer([&test_job]() { test_job.kill(); });

  std::string test_helper = std::string(getenv("TEST_ROOT_DIR")) + kTestHelper;
  const char* args[] = {test_helper.c_str(), mode, nullptr};

  zx::process process;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  ASSERT_OK(fdio_spawn_etc(test_job.get(), FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_NAMESPACE,
                           args[0], args, nullptr, action_count, fdio_actions,
                           process.reset_and_get_address(), err_msg));

  ASSERT_OK(process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr));

  zx_info_process_t proc_info;
  ASSERT_OK(process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr));
  ASSERT_EQ(expected_return_code, proc_info.return_code);
}

void RunHelperWithSvc(const char* mode, zx::channel svc_handle, int expected_return_code) {
  fdio_spawn_action_t fdio_actions[] = {
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
          .ns =
              {
                  .prefix = "/svc",
                  .handle = svc_handle.release(),
              },
      },
  };
  RunHelper(mode, 1, fdio_actions, expected_return_code);
}

void RunHelperWithoutSvc(const char* mode, int expected_return_code) {
  RunHelper(mode, 0, nullptr, expected_return_code);
}

TEST(DebugDataTests, PublishData) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<fs::SynchronousVfs> vfs;
  zx::channel client;
  DebugData svc;
  ASSERT_NO_FATAL_FAILURES(svc.Serve(loop.dispatcher(), &vfs, &client));

  ASSERT_NO_FATAL_FAILURES(RunHelperWithSvc("publish_data", std::move(client), 0));

  ASSERT_OK(loop.RunUntilIdle());

  loop.Shutdown();
  vfs.reset();

  auto it = svc.data.find(kTestName);
  ASSERT_TRUE(it != svc.data.end());

  char content[sizeof(kTestData)];
  ASSERT_OK(it->second.read(content, 0, sizeof(content)));
  ASSERT_EQ(memcmp(content, kTestData, sizeof(kTestData)), 0);
}

TEST(DebugDataTests, PublishDataWithoutSvc) {
  ASSERT_NO_FATAL_FAILURES(RunHelperWithoutSvc("publish_data", 0));
}

TEST(DebugDataTests, LoadConfig) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<fs::SynchronousVfs> vfs;
  zx::channel client;
  DebugData svc;
  ASSERT_NO_FATAL_FAILURES(svc.Serve(loop.dispatcher(), &vfs, &client));
  ASSERT_OK(loop.StartThread("debugdata"));

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo));
  ASSERT_OK(vmo.write(kTestData, 0, sizeof(kTestData)));

  svc.configs.emplace(kTestName, std::move(vmo));

  ASSERT_NO_FATAL_FAILURES(RunHelperWithSvc("load_config", std::move(client), 0));

  loop.Shutdown();
  vfs.reset();
}

TEST(DebugDataTests, LoadConfigNotFound) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<fs::SynchronousVfs> vfs;
  zx::channel client;
  DebugData svc;
  ASSERT_NO_FATAL_FAILURES(svc.Serve(loop.dispatcher(), &vfs, &client));
  ASSERT_OK(loop.StartThread("debugdata"));

  ASSERT_NO_FATAL_FAILURES(RunHelperWithSvc("load_config", std::move(client), ZX_ERR_PEER_CLOSED));

  loop.Shutdown();
  vfs.reset();
}

TEST(DebugDataTests, LoadConfigWithoutSvc) {
  ASSERT_NO_FATAL_FAILURES(RunHelperWithoutSvc("load_config", ZX_ERR_BAD_HANDLE));
}

// debugdata.cc cannot use LLCPP (because it allocates with new/delete) so
// instead defines a local set of a few constants and structure definition in
// fuchsia-io-constants.h to call fuchsia.io.Directory/Open(). Confirm that the
// local copy matches the canonical definition here.
TEST(DebugDataTests, ConfirmMatchingFuchsiaIODefinitions) {
  namespace fio = fuchsia_io;

  ASSERT_EQ(fuchsia_io_MAX_PATH, fio::wire::kMaxPath);
  ASSERT_EQ(fuchsia_io_OPEN_RIGHT_READABLE, fio::wire::kOpenRightReadable);
  ASSERT_EQ(fuchsia_io_OPEN_RIGHT_WRITABLE, fio::wire::kOpenRightWritable);

  // LLCPP doesn't expose the ordinal, so we have to dig into internals to assert this.
  fidl::WireRequest<fio::Directory::Open> for_ordinal;
  ASSERT_EQ(fuchsia_io_DirectoryOpenOrdinal, for_ordinal._hdr.ordinal);

  zx::channel ch1, ch2;
  ASSERT_OK(zx::channel::create(0, &ch1, &ch2));
  fidl::ServerEnd<fio::Node> server_end(std::move(ch2));
  fidl::WireRequest<fio::Directory::Open>::OwnedEncodedMessage msg(0, 0, "", std::move(server_end));
  ASSERT_OK(msg.status());
  ASSERT_EQ(sizeof(fuchsia_io_DirectoryOpenRequest), msg.GetOutgoingMessage().CopyBytes().size());
}

}  // anonymous namespace
