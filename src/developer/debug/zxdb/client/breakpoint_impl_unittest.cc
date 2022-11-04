// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/breakpoint_impl.h"

#include <utility>

#include <gtest/gtest.h>

#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/breakpoint_observer.h"
#include "src/developer/debug/zxdb/client/process_impl.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/target_impl.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/index_test_support.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"

namespace zxdb {

namespace {

using debug::MessageLoop;

class BreakpointSink : public RemoteAPI {
 public:
  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      fit::callback<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) override {
    adds.push_back(request);

    MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable {
      cb(Err(), debug_ipc::AddOrChangeBreakpointReply());
    });
  }

  void RemoveBreakpoint(
      const debug_ipc::RemoveBreakpointRequest& request,
      fit::callback<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb) override {
    removes.push_back(request);

    MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb)]() mutable {
      cb(Err(), debug_ipc::RemoveBreakpointReply());
    });
  }

  // No-op.
  void Threads(const debug_ipc::ThreadsRequest& request,
               fit::callback<void(const Err&, debug_ipc::ThreadsReply)> cb) override {
    thread_request_made = true;
  }

  // No-op.
  void Resume(const debug_ipc::ResumeRequest& request,
              fit::callback<void(const Err&, debug_ipc::ResumeReply)> cb) override {}

  std::vector<debug_ipc::AddOrChangeBreakpointRequest> adds;
  std::vector<debug_ipc::RemoveBreakpointRequest> removes;

  bool thread_request_made = false;
};

class BreakpointImplTest : public RemoteAPITest {
 public:
  BreakpointImplTest() = default;
  ~BreakpointImplTest() override = default;

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

class BreakpointObserverSink : public BreakpointObserver {
 public:
  // The Session must outlive this object.
  explicit BreakpointObserverSink(Session* session) : session_(session) {
    session->AddBreakpointObserver(this);
  }
  ~BreakpointObserverSink() { session_->RemoveBreakpointObserver(this); }

  void OnBreakpointMatched(Breakpoint* breakpoint, bool user_requested) override {
    notification_count++;
    last_breakpoint = breakpoint;
    last_user_requested = user_requested;
  }

  int notification_count = 0;

  // Parameters of last notification.
  Breakpoint* last_breakpoint = nullptr;
  bool last_user_requested = false;

 private:
  Session* session_;
};

}  // namespace

TEST_F(BreakpointImplTest, DynamicLoading) {
  BreakpointObserverSink observer_sink(&session());

  BreakpointImpl bp(&session(), false);
  EXPECT_EQ(0, observer_sink.notification_count);

  ProcessSymbolsTestSetup setup;
  auto module_symbols1 = fxl::MakeRefCounted<MockModuleSymbols>("myfile1.so");
  auto module_symbols2 = fxl::MakeRefCounted<MockModuleSymbols>("myfile2.so");

  // The function to find the breakpoint for is in module1.
  const std::string kFunctionName = "DoThings";
  auto function_symbol = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function_symbol->set_assigned_name(kFunctionName);
  TestIndexedSymbol function_indexed(module_symbols1.get(), &module_symbols1->index().root(),
                                     kFunctionName, function_symbol);

  // Make a disabled symbolic breakpoint.
  BreakpointSettings in;
  in.enabled = false;
  in.locations.emplace_back(Identifier(IdentifierComponent(kFunctionName)));

  // Setting the disabled settings shouldn't update the backend.
  bp.SetSettings(in);
  EXPECT_EQ(0, observer_sink.notification_count);  // No calls because it matched no places.
  ASSERT_TRUE(sink().adds.empty());

  // Setting enabled settings with no processes should not update the backend.
  in.enabled = true;
  bp.SetSettings(in);
  EXPECT_EQ(0, observer_sink.notification_count);
  ASSERT_TRUE(sink().adds.empty());

  TargetImpl* target = session().system().GetTargetImpls()[0];

  // Create a process for it. Since the process doesn't resolve the symbol yet, no messages should
  // be sent.
  const uint64_t koid = 5678;
  target->CreateProcessForTesting(koid, "test");
  ASSERT_TRUE(sink().adds.empty());

  // Make two fake modules. The first will resolve the function to two locations, the second will
  // resolve nothing. They must be larger than the module base.
  const uint64_t kModule1Base = 0x1000000;
  const uint64_t kAddress1 = 0x78456345;
  const uint64_t kAddress2 = 0x12345678;
  module_symbols1->AddSymbolLocations(kFunctionName,
                                      {Location(Location::State::kSymbolized, kAddress1),
                                       Location(Location::State::kSymbolized, kAddress2)});

  // Cause the process to load the module. We have to keep the module_ref alive for this to stay
  // cached in the SystemSymbols.
  const std::string kBuildID1 = "abcd";
  const std::string kBuildID2 = "zyxw";
  session().system().GetSymbols()->InjectModuleForTesting(kBuildID1, module_symbols1.get());
  session().system().GetSymbols()->InjectModuleForTesting(kBuildID2, module_symbols2.get());

  // Before modules no thread request should be made.
  EXPECT_FALSE(sink().thread_request_made);

  // Cause the process to load module 1.
  std::vector<debug_ipc::Module> modules;
  debug_ipc::Module load1;
  load1.name = "test";
  load1.base = kModule1Base;
  load1.build_id = kBuildID1;
  modules.push_back(load1);
  target->process()->OnModules(modules);

  // After adding modules, the client should have asked for threads.
  EXPECT_TRUE(sink().thread_request_made);

  // That should have notified the breakpoint which should have added the two addresses to the
  // backend.
  ASSERT_FALSE(sink().adds.empty());
  debug_ipc::AddOrChangeBreakpointRequest out = sink().adds[0];
  EXPECT_FALSE(out.breakpoint.one_shot);
  EXPECT_EQ(debug_ipc::Stop::kAll, out.breakpoint.stop);

  // Both locations should be for the same process, with no thread restriction.
  ASSERT_EQ(2u, out.breakpoint.locations.size());
  EXPECT_EQ(koid, out.breakpoint.locations[0].id.process);
  EXPECT_EQ(koid, out.breakpoint.locations[1].id.process);
  EXPECT_EQ(0u, out.breakpoint.locations[0].id.thread);
  EXPECT_EQ(0u, out.breakpoint.locations[1].id.thread);

  // Addresses could be in either order. They should be absolute.
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
  target->process()->OnModules(modules);
  ASSERT_TRUE(sink().adds.empty());

  // Should have sent a notification. It should not be marked user-requested since this was a
  // result of loading a new process.
  EXPECT_EQ(1, observer_sink.notification_count);
  EXPECT_EQ(&bp, observer_sink.last_breakpoint);
  EXPECT_EQ(false, observer_sink.last_user_requested);

  // Disabling should send the delete message.
  ASSERT_TRUE(sink().removes.empty());  // Should have none so far.
  in.enabled = false;
  bp.SetSettings(in);
  ASSERT_TRUE(sink().adds.empty());
  ASSERT_EQ(1u, sink().removes.size());
  EXPECT_EQ(out.breakpoint.id, sink().removes[0].breakpoint_id);
}

// Tests that address breakpoints are enabled immediately even when no symbols are available.
TEST_F(BreakpointImplTest, Address) {
  // TargetImpl target(&session().system());
  auto target_impls = session().system().GetTargetImpls();
  ASSERT_EQ(1u, target_impls.size());
  TargetImpl* target = target_impls[0];
  const uint64_t kProcessKoid = 6789;
  target->CreateProcessForTesting(kProcessKoid, "test");

  BreakpointImpl bp(&session(), false);

  const uint64_t kAddress = 0x123456780;
  BreakpointSettings in;
  in.enabled = true;
  in.scope = ExecutionScope(target);
  in.locations.emplace_back(kAddress);

  bp.SetSettings(in);

  // Check the message was sent.
  ASSERT_EQ(1u, sink().adds.size());
  debug_ipc::AddOrChangeBreakpointRequest out = sink().adds[0];
  EXPECT_FALSE(out.breakpoint.one_shot);
  EXPECT_EQ(debug_ipc::Stop::kAll, out.breakpoint.stop);
  EXPECT_EQ(1u, out.breakpoint.locations.size());
}

TEST_F(BreakpointImplTest, Watchpoint) {
  auto target_impls = session().system().GetTargetImpls();
  ASSERT_EQ(1u, target_impls.size());
  TargetImpl* target = target_impls[0];
  const uint64_t kProcessKoid = 6789;
  target->CreateProcessForTesting(kProcessKoid, "test");

  BreakpointImpl bp(&session(), false);

  const uint64_t kAddress = 0x123456780;
  const uint32_t kSize = 4;
  BreakpointSettings in;
  in.enabled = true;
  in.type = debug_ipc::BreakpointType::kWrite;
  in.byte_size = kSize;
  in.scope = ExecutionScope(target);
  in.locations.emplace_back(kAddress);

  bp.SetSettings(in);

  // Check the message was sent.
  ASSERT_EQ(1u, sink().adds.size());
  debug_ipc::AddOrChangeBreakpointRequest& out = sink().adds[0];
  EXPECT_EQ(out.breakpoint.type, debug_ipc::BreakpointType::kWrite);
  EXPECT_FALSE(out.breakpoint.one_shot);
  EXPECT_EQ(debug_ipc::Stop::kAll, out.breakpoint.stop);

  ASSERT_EQ(1u, out.breakpoint.locations.size());
  EXPECT_EQ(out.breakpoint.locations[0].address, 0u);
  // For now, the debugger will send the same address as a range/begin.
  EXPECT_EQ(out.breakpoint.locations[0].address_range.begin(), kAddress);
  EXPECT_EQ(out.breakpoint.locations[0].address_range.end(), kAddress + kSize);
}

// Tests the SettingStore integration and error checking of sizes.
TEST_F(BreakpointImplTest, SetSize) {
  BreakpointImpl bp(&session(), false);

  SettingStore& setting_store = bp.settings();
  EXPECT_EQ(0, setting_store.GetInt(ClientSettings::Breakpoint::kSize));

  // Setting the size at this point should be invalid because the type isn't hardware.
  Err err = setting_store.SetInt(ClientSettings::Breakpoint::kSize, 1);
  EXPECT_EQ("Breakpoints of type 'software' don't have sizes associated with them.", err.msg());
  err = setting_store.SetInt(ClientSettings::Breakpoint::kSize, 0);
  EXPECT_TRUE(err.ok());

  // Se thte settings for a 4-byte write breakpoint.
  BreakpointSettings in;
  in.enabled = true;
  in.type = debug_ipc::BreakpointType::kWrite;
  in.byte_size = 4;
  in.locations.emplace_back(0x1234);
  bp.SetSettings(in);

  // The setting store should provide the new value.
  EXPECT_EQ(4, setting_store.GetInt(ClientSettings::Breakpoint::kSize));

  // Odd size.
  err = setting_store.SetInt(ClientSettings::Breakpoint::kSize, 3);
  EXPECT_FALSE(err.ok());

  // Large size.
  err = setting_store.SetInt(ClientSettings::Breakpoint::kSize, 200);
  EXPECT_FALSE(err.ok());

  // Good new size.
  err = setting_store.SetInt(ClientSettings::Breakpoint::kSize, 8);
  EXPECT_TRUE(err.ok());
  EXPECT_EQ(8, setting_store.GetInt(ClientSettings::Breakpoint::kSize));
}

}  // namespace zxdb
