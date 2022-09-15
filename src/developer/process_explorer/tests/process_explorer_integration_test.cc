// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/process/explorer/cpp/fidl.h>
#include <lib/fdio/spawn.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/socket.h>

#include <fbl/unique_fd.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

#include "src/developer/process_explorer/utils.h"
#include "src/lib/fsl/socket/strings.h"

namespace process_explorer {
namespace {

using namespace component_testing;

class RealmBuilderTest : public gtest::RealLoopFixture {};

class LocalRootJobImpl : public fuchsia::kernel::RootJob, public LocalComponent {
 public:
  explicit LocalRootJobImpl(fit::closure quit_loop, async_dispatcher_t* dispatcher)
      : quit_loop_(std::move(quit_loop)), dispatcher_(dispatcher), called_(false) {}

  // Override `Get` from `RootJob` protocol.
  void Get(GetCallback callback) override {
    // job_ acts as the root job
    // job_ has two child processes
    job_ = CreateJob();
    LaunchProcess(job_, "MockProcess1", {"/pkg/bin/mock_process"});
    LaunchProcess(job_, "MockProcess2", {"/pkg/bin/mock_process"});

    callback(std::move(job_));
    called_ = true;
    quit_loop_();
  }

  std::string ProcessesDataAsJson() {
    FillPeerOwnerKoid(processes_);
    std::string json = "{\"Processes\":[";
    for (const auto& process : processes_) {
      json.append("{\"koid\":").append(std::to_string(process.koid)).append(",");
      json.append("\"name\":\"").append(process.name).append("\",");
      json.append("\"objects\":[");
      for (const auto& object : process.objects) {
        json.append("{\"type\":").append(std::to_string(object.object_type));
        json.append(",\"koid\":").append(std::to_string(object.koid));
        json.append(",\"related_koid\":").append(std::to_string(object.related_koid));
        json.append(",\"peer_owner_koid\":").append(std::to_string(object.peer_owner_koid));
        json.append("},");
      }
      json.pop_back();
      json.append("]},");
    }
    json.pop_back();
    json.append("]}");

    for (const auto& stdin : processes_stdin_) {
      EXPECT_EQ(close(stdin.get()), 0);
    }
    return json;
  }

  // Override `Start` from `LocalComponent` class.
  void Start(std::unique_ptr<LocalComponentHandles> handles) override {
    // Keep reference to `handles` in member variable.
    // This class contains handles to the component's incoming
    // and outgoing capabilities.
    handles_ = std::move(handles);
    EXPECT_EQ(handles_->outgoing()->AddPublicService(bindings_.GetHandler(this, dispatcher_)),
              ZX_OK);
  }

  bool WasCalled() const { return called_; }

 private:
  zx::job CreateJob() {
    zx_handle_t default_job = zx_job_default();
    zx_handle_t job;
    if (auto status = zx_job_create(default_job, 0u, &job); status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to create job: " << zx_status_get_string(status);
    }
    return zx::job(job);
  }

  void LaunchProcess(const zx::job& job, const std::string name, std::vector<const char*> argv) {
    // fdio_spawn requires that argv has a nullptr in the end.
    std::vector<const char*> normalized_argv = argv;
    normalized_argv.push_back(nullptr);

    zx::process process;
    std::vector<fdio_spawn_action_t> actions;
    actions.push_back({.action = FDIO_SPAWN_ACTION_SET_NAME, .name = {.data = name.c_str()}});

    int pipes[2];
    pipe2(pipes, 0);
    fbl::unique_fd stdin_fd(pipes[0]);
    actions.push_back({
        .action = FDIO_SPAWN_ACTION_TRANSFER_FD,
        .fd =
            {
                .local_fd = pipes[1],
                .target_fd = STDIN_FILENO,
            },
    });
    pipe2(pipes, 0);
    fbl::unique_fd stdout_fd(pipes[0]);
    actions.push_back({
        .action = FDIO_SPAWN_ACTION_TRANSFER_FD,
        .fd =
            {
                .local_fd = pipes[1],
                .target_fd = STDOUT_FILENO,
            },
    });

    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    if (auto status = fdio_spawn_etc(job.get(), FDIO_SPAWN_CLONE_ALL, argv[0], argv.data(),
                                     nullptr,  // Environ
                                     actions.size(), actions.data(),
                                     process.reset_and_get_address(), err_msg);
        status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to spawn command: " << zx_status_get_string(status);
    }

    char buffer[1];
    EXPECT_EQ(read(stdout_fd.get(), buffer, 1), 0);
    AddProcessToList(process.borrow(), name);
    processes_stdin_.push_back(std::move(stdin_fd));
  }

  void AddProcessToList(zx::unowned_process process, std::string name) {
    zx_info_handle_basic_t info;
    if (auto status =
            process->get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
        status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to get process koid: " << zx_status_get_string(status);
    }

    std::vector<zx_info_handle_extended_t> handles;
    if (auto status = GetHandles(process->borrow(), &handles); status != ZX_OK) {
      FX_LOGS(ERROR) << "Unable to get handles for process: " << zx_status_get_string(status);
    }

    std::vector<KernelObject> process_objects;
    for (const auto& handle : handles) {
      process_objects.push_back(
          {handle.type, handle.koid, handle.related_koid, handle.peer_owner_koid});
    }

    processes_.push_back({info.koid, name, process_objects});
  }

  zx::job job_;
  std::vector<fbl::unique_fd> processes_stdin_;
  std::vector<Process> processes_;
  fit::closure quit_loop_;
  async_dispatcher_t* dispatcher_;
  fidl::BindingSet<fuchsia::kernel::RootJob> bindings_;
  std::unique_ptr<LocalComponentHandles> handles_;
  bool called_;
};

TEST_F(RealmBuilderTest, RouteServiceToComponent) {
  auto builder = RealmBuilder::Create();
  builder.AddChild("process_explorer", "#meta/process_explorer.cm");
  LocalRootJobImpl mock_root_job(QuitLoopClosure(), dispatcher());
  builder.AddLocalChild("root_job", &mock_root_job);

  builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::kernel::RootJob::Name_}},
                         .source = ChildRef{"root_job"},
                         .targets = {ChildRef{"process_explorer"}}});
  builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_}},
                         .source = ParentRef(),
                         .targets = {ChildRef{"process_explorer"}}});
  builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::process::explorer::Query::Name_}},
                         .source = ChildRef{"process_explorer"},
                         .targets = {ParentRef()}});

  auto realm = builder.Build(dispatcher());
  fuchsia::process::explorer::QueryPtr explorer;
  ASSERT_EQ(realm.Connect(explorer.NewRequest()), ZX_OK);
  zx::socket socket[2];
  ASSERT_EQ(zx::socket::create(0u, &socket[0], &socket[1]), ZX_OK);
  explorer->WriteJsonProcessesData(std::move(socket[0]));
  RunLoop();

  std::string s;
  fsl::BlockingCopyToString(std::move(socket[1]), &s);
  ASSERT_EQ(s, mock_root_job.ProcessesDataAsJson());
  EXPECT_TRUE(mock_root_job.WasCalled());
}

}  // namespace
}  // namespace process_explorer
