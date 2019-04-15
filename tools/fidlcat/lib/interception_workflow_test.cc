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

  const zx_handle_t* handles() { return handles_; }

  size_t num_handles() { return sizeof(handles_) / sizeof(handles_[0]); }

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
    if (address == kHandlesAddress) {
      block.address = address;
      block.size = size;
      block.valid = true;
      const zx_handle_t* h = handles();
      std::copy(reinterpret_cast<const uint8_t*>(h),
                reinterpret_cast<const uint8_t*>(h + num_handles()),
                std::back_inserter(block.data));
    }
  }

  void PopulateRegisters(debug_ipc::RegisterCategory& category) {
    category.type = debug_ipc::RegisterCategory::Type::kGeneral;
    // Assumes Little Endian
    const uint8_t* address_as_bytes =
        reinterpret_cast<const uint8_t*>(&kBytesAddress);

    const uint8_t* handles_address_as_bytes =
        reinterpret_cast<const uint8_t*>(&kHandlesAddress);

    std::map<debug_ipc::RegisterID, std::vector<uint8_t>> values = {
        // zx_handle_t handle
        {debug_ipc::RegisterID::kX64_rdi, {0xb0, 0x1d, 0xfa, 0xce}},
        // uint32_t options
        {debug_ipc::RegisterID::kX64_rsi, {0x00, 0x00, 0x00, 0x00}},
        // bytes_address
        {debug_ipc::RegisterID::kX64_rdx,
         std::vector<uint8_t>(address_as_bytes,
                              address_as_bytes + num_bytes())},
        // num_bytes
        {debug_ipc::RegisterID::kX64_rcx,
         {static_cast<unsigned char>(num_bytes()), 0x00, 0x00, 0x00}},
        // handles_address
        {debug_ipc::RegisterID::kX64_r8,
         std::vector<uint8_t>(
             handles_address_as_bytes,
             handles_address_as_bytes + (sizeof(zx_handle_t) * num_handles()))},
        // num_handles
        {debug_ipc::RegisterID::kX64_r9,
         {static_cast<unsigned char>(num_handles()), 0x00, 0x00, 0x00}}};

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
  static constexpr uint64_t kHandlesAddress = 0xca11ab1e7e57;
  static const char* zx_channel_write_name_;

  fidl_message_header_t header_;
  zx_handle_t handles_[2] = {0x01234567, 0x89abcdef};
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

namespace {
template <typename T>
void AppendElements(std::string& result, size_t num, const T* a, const T* b) {
  std::ostringstream os;
  os << "actual      expected\n";
  for (size_t i = 0; i < num; i++) {
    os << std::left << std::setw(11) << a[i];
    os << " ";
    os << std::left << std::setw(11) << b[i];
    os << std::endl;
  }
  result.append(os.str());
}

struct AlwaysQuit {
  ~AlwaysQuit() { debug_ipc::MessageLoop::Current()->QuitNow(); }
};

}  // namespace

TEST_F(InterceptionWorkflowTest, ZxChannelWrite) {
  zxdb::Session& ses = session();
  debug_ipc::PlatformMessageLoop& lp = loop();
  InterceptionWorkflow workflow(&ses, &lp);

  zxdb::Err err;
  std::vector<std::string> blank;
  workflow.Initialize(blank);

  bool hit_breakpoint = false;
  // This will be executed when the zx_channel_write breakpoint is triggered.
  workflow.SetZxChannelWriteCallback([this, &hit_breakpoint](
                                         const zxdb::Err& err,
                                         const ZxChannelWriteParams& params) {
    AlwaysQuit aq;
    hit_breakpoint = true;
    ASSERT_TRUE(err.ok());

    std::string result;

    const uint8_t* data = data_.data();
    uint32_t num_bytes = params.GetNumBytes();
    ASSERT_EQ(num_bytes, data_.num_bytes());
    if (memcmp(params.GetBytes().get(), data, num_bytes) != 0) {
      result.append("bytes not equivalent");
      AppendElements<uint8_t>(result, num_bytes, params.GetBytes().get(), data);
      ASSERT_TRUE(false) << result;
    }

    const zx_handle_t* handles = data_.handles();
    uint32_t num_handles = params.GetNumHandles();
    ASSERT_EQ(num_handles, data_.num_handles());
    if (memcmp(params.GetHandles().get(), handles,
               num_handles * sizeof(zx_handle_t)) != 0) {
      result.append("handles not equivalent\n");
      AppendElements<zx_handle_t>(result, num_handles,
                                  params.GetHandles().get(), handles);
      ASSERT_TRUE(false) << result;
    }
  });

  // Create a fake process and thread.
  constexpr uint64_t kProcessKoid = 1234;
  zxdb::Process* the_process = InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  zxdb::Thread* the_thread = InjectThread(kProcessKoid, kThreadKoid);

  // Observe thread.  This is usually done in workflow::Attach, but
  // RemoteAPITest has its own ideas about attaching, so that method only
  // half-works (the half that registers the target with the workflow). We have
  // to register the observer manually.
  internal::InterceptingTargetObserver target_observer(&workflow);
  zxdb::Target* target = session().system().GetTargets()[0];
  target->AddObserver(&target_observer);
  target_observer.DidCreateProcess(target, the_process, false);
  target_observer.process_observer()->DidCreateThread(the_process, the_thread);

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

  // Trigger breakpoint.
  debug_ipc::NotifyException notification;
  notification.process_koid = kProcessKoid;
  notification.type = debug_ipc::NotifyException::Type::kGeneral;
  notification.thread.koid = kThreadKoid;
  notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  mock_remote_api().PopulateBreakpointIds(notification);
  InjectException(notification);

  debug_ipc::MessageLoop::Current()->Run();

  // At this point, the ZxChannelWrite callback should have been executed.
  ASSERT_TRUE(hit_breakpoint);
  the_process->RemoveObserver(target_observer.process_observer());
  target->RemoveObserver(&target_observer);
}

}  // namespace fidlcat
