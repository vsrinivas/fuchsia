// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/integration_tests/message_loop_wrapper.h"
#include "src/developer/debug/debug_agent/integration_tests/so_wrapper.h"
#include "src/developer/debug/debug_agent/local_stream_backend.h"
#include "src/developer/debug/debug_agent/zircon_system_interface.h"
#include "src/developer/debug/shared/logging/file_line_function.h"
#include "src/developer/debug/shared/zx_status.h"

namespace debug_agent {

namespace {

const char kSoName[] = "debug_agent_test_so.so";

class DynamicLoaderStreamBackend : public LocalStreamBackend {
 public:
  DynamicLoaderStreamBackend(debug_ipc::MessageLoop* loop) : loop_(loop) {}

  void set_remote_api(RemoteAPI* remote_api) { remote_api_ = remote_api; }

  // LocalStreamBackend implementation:
  void HandleNotifyThreadStarting(debug_ipc::NotifyThread thread) override;
  void HandleNotifyModules(debug_ipc::NotifyModules modules) override;
  void HandleNotifyProcessExiting(debug_ipc::NotifyProcessExiting) override;

 private:
  void ResumeAll();

  bool HasModuleForFile(const debug_ipc::NotifyModules& modules, const char* file_name);

  enum class Stage {
    kWaitingForThread,
    kWaitingForModules,
    kWaitingForLoad,
    kWaitingForUnload,
    kWaitingForExit,
    kDone,
  };

  Stage stage_ = Stage::kWaitingForThread;

  debug_ipc::MessageLoop* loop_ = nullptr;
  RemoteAPI* remote_api_ = nullptr;

  zx_koid_t process_koid_ = 0;
  zx_koid_t thread_koid_ = 0;
};

void DynamicLoaderStreamBackend::HandleNotifyThreadStarting(debug_ipc::NotifyThread thread) {
  if (stage_ == Stage::kWaitingForThread) {
    process_koid_ = thread.record.id.process;
    thread_koid_ = thread.record.id.thread;
    stage_ = Stage::kWaitingForModules;
    ResumeAll();
  } else {
    ADD_FAILURE() << "Got starting message when not expected";
    loop_->QuitNow();
  }
}

void DynamicLoaderStreamBackend::HandleNotifyModules(debug_ipc::NotifyModules modules) {
  if (stage_ == Stage::kWaitingForModules) {
    EXPECT_FALSE(HasModuleForFile(modules, kSoName));
    stage_ = Stage::kWaitingForLoad;
    ResumeAll();
  } else if (stage_ == Stage::kWaitingForLoad) {
    EXPECT_TRUE(HasModuleForFile(modules, kSoName));

// TODO(bug 58371) our dynamic loader does not implement dlclose() so that we never get a
// notification that dynamic libraries are unloaded. When that's implemented, this corresponding
// code should be used to test the debugger's behavior in that context.
#if 0
    stage_ = Stage::kWaitingForUnload;
    ResumeAll();
  } else if (stage_ == Stage::kWaitingForUnload) {
    EXPECT_FALSE(HasModuleForFile(modules, kSoName));
#endif

    stage_ = Stage::kWaitingForExit;
    ResumeAll();
  } else {
    ADD_FAILURE() << "Unexpected NotifyModules call.";
    loop_->QuitNow();
  }
}

void DynamicLoaderStreamBackend::HandleNotifyProcessExiting(debug_ipc::NotifyProcessExiting) {
  if (stage_ == Stage::kWaitingForExit) {
    stage_ = Stage::kDone;
  } else {
    ADD_FAILURE() << "Process exited before getting the right notifications.";
  }

  loop_->QuitNow();
}

void DynamicLoaderStreamBackend::ResumeAll() {
  // Delay the request because we might be called in some notification callbacks and sending
  // requests in callbacks might not be expected by the debug_agent.
  loop_->PostTask(FROM_HERE, [this]() {
    debug_ipc::ResumeRequest resume_request;
    debug_ipc::ResumeReply resume_reply;
    remote_api_->OnResume(resume_request, &resume_reply);
  });
}

bool DynamicLoaderStreamBackend::HasModuleForFile(const debug_ipc::NotifyModules& modules,
                                                  const char* file_name) {
  for (const auto& m : modules.modules) {
    if (m.name == file_name)
      return true;
  }
  return false;
}

}  // namespace

// Tests that dynamic library load and unload events are caught by the debug agent and the proper
// notifications are issued.
TEST(DynamicLoader, LoadUnload) {
  MessageLoopWrapper loop_wrapper;
  {
    auto* loop = loop_wrapper.loop();

    DynamicLoaderStreamBackend backend(loop);
    DebugAgent agent(std::make_unique<ZirconSystemInterface>());

    agent.Connect(&backend.stream());
    backend.set_remote_api(&agent);

    debug_ipc::LaunchRequest launch_request = {};
    launch_request.argv = {"/pkg/bin/load_so_exe"};
    launch_request.inferior_type = debug_ipc::InferiorType::kBinary;

    debug_ipc::LaunchReply launch_reply;
    agent.OnLaunch(launch_request, &launch_reply);

// TODO(fxbug.dev/81801) Currently binary loading is disabled in zircon_binary_launcher.cc because
// it hangs for normal binaries. This test actually passes if the disabled code in the launcher is
// enabled because the load_so_exe binary is in our package and can be loaded. But since that code
// exits early, this is disabled.
#if 0
    ASSERT_TRUE(launch_reply.status.ok());
    loop->Run();
#else
    // Currently just validate that the correct error was sent.
    ASSERT_EQ(launch_reply.status.type(), debug::Status::kNotSupported);
#endif
  }
}

}  // namespace debug_agent
