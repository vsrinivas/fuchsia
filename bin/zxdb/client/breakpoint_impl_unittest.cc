// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "garnet/bin/zxdb/client/breakpoint_impl.h"
#include "garnet/bin/zxdb/client/process_impl.h"
#include "garnet/bin/zxdb/client/remote_api_test.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/symbols/mock_module_symbols.h"
#include "garnet/bin/zxdb/client/target_impl.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

using debug_ipc::MessageLoop;

class BreakpointSink : public RemoteAPI {
 public:
  using AddPair = std::pair<
      debug_ipc::AddOrChangeBreakpointRequest,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)>>;
  using RemovePair = std::pair<
      debug_ipc::RemoveBreakpointRequest,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)>>;

  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb)
      override {
    adds.push_back(std::make_pair(request, cb));

    MessageLoop::Current()->PostTask(
        [cb]() { cb(Err(), debug_ipc::AddOrChangeBreakpointReply()); });
  }

  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb)
      override {
    removes.push_back(std::make_pair(request, cb));

    MessageLoop::Current()->PostTask(
        [cb]() { cb(Err(), debug_ipc::RemoveBreakpointReply()); });
  }

  std::vector<AddPair> adds;
  std::vector<RemovePair> removes;
};

class BreakpointImplTest : public RemoteAPITest {
 public:
  BreakpointImplTest() = default;
  ~BreakpointImplTest() override = default;

  // Emulates a synchronous call to SetSettings on a breakpoint.
  Err SyncSetSettings(Breakpoint& bp, const BreakpointSettings& settings) {
    Err out_err;
    bp.SetSettings(settings, [&out_err](const Err& new_err) {
      out_err = new_err;
      MessageLoop::Current()->QuitNow();
    });
    loop().Run();
    return out_err;
  }

  BreakpointSink& sink() { return *sink_; }

 protected:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto sink = std::make_unique<BreakpointSink>();
    sink_ = sink.get();
    return std::move(sink);
  }

 private:
  BreakpointSink* sink_;  // Owned by the session.
};

}  // namespace

TEST_F(BreakpointImplTest, DynamicLoading) {
  BreakpointImpl bp(&session(), false);

  // Make a disabled symbolic breakpoint.
  const std::string kFunctionName = "DoThings";
  BreakpointSettings in;
  in.enabled = false;
  in.scope = BreakpointSettings::Scope::kSystem;
  in.location.type = InputLocation::Type::kSymbol;
  in.location.symbol = kFunctionName;

  // Setting the disabled settings shouldn't update the backend.
  Err err = SyncSetSettings(bp, in);
  EXPECT_FALSE(err.has_error());
  ASSERT_TRUE(sink().adds.empty());

  // Setting enabled settings with no processes should not update the backend.
  in.enabled = true;
  err = SyncSetSettings(bp, in);
  EXPECT_FALSE(err.has_error());
  ASSERT_TRUE(sink().adds.empty());

  TargetImpl* target = session().system_impl().GetTargetImpls()[0];

  // Create a process for it. Since the process doesn't resolve the symbol
  // yet, no messages should be sent.
  const uint64_t koid = 5678;
  target->CreateProcessForTesting(koid, "test");
  ASSERT_TRUE(sink().adds.empty());

  // Make two fake modules. The first will resolve the function to two
  // locations, the second will resolve nothing.
  const uint64_t kRelAddress1 = 0x78456345;
  const uint64_t kRelAddress2 = 0x12345678;
  std::unique_ptr<MockModuleSymbols> module1 =
      std::make_unique<MockModuleSymbols>("myfile1.so");
  std::unique_ptr<MockModuleSymbols> module2 =
      std::make_unique<MockModuleSymbols>("myfile2.so");
  module1->AddSymbol(kFunctionName,
                     std::vector<uint64_t>{kRelAddress1, kRelAddress2});

  // Cause the process to load the module. We have to keep the module_ref
  // alive for this to stay cached in the SystemSymbols.
  const std::string kBuildID1 = "abcd";
  const std::string kBuildID2 = "zyxw";
  fxl::RefPtr<SystemSymbols::ModuleRef> module1_ref =
      session().system().GetSymbols()->InjectModuleForTesting(
          kBuildID1, std::move(module1));
  fxl::RefPtr<SystemSymbols::ModuleRef> module2_ref =
      session().system().GetSymbols()->InjectModuleForTesting(
          kBuildID2, std::move(module2));

  // Cause the process to load module 1.
  std::vector<debug_ipc::Module> modules;
  const uint64_t kModule1Base = 0x1000000;
  debug_ipc::Module load1;
  load1.name = "test";
  load1.base = kModule1Base;
  load1.build_id = kBuildID1;
  modules.push_back(load1);
  target->process()->OnModules(modules, std::vector<uint64_t>());

  // That should have notified the breakpoint which should have added the two
  // addresses to the backend.
  ASSERT_FALSE(sink().adds.empty());
  debug_ipc::AddOrChangeBreakpointRequest out = sink().adds[0].first;
  EXPECT_FALSE(out.breakpoint.one_shot);
  EXPECT_EQ(debug_ipc::Stop::kAll, out.breakpoint.stop);

  // Both locations should be for the same process, with no thread restriction.
  ASSERT_EQ(2u, out.breakpoint.locations.size());
  EXPECT_EQ(koid, out.breakpoint.locations[0].process_koid);
  EXPECT_EQ(koid, out.breakpoint.locations[1].process_koid);
  EXPECT_EQ(0u, out.breakpoint.locations[0].thread_koid);
  EXPECT_EQ(0u, out.breakpoint.locations[1].thread_koid);

  // Addresses could be in either order. They should be absolute.
  const uint64_t kAddress1 = kModule1Base + kRelAddress1;
  const uint64_t kAddress2 = kModule1Base + kRelAddress2;
  EXPECT_TRUE((out.breakpoint.locations[0].address == kAddress1 &&
               out.breakpoint.locations[1].address == kAddress2) ||
              (out.breakpoint.locations[0].address == kAddress2 &&
               out.breakpoint.locations[1].address == kAddress1));

  // Adding another module with nothing to resolve should send no messages.
  sink().adds.clear();
  const uint64_t kModule2Base = 0x2000000;
  debug_ipc::Module load2;
  load2.name = "test2";
  load2.base = kModule2Base;
  load2.build_id = kBuildID2;
  modules.push_back(load2);
  target->process()->OnModules(modules, std::vector<uint64_t>());
  ASSERT_TRUE(sink().adds.empty());

  // Disabling should send the delete message.
  ASSERT_TRUE(sink().removes.empty());  // Should have none so far.
  in.enabled = false;
  err = SyncSetSettings(bp, in);
  EXPECT_FALSE(err.has_error());
  ASSERT_TRUE(sink().adds.empty());
  ASSERT_EQ(1u, sink().removes.size());
  EXPECT_EQ(out.breakpoint.breakpoint_id,
            sink().removes[0].first.breakpoint_id);
}

// Tests that address breakpoints are enabled immediately even when no symbols
// are available.
TEST_F(BreakpointImplTest, Address) {
  // TargetImpl target(&session().system_impl());
  auto target_impls = session().system_impl().GetTargetImpls();
  ASSERT_EQ(1u, target_impls.size());
  TargetImpl* target = target_impls[0];
  const uint64_t kProcessKoid = 6789;
  target->CreateProcessForTesting(kProcessKoid, "test");

  BreakpointImpl bp(&session(), false);

  const uint64_t kAddress = 0x123456780;
  BreakpointSettings in;
  in.enabled = true;
  in.scope = BreakpointSettings::Scope::kTarget;
  in.scope_target = target;
  in.location.type = InputLocation::Type::kAddress;
  in.location.address = kAddress;

  Err err = SyncSetSettings(bp, in);
  EXPECT_FALSE(err.has_error());

  // Check the message was sent.
  ASSERT_EQ(1u, sink().adds.size());
  debug_ipc::AddOrChangeBreakpointRequest out = sink().adds[0].first;
  EXPECT_FALSE(out.breakpoint.one_shot);
  EXPECT_EQ(debug_ipc::Stop::kAll, out.breakpoint.stop);
  EXPECT_EQ(1u, out.breakpoint.locations.size());
  // EXPECT_EQ(, out.breakpoint.locations[0].process_koid);
}

}  // namespace zxdb
