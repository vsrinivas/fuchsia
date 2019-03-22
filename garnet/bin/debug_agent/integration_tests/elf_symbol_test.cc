// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdio.h>

#include "garnet/bin/debug_agent/integration_tests/message_loop_wrapper.h"
#include "garnet/bin/debug_agent/integration_tests/mock_stream_backend.h"
#include "garnet/bin/debug_agent/integration_tests/so_wrapper.h"
#include "lib/fxl/logging.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/shared/message_loop_zircon.h"
#include "src/developer/debug/shared/zx_status.h"

namespace debug_agent {

namespace {

// This test is an integration test to verify that the debug agent is able to
// successfully locate Elf symbols after linking.
//
// 1. Launch a process (through RemoteAPI::OnLaunch) control by the debug agent.
//
// 2. Get the module notication (NotifyModules message) for the process launched
//    in (1). We look over the modules for a module (debug_agent_test_so) that
//    was loaded by this newly created process.
//
// 3. Request the symbol tables for the process and look for a particular entry.
//
// 4. Success!

// The exported symbol we're going to ger a symbol for.
const char* kExportedFunctionName = "InsertBreakpointFunction";
const char* kSecondExportedFunctionName = "AnotherFunctionForKicks";

// The test .so we load in order to search the offset of the exported symbol
// within it.
const char* kTestSo = "debug_agent_test_so.so";

// The test executable the debug agent is going to launch. This is linked with
// |kTestSo|, meaning that the offset within that .so will be valid into the
// loaded module of this executable.
/* const char* kTestExecutableName = "breakpoint_test_exe"; */
const char* kTestExecutablePath = "/pkg/bin/breakpoint_test_exe";
const char* kModuleToSearch = "libdebug_agent_test_so.so";

class ElfSymbolStreamBackend : public MockStreamBackend {
 public:
  ElfSymbolStreamBackend(debug_ipc::MessageLoop* loop) : loop_(loop) {}

  uint64_t so_test_base_addr() const { return so_test_base_addr_; }
  const std::string& so_test_build_id() const { return so_test_build_id_; }
  const debug_ipc::SymbolTablesReply& reply() const { return reply_; }

  // The messages we're interested in handling ---------------------------------

  // Searches the loaded modules for specific one.
  void HandleNotifyModules(debug_ipc::NotifyModules modules) override {
    for (auto& module : modules.modules) {
      if (module.name == kModuleToSearch) {
        so_test_base_addr_ = module.base;
        so_test_build_id_ = module.build_id;
        break;
      }
    }
    loop_->QuitNow();
  }

 private:
  debug_ipc::MessageLoop* loop_;
  uint64_t so_test_base_addr_ = 0;
  std::string so_test_build_id_;
  debug_ipc::SymbolTablesReply reply_;
};

}  // namespace

TEST(ElfSymbol, Lookup) {
  // We attempt to load the pre-made .so.
  SoWrapper so_wrapper;
  ASSERT_TRUE(so_wrapper.Init(kTestSo)) << "Could not load so " << kTestSo;

  uint64_t symbol_offset =
      so_wrapper.GetSymbolOffset(kTestSo, kExportedFunctionName);
  ASSERT_NE(symbol_offset, 0u);

  MessageLoopWrapper loop_wrapper;
  {
    auto* loop = loop_wrapper.loop();
    // This stream backend will take care of intercepting the calls from the
    // debug agent.
    ElfSymbolStreamBackend mock_stream_backend(loop);
    RemoteAPI* remote_api = mock_stream_backend.remote_api();

    // We launch the test binary.
    debug_ipc::LaunchRequest launch_request = {};
    launch_request.argv.push_back(kTestExecutablePath);
    launch_request.inferior_type = debug_ipc::InferiorType::kBinary;
    debug_ipc::LaunchReply launch_reply;
    remote_api->OnLaunch(launch_request, &launch_reply);
    ASSERT_EQ(launch_reply.status, ZX_OK)
        << "Expected ZX_OK, Got: "
        << debug_ipc::ZxStatusToString(launch_reply.status);

    // We run the look to get the notifications sent by the agent.
    // The stream backend will stop the loop once it has received the modules
    // notification.
    loop->Run();

    // We should have found the correct module by now.
    ASSERT_NE(mock_stream_backend.so_test_base_addr(), 0u);

    // We request symbol tables for our module.
    debug_ipc::SymbolTablesRequest symbols_request;
    symbols_request.process_koid = launch_reply.process_koid;
    symbols_request.base = mock_stream_backend.so_test_base_addr();
    symbols_request.build_id = mock_stream_backend.so_test_build_id();
    debug_ipc::SymbolTablesReply symbols_reply;
    remote_api->OnSymbolTables(symbols_request, &symbols_reply);

    uint64_t exported_function_value = 0;
    uint64_t second_exported_function_value = 0;

    for (const auto& symbol : symbols_reply.symbols) {
      if (symbol.name == kExportedFunctionName) {
        exported_function_value = symbol.value;
      } else if (symbol.name == kSecondExportedFunctionName) {
        second_exported_function_value = symbol.value;
      }
    }

    // It's not clear what non-flaky things we can test about the value other
    // than this.
    EXPECT_NE(0U, exported_function_value);
    EXPECT_NE(0U, second_exported_function_value);
  }
}

}  // namespace debug_agent
