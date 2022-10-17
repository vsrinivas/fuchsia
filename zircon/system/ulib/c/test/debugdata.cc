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

constexpr char kTestHelper[] = "/pkg/bin/debugdata-test-helper";

struct Publisher : public fidl::WireServer<fuchsia_debugdata::Publisher> {
  std::unordered_map<std::string, zx::vmo> data;

  void Publish(PublishRequestView request, PublishCompleter::Sync&) override {
    std::string name(request->data_sink.data(), request->data_sink.size());
    data.emplace(name, std::move(request->data));
  }

  void Serve(async_dispatcher_t* dispatcher, std::unique_ptr<fs::SynchronousVfs>* vfs,
             fidl::ClientEnd<fuchsia_io::Directory>* client_end) {
    auto dir = fbl::MakeRefCounted<fs::PseudoDir>();
    auto node = fbl::MakeRefCounted<fs::Service>(
        [dispatcher, this](fidl::ServerEnd<fuchsia_debugdata::Publisher> server_end) {
          return fidl::BindSingleInFlightOnly(dispatcher, std::move(server_end), this);
        });

    dir->AddEntry(fidl::DiscoverableProtocolName<fuchsia_debugdata::Publisher>, node);

    zx::result server_end = fidl::CreateEndpoints(client_end);
    ASSERT_OK(server_end.status_value());

    *vfs = std::make_unique<fs::SynchronousVfs>(dispatcher);
    ASSERT_OK((*vfs)->ServeDirectory(std::move(dir), std::move(server_end.value()),
                                     fs::Rights::ReadWrite()));
  }
};

void RunHelper(const char* mode, const size_t action_count, const fdio_spawn_action_t* fdio_actions,
               int expected_return_code) {
  zx::job test_job;
  ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &test_job));
  auto auto_call_kill_job = fit::defer([&test_job]() { test_job.kill(); });

  const char* args[] = {kTestHelper, mode, nullptr};

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

void RunHelperWithSvc(const char* mode, fidl::ClientEnd<fuchsia_io::Directory> client_end,
                      int expected_return_code) {
  fdio_spawn_action_t fdio_actions[] = {
      fdio_spawn_action_t{
          .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
          .ns =
              {
                  .prefix = "/svc",
                  .handle = client_end.TakeChannel().release(),
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
  fidl::ClientEnd<fuchsia_io::Directory> client_end;
  Publisher svc;
  ASSERT_NO_FATAL_FAILURE(svc.Serve(loop.dispatcher(), &vfs, &client_end));

  ASSERT_NO_FATAL_FAILURE(RunHelperWithSvc("publish_data", std::move(client_end), 0));

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
  ASSERT_NO_FATAL_FAILURE(RunHelperWithoutSvc("publish_data", 0));
}

// debugdata.cc cannot use LLCPP (because it allocates with new/delete) so
// instead defines a local set of a few constants and structure definition in
// fuchsia-io-constants.h to call fuchsia.io.Directory/Open(). Confirm that the
// local copy matches the canonical definition here.
TEST(DebugDataTests, ConfirmMatchingFuchsiaIODefinitions) {
  namespace fio = fuchsia_io;

  static_assert(fuchsia_io_DirectoryOpenOrdinal ==
                fidl::internal::WireOrdinal<fio::Directory::Open>::value);

  zx::result endpoints = fidl::CreateEndpoints<fio::Node>();
  ASSERT_OK(endpoints.status_value());
  fidl::internal::TransactionalRequest<fio::Directory::Open> request{
      {}, 0, fidl::StringView(), std::move(endpoints->server)};
  fidl::unstable::OwnedEncodedMessage<fidl::internal::TransactionalRequest<fio::Directory::Open>>
      msg{&request};
  ASSERT_OK(msg.status());
  ASSERT_EQ(sizeof(fuchsia_io_DirectoryOpenRequest), msg.GetOutgoingMessage().CopyBytes().size());
}

}  // anonymous namespace
