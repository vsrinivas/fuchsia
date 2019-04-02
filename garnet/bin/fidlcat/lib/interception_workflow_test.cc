// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "interception_workflow.h"

#include <thread>
#undef __TA_REQUIRES
#include <zircon/fidl.h>

#include "gtest/gtest.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"

namespace fidlcat {

// This class encapsulates the data needed for the zx_channel_write test.
// TODO: This is obviously not extensible to more than one test.
class DataForZxWriteTest {
 public:
  DataForZxWriteTest() {
    header_.txid = kTxId;
    header_.reserved0 = kReserved;
    header_.flags = kFlags;
    header_.ordinal = kOrdinal;
  }

  const uint8_t* data() { return reinterpret_cast<uint8_t*>(&header_); }

  size_t num_bytes() { return sizeof(header_); }

  fxl::RefPtr<zxdb::SystemSymbols::ModuleRef> GetModuleRef(
      zxdb::Session* session) {
    constexpr uint64_t kElfSymbolAddress = 0x100060;

    // Create a module with zx_channel_write
    std::unique_ptr<zxdb::MockModuleSymbols> module =
        std::make_unique<zxdb::MockModuleSymbols>("zx.so");
    module->AddSymbolLocations(
        zx_channel_write_name_,
        {zxdb::Location(zxdb::Location::State::kSymbolized,
                        kElfSymbolAddress)});

    return session->system().GetSymbols()->InjectModuleForTesting(
        kElfSymbolBuildID, std::move(module));
  }

  void PopulateModules(std::vector<debug_ipc::Module>& modules) {
    const uint64_t kModuleBase = 0x1000000;
    debug_ipc::Module load;
    load.name = "test";
    load.base = kModuleBase;
    load.build_id = kElfSymbolBuildID;
    modules.push_back(load);
  }

  void PopulateMemoryBlockForAddress(uint64_t address, uint64_t size,
                                     debug_ipc::MemoryBlock& block) {
    if (address == kBytesAddress) {
      block.address = address;
      block.size = size;
      block.valid = true;
      const uint8_t* bytes = data();
      std::copy(bytes, bytes + num_bytes(), std::back_inserter(block.data));
    }
  }

  void PopulateRegisters(debug_ipc::RegisterCategory& category) {
    category.type = debug_ipc::RegisterCategory::Type::kGeneral;
    // Assumes Little Endian
    const uint8_t* address_as_bytes =
        reinterpret_cast<const uint8_t*>(&kBytesAddress);
    uint8_t num_bytes = sizeof(fidl_message_header_t);

    std::map<debug_ipc::RegisterID, std::vector<uint8_t>> values = {
        {debug_ipc::RegisterID::kX64_rdi, {0xb0, 0x1d, 0xfa, 0xce}},
        {debug_ipc::RegisterID::kX64_rsi, {0x00, 0x00, 0x00, 0x00}},
        {debug_ipc::RegisterID::kX64_rdx,
         std::vector<uint8_t>(address_as_bytes, address_as_bytes + num_bytes)},
        {debug_ipc::RegisterID::kX64_rcx, {num_bytes, 0x00, 0x00, 0x00}},
        {debug_ipc::RegisterID::kX64_r8,
         {0x7e, 0x57, 0xab, 0x1e, 0x0f, 0xac, 0xad, 0xe5}},
        {debug_ipc::RegisterID::kX64_r9, {0x01, 0x00, 0x00, 0x00}}};

    std::vector<debug_ipc::Register>& registers = category.registers;
    for (auto value : values) {
      debug_ipc::Register& reg = registers.emplace_back();
      reg.id = value.first;
      reg.data = value.second;
    }
  }

 private:
  static const zx_txid_t kTxId = 0xaaaaaaaa;
  static const uint32_t kReserved = 0x0;
  static const uint32_t kFlags = 0x0;
  static const uint32_t kOrdinal = 2011483371;
  static constexpr char kElfSymbolBuildID[] = "123412341234";
  static constexpr uint64_t kBytesAddress = 0x7e57ab1eba5eba11;
  static const char* zx_channel_write_name_;

  fidl_message_header_t header_;
};

const char* DataForZxWriteTest::zx_channel_write_name_ =
    InterceptionWorkflow::kZxChannelWriteName;

// Provides the infrastructure needed to provide the data above.
class InterceptionRemoteAPI : public zxdb::MockRemoteAPI {
 public:
  explicit InterceptionRemoteAPI(DataForZxWriteTest& data) : data_(data) {}

  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const zxdb::Err&,
                         debug_ipc::AddOrChangeBreakpointReply)>
          cb) override {
    breakpoint_ids_.insert(request.breakpoint.breakpoint_id);
    MockRemoteAPI::AddOrChangeBreakpoint(request, cb);
  }

  void Attach(const debug_ipc::AttachRequest& request,
              std::function<void(const zxdb::Err&, debug_ipc::AttachReply)> cb)
      override {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb]() { cb(zxdb::Err(), debug_ipc::AttachReply()); });
  }

  void Modules(const debug_ipc::ModulesRequest& request,
               std::function<void(const zxdb::Err&, debug_ipc::ModulesReply)>
                   cb) override {
    debug_ipc::ModulesReply reply;
    data_.PopulateModules(reply.modules);
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb, reply]() { cb(zxdb::Err(), reply); });
  }

  void ReadMemory(
      const debug_ipc::ReadMemoryRequest& request,
      std::function<void(const zxdb::Err&, debug_ipc::ReadMemoryReply)> cb)
      override {
    debug_ipc::ReadMemoryReply reply;
    data_.PopulateMemoryBlockForAddress(request.address, request.size,
                                        reply.blocks.emplace_back());
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb, reply]() { cb(zxdb::Err(), reply); });
  }

  void ReadRegisters(
      const debug_ipc::ReadRegistersRequest& request,
      std::function<void(const zxdb::Err&, debug_ipc::ReadRegistersReply)> cb)
      override {
    // TODO: Parameterize this so we can have more than one test.
    debug_ipc::ReadRegistersReply reply;
    data_.PopulateRegisters(reply.categories.emplace_back());
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb, reply]() { cb(zxdb::Err(), reply); });
  }

  const std::set<uint32_t>& breakpoint_ids() const { return breakpoint_ids_; }

  void PopulateBreakpointIds(debug_ipc::NotifyException& notification) {
    for (uint32_t id : breakpoint_ids_) {
      notification.hit_breakpoints.emplace_back();
      notification.hit_breakpoints.back().breakpoint_id = id;
    }
  }

 private:
  std::set<uint32_t> breakpoint_ids_;
  DataForZxWriteTest& data_;
};

class InterceptionWorkflowTest : public zxdb::RemoteAPITest {
 public:
  InterceptionWorkflowTest() = default;
  ~InterceptionWorkflowTest() override = default;

  InterceptionRemoteAPI& mock_remote_api() { return *mock_remote_api_; }

  std::unique_ptr<zxdb::RemoteAPI> GetRemoteAPIImpl() override {
    auto remote_api = std::make_unique<InterceptionRemoteAPI>(data_);
    mock_remote_api_ = remote_api.get();
    return std::move(remote_api);
  }

 protected:
  DataForZxWriteTest data_;

 private:
  InterceptionRemoteAPI* mock_remote_api_;  // Owned by the session.
};

TEST_F(InterceptionWorkflowTest, ZxChannelWrite) {
  zxdb::Session& ses = session();
  debug_ipc::PlatformMessageLoop& lp = loop();
  InterceptionWorkflow workflow(&ses, &lp);

  zxdb::Err err;
  std::vector<std::string> blank;
  workflow.Initialize(blank);

  // This will be executed when the zx_channel_write breakpoint is triggered.
  workflow.SetZxChannelWriteCallback(
      [this](const zxdb::Err& err, const ZxChannelWriteParams& params) {
        ASSERT_TRUE(err.ok());
        const uint8_t* data = data_.data();
        std::string result;
        uint32_t num_bytes = params.GetNumBytes();
        ASSERT_EQ(num_bytes, data_.num_bytes());
        if (memcmp(params.GetBytes().get(), data, num_bytes) != 0) {
          for (size_t i = 0; i < num_bytes; i++) {
            result.append(std::to_string(params.GetBytes().get()[i]));
            result.append(" ");
            result.append(std::to_string(data[i]));
            result.append("\n");
          }
          result.append("bytes not equivalent");
          ASSERT_TRUE(false) << result;
        }
      });

  // Create a fake process and thread.
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  zxdb::Thread* thread = InjectThread(kProcessKoid, kThreadKoid);

  // Observe thread.  This is usually done in workflow::Attach, but
  // RemoteAPITest has its own ideas about attaching, so that method only
  // half-works (the half that registers the target with the workflow). We have
  // to register the observer manually.
  internal::InterceptingThreadObserver thread_observer(&workflow);
  thread->AddObserver(&thread_observer);

  // Attach to process.
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [&workflow]() {
    workflow.Attach(kProcessKoid, [](const zxdb::Err& err) {
      // Because we are already attached, we don't get here.
      ASSERT_TRUE(false) << "Should not be reached";
    });
    debug_ipc::MessageLoop::Current()->QuitNow();
  });
  debug_ipc::MessageLoop::Current()->Run();

  // Load modules into program (including the one with the zx_channel_write
  // symbol)
  fxl::RefPtr<zxdb::SystemSymbols::ModuleRef> module_ref =
      data_.GetModuleRef(&ses);

  for (zxdb::Target* target : ses.system().GetTargets()) {
    zxdb::Err err;
    std::vector<debug_ipc::Module> modules;
    // Force system to load modules.  Callback doesn't need to do anything
    // interesting.
    target->GetProcess()->GetModules(
        [](const zxdb::Err&, std::vector<debug_ipc::Module>) {
          debug_ipc::MessageLoop::Current()->QuitNow();
        });
    debug_ipc::MessageLoop::Current()->Run();
  }

  bool hit_breakpoint = false;
  // Set breakpoint on zx_channel_write.
  workflow.SetBreakpoints([&hit_breakpoint](const zxdb::Err& err) {
    hit_breakpoint = true;
    ASSERT_TRUE(err.ok()) << "Failure: " << err.msg();
    debug_ipc::MessageLoop::Current()->QuitNow();
  });
  debug_ipc::MessageLoop::Current()->Run();

  // Trigger breakpoint.
  debug_ipc::NotifyException notification;
  notification.process_koid = kProcessKoid;
  notification.type = debug_ipc::NotifyException::Type::kGeneral;
  notification.thread.koid = kThreadKoid;
  notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  mock_remote_api().PopulateBreakpointIds(notification);
  InjectException(notification);

  // At this point, the ZxChannelWrite callback should have been executed.
  ASSERT_TRUE(hit_breakpoint);
}

}  // namespace fidlcat
