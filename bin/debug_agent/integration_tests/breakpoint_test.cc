// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
//
// found in the LICENSE file.

#include <dlfcn.h>
#include <link.h>
#include <stdio.h>

#include <gtest/gtest.h>

#include "garnet/bin/debug_agent/arch.h"
#include "garnet/bin/debug_agent/debug_agent.h"
#include "garnet/lib/debug_ipc/client_protocol.h"
#include "garnet/lib/debug_ipc/helper/message_loop_zircon.h"
#include "garnet/lib/debug_ipc/message_reader.h"

#include "lib/component/cpp/environment_services_helper.h"
#include "lib/fxl/logging.h"

namespace debug_agent {

// This test is an integration test to verify that the debug agent is able to
// successfully set breakpoints to Zircon and get the correct responses.
// This particular test does the following script:
//
// 1. Load a pre-made .so (debug_agent_test_so) and search for a particular
//    exported function. By also getting the loaded base address of the .so, we
//    can get the offset of the function within the module.
//
// 2. Launch a process (through RemoteAPI::OnLaunch) control by the debug agent.
//
// 3. Get the module notication (NotifyModules message) for the process launched
//    in (2). We look over the modules for the same module (debug_agent_test_so)
//    that was loaded by this newly created process.
//    With the base address of this module, we can use the offset calculated in
//    (1) and get the actual loaded address for the exported function within
//    the process.
//
// 4. Set a breakpoint on that address and resume the process. The test program
//    is written such that it will call the searched symbol, so should hit the
//    breakpoint.
//
// 5. Verify that we get a breakpoint exception on that address.
//
// 6. Success!

// The exported symbol we're going to put the breakpoint on.
const char* kExportedFunctionName = "ExportedFunction";

// The test .so we load in order to search the offset of the exported symbol
// within it.
const char* kTestSo = "debug_agent_test_so.so";

// The test executable the debug agent is going to launch. This is linked with
// |kTestSo|, meaning that the offset within that .so will be valid into the
// loaded module of this executable.
const char* kTestExecutableName = "debug_agent_so_test";
const char* kTestExecutablePath = "/pkg/bin/debug_agent_so_test";

// This class is meant to receive the raw messages outputted by the debug agent.
// The agent's stream calls this backend to output the data and verifies that
// all the content is sent.
//
// We use this class to intercept the messages sent back from the agent and
// react accordingly. This class is kinda hardcoded for this tests, as different
// integration tests care about different messages. If there are more tests that
// require this kind of interception, this class should be separated and
// generalized.
class MockStreamBackend : public debug_ipc::StreamBuffer::Writer {
 public:
  MockStreamBackend(debug_ipc::MessageLoop* loop) : loop_(loop) {}

  uint64_t so_test_base_addr() const { return so_test_base_addr_; }
  debug_ipc::NotifyException exception() const { return exception_; }

  // The stream will call this function to send the data to whatever backend it
  // is connected to. It returns how much of the input message it could actually
  // write. For this tests purposes, we always read the whole message.
  size_t ConsumeStreamBufferData(const char* data, size_t len) override {
    // We assume we always get a header.
    debug_ipc::MsgHeader header = *(const debug_ipc::MsgHeader*)data;

    // Buffer for the message.
    std::vector<char> msg_buffer;
    msg_buffer.reserve(len);
    msg_buffer.insert(msg_buffer.end(), data, data + len);

    // Dispatch the messages we find interesting.
    debug_ipc::MessageReader reader(std::move(msg_buffer));
    switch (header.type) {
      case debug_ipc::MsgHeader::Type::kNotifyModules:
        HandleNotifyModules(std::move(reader));
        // We make the test continue.
        loop_->QuitNow();
        break;
      case debug_ipc::MsgHeader::Type::kNotifyException:
        HandleNotifyException(std::move(reader));
        // We make the test continue.
        loop_->QuitNow();
        break;
      default:
        // We are not interested in breaking out of the loop for other
        // notifications.
        break;
    }

    // Say we read the whole message.
    return len;
  }

  // Searches the loaded modules for specific one.
  void HandleNotifyModules(debug_ipc::MessageReader reader) {
    debug_ipc::NotifyModules modules;
    if (!debug_ipc::ReadNotifyModules(&reader, &modules))
      return;
    for (auto& module : modules.modules) {
      if (module.name.find(kTestExecutableName) != std::string::npos) {
        so_test_base_addr_ = module.base;
        break;
      }
    }
  }

  // Records the exception given from the debug agent.
  void HandleNotifyException(debug_ipc::MessageReader reader) {
    debug_ipc::NotifyException exception;
    if (!debug_ipc::ReadNotifyException(&reader, &exception))
      return;
    exception_ = exception;
  }

 private:
  uint64_t so_test_base_addr_ = 0;
  debug_ipc::NotifyException exception_ = {};

  debug_ipc::MessageLoop* loop_;
};

struct IteratePhdrCallbackControl {
  const char* searched_so_name;
  uint64_t so_base_address;
};

// This callback will be called by dl_iterate_phdr for each module loaded into
// the current process. We use this to search for the module opened through
// dlopen.
//
// dl_iterate_phdr iterates over all the modules until one of them returns
// non-zero (signal to stop) or when there are no more modules left.
int IteratePhdrCallback(struct dl_phdr_info* info, size_t size, void* user) {
  IteratePhdrCallbackControl* control =
      reinterpret_cast<IteratePhdrCallbackControl*>(user);

  // We verify the current .so being iterated vs the one we're searching for.
  std::string so_name(info->dlpi_name);
  if (so_name.find(control->searched_so_name) != std::string::npos) {
    (*control).so_base_address = info->dlpi_addr;
    return 1;   // We end the iteration.
  }

  // Continue the iteration.
  return 0;
}

// Minor utility to ensure loaded .so are freed.
class SoWrapper {
 public:
  explicit SoWrapper(const char* so_name) {
    so_ = dlopen(so_name, RTLD_GLOBAL);
  }
  ~SoWrapper() {
    if (so_)
      dlclose(so_);
  }

  operator bool() const { return so_ != nullptr; }
  void* operator*() const { return so_; }

 private:
  void* so_;
};

TEST(BreakpointIntegration, CorrectSetsSWBreakpoint) {
  // We attempt to load the pre-made .so.
  SoWrapper so(kTestSo);
  if (!so)
    FAIL() << "Could not load " << kTestSo << ": " << dlerror();

  // We iterate over the elf headers of the loaded so and search for a
  // particular module within it. If we find it, we record the base address of
  // the module for later canculating the offset of a symbol from this base.
  IteratePhdrCallbackControl control = {};
  control.searched_so_name = kTestSo;
  if (dl_iterate_phdr(IteratePhdrCallback, &control) == 0)
    FAIL() << "Did not find debug_agent_test_so.";

  // We search for a particular symbol within the .so.
  void* function_ptr = dlsym(*so, kExportedFunctionName);
  if (!function_ptr)
    FAIL() << "Could not find symbol \"" << kExportedFunctionName << "\"";

  // We calculate the offset of the searched symbol within the .so. This offset
  // will be same in a binary that has linked with the same module. We only need
  // to know the base address of that module. We get that through the notify
  // modules message from the debug agent.
  uint64_t function_offset = (uint64_t)function_ptr - control.so_base_address;

  debug_ipc::MessageLoopZircon loop;
  loop.Init();
  {
    // Create a mock backed the debug agent's stream will write to. This is
    // mocking what the socket would do in the normal environment.
    MockStreamBackend mock_stream_backend(&loop);
    debug_ipc::StreamBuffer stream;
    stream.set_writer(&mock_stream_backend);

    // Create a debug agent that's "connected" to our mock environment.
    // This will have the correct setup to talk to Zircon through the component
    // environment.
    auto environment_services = component::GetEnvironmentServices();
    DebugAgent debug_agent(&stream, std::move(environment_services));
    // The RemoteAPI is needed because the debug agent API is private.
    RemoteAPI* remote_api = &debug_agent;

    // We launch the test binary.
    debug_ipc::LaunchRequest launch_request = {};
    launch_request.argv.push_back(kTestExecutablePath);
    debug_ipc::LaunchReply launch_reply;
    remote_api->OnLaunch(launch_request, &launch_reply);
    ASSERT_EQ(launch_reply.status, static_cast<uint32_t>(ZX_OK));

    // We run the look to get the notifications sent by the agent.
    // The stream backend will stop the loop once it has received the modules
    // notification.
    loop.Run();

    // We should have found the correct module by now.
    ASSERT_NE(mock_stream_backend.so_test_base_addr(), 0u);

    // We get the offset of the loaded function within the process space.
    uint64_t module_base = mock_stream_backend.so_test_base_addr();
    uint64_t module_function = module_base + function_offset;

    // We add a breakpoint in that address.
    constexpr uint32_t kBreakpointId = 1234u;
    debug_ipc::ProcessBreakpointSettings location = {};
    location.process_koid = launch_reply.process_koid;
    location.address = module_function;

    debug_ipc::AddOrChangeBreakpointRequest breakpoint_request = {};
    breakpoint_request.breakpoint.breakpoint_id = kBreakpointId;
    breakpoint_request.breakpoint.one_shot = true;
    breakpoint_request.breakpoint.locations.push_back(location);

    debug_ipc::AddOrChangeBreakpointReply breakpoint_reply;
    remote_api->OnAddOrChangeBreakpoint(breakpoint_request,
                                                   &breakpoint_reply);
    ASSERT_EQ(breakpoint_reply.status, static_cast<uint32_t>(ZX_OK));

    // Resume the process now that the breakpoint is installed.
    debug_ipc::ResumeRequest resume_request;
    resume_request.process_koid = launch_reply.process_koid;
    debug_ipc::ResumeReply resume_reply;
    remote_api->OnResume(resume_request, &resume_reply);

    // The loop will run until the stream backend receives an exception
    // notification.
    loop.Run();

    // We should have received an exception now.
    debug_ipc::NotifyException exception = mock_stream_backend.exception();
    EXPECT_EQ(exception.process_koid, launch_reply.process_koid);
    EXPECT_EQ(exception.type, debug_ipc::NotifyException::Type::kSoftware);
    ASSERT_EQ(exception.hit_breakpoints.size(), 1u);

    // Verify that the correct breakpoint was hit.
    auto& breakpoint = exception.hit_breakpoints[0];
    EXPECT_EQ(breakpoint.breakpoint_id, kBreakpointId);
    EXPECT_EQ(breakpoint.hit_count, 1u);
    EXPECT_TRUE(breakpoint.should_delete);
  }
  loop.Cleanup();
}

}  // namespace debug_agent
