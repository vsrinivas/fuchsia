// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "interception_workflow.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>
#undef __TA_REQUIRES
#include <zircon/fidl.h>

#include "gtest/gtest.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"

namespace fidlcat {

class ProcessController;
class SyscallDecoderDispatcherTest;

constexpr uint32_t kHandle = 0xcefa1db0;

class SystemCallTest {
 public:
  static std::unique_ptr<SystemCallTest> ZxChannelWrite(
      int64_t result, std::string_view result_name, zx_handle_t handle,
      uint32_t options, const uint8_t* bytes, uint32_t num_bytes,
      const zx_handle_t* handles, uint32_t num_handles) {
    auto value = std::make_unique<SystemCallTest>("zx_channel_write", result,
                                                  result_name);
    value->inputs_.push_back(handle);
    value->inputs_.push_back(options);
    value->inputs_.push_back(reinterpret_cast<uint64_t>(bytes));
    value->inputs_.push_back(num_bytes);
    value->inputs_.push_back(reinterpret_cast<uint64_t>(handles));
    value->inputs_.push_back(num_handles);
    return value;
  }

  static std::unique_ptr<SystemCallTest> ZxChannelRead(
      int64_t result, std::string_view result_name, zx_handle_t handle,
      uint32_t options, const uint8_t* bytes, const zx_handle_t* handles,
      uint32_t num_bytes, uint32_t num_handles, uint32_t* actual_bytes,
      uint32_t* actual_handles) {
    auto value = std::make_unique<SystemCallTest>("zx_channel_read", result,
                                                  result_name);
    value->inputs_.push_back(handle);
    value->inputs_.push_back(options);
    value->inputs_.push_back(reinterpret_cast<uint64_t>(bytes));
    value->inputs_.push_back(reinterpret_cast<uint64_t>(handles));
    value->inputs_.push_back(num_bytes);
    value->inputs_.push_back(num_handles);
    value->inputs_.push_back(reinterpret_cast<uint64_t>(actual_bytes));
    value->inputs_.push_back(reinterpret_cast<uint64_t>(actual_handles));
    return value;
  }

  SystemCallTest(const char* name, int64_t result, std::string_view result_name)
      : name_(name), result_(result), result_name_(result_name) {}

  const std::string& name() const { return name_; }
  int64_t result() const { return result_; }
  const std::string& result_name() const { return result_name_; }
  const std::vector<uint64_t>& inputs() const { return inputs_; }

 private:
  const std::string name_;
  const int64_t result_;
  const std::string result_name_;
  std::vector<uint64_t> inputs_;
};

static debug_ipc::RegisterID aarch64_regs[] = {
    debug_ipc::RegisterID::kARMv8_x0, debug_ipc::RegisterID::kARMv8_x1,
    debug_ipc::RegisterID::kARMv8_x2, debug_ipc::RegisterID::kARMv8_x3,
    debug_ipc::RegisterID::kARMv8_x4, debug_ipc::RegisterID::kARMv8_x5,
    debug_ipc::RegisterID::kARMv8_x6, debug_ipc::RegisterID::kARMv8_x7};
constexpr size_t aarch64_regs_count =
    sizeof(aarch64_regs) / sizeof(debug_ipc::RegisterID);

static debug_ipc::RegisterID amd64_regs[] = {
    debug_ipc::RegisterID::kX64_rdi, debug_ipc::RegisterID::kX64_rsi,
    debug_ipc::RegisterID::kX64_rdx, debug_ipc::RegisterID::kX64_rcx,
    debug_ipc::RegisterID::kX64_r8,  debug_ipc::RegisterID::kX64_r9};
constexpr size_t amd64_regs_count =
    sizeof(amd64_regs) / sizeof(debug_ipc::RegisterID);

// Data for syscall tests.
class DataForSyscallTest {
 public:
  DataForSyscallTest(debug_ipc::Arch arch) : arch_(arch) {
    if (arch_ == debug_ipc::Arch::kArm64) {
      param_regs_ = aarch64_regs;
      param_regs_count_ = aarch64_regs_count;
    } else {
      param_regs_ = amd64_regs;
      param_regs_count_ = amd64_regs_count;
    }
    header_.txid = kTxId;
    header_.reserved0 = kReserved;
    header_.ordinal = kOrdinal;

    sp_ = stack_ + kMaxStackSizeInWords;
  }

  const SystemCallTest* syscall() const { return syscall_.get(); }

  void set_syscall(std::unique_ptr<SystemCallTest> syscall) {
    syscall_ = std::move(syscall);
    size_t argument_count = syscall_->inputs().size();
    if (argument_count > param_regs_count_) {
      argument_count -= param_regs_count_;
      for (auto input = syscall_->inputs().crbegin();
           (input != syscall_->inputs().crend()) && (argument_count > 0);
           ++input, --argument_count) {
        *(--sp_) = *input;
      }
    }
    if (arch_ == debug_ipc::Arch::kX64) {
      *(--sp_) = kReturnAddress;
    }
  }

  uint64_t* sp() const { return sp_; }

  void set_check_bytes() { check_bytes_ = true; }
  void set_check_handles() { check_handles_ = true; }

  const uint8_t* bytes() const {
    return reinterpret_cast<const uint8_t*>(&header_);
  }

  size_t num_bytes() const { return sizeof(header_); }

  const zx_handle_t* handles() const { return handles_; }

  size_t num_handles() const { return sizeof(handles_) / sizeof(handles_[0]); }

  fxl::RefPtr<zxdb::SystemSymbols::ModuleRef> GetModuleRef(
      zxdb::Session* session) {
    // Create a module with zx_channel_write and zx_channel_read
    std::unique_ptr<zxdb::MockModuleSymbols> module =
        std::make_unique<zxdb::MockModuleSymbols>("zx.so");
    module->AddSymbolLocations(
        syscall_->name() + "@plt",
        {zxdb::Location(zxdb::Location::State::kSymbolized,
                        kSyscallSymbolAddress)});

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
    block.address = address;
    block.size = size;
    block.valid = true;
    std::copy(reinterpret_cast<uint8_t*>(address),
              reinterpret_cast<uint8_t*>(address + size),
              std::back_inserter(block.data));
    FXL_DCHECK(size == block.data.size())
        << "expected size: " << size
        << " and actual size: " << block.data.size();
  }

  void PopulateRegister(debug_ipc::RegisterID register_id, uint64_t value,
                        std::vector<debug_ipc::Register>* registers) {
    debug_ipc::Register& reg = registers->emplace_back();
    reg.id = register_id;
    for (int i = 0; i < 64; i += 8) {
      reg.data.push_back((value >> i) & 0xff);
    }
  }

  void PopulateRegisters(std::vector<debug_ipc::Register>* registers) {
    if (first_register_read_) {
      size_t count = std::min(param_regs_count_, syscall_->inputs().size());
      for (size_t i = 0; i < count; ++i) {
        PopulateRegister(param_regs_[i], syscall_->inputs()[i], registers);
      }
    } else {
      if (arch_ == debug_ipc::Arch::kArm64) {
        PopulateRegister(debug_ipc::RegisterID::kARMv8_x0, syscall_->result(),
                         registers);
      } else {
        PopulateRegister(debug_ipc::RegisterID::kX64_rax, syscall_->result(),
                         registers);
      }
    }

    if (arch_ == debug_ipc::Arch::kArm64) {
      // stack pointer
      PopulateRegister(debug_ipc::RegisterID::kARMv8_sp,
                       reinterpret_cast<uint64_t>(sp_), registers);
      // link register
      PopulateRegister(debug_ipc::RegisterID::kARMv8_lr, kReturnAddress,
                       registers);
    } else if (arch_ == debug_ipc::Arch::kX64) {
      // stack pointer
      PopulateRegister(debug_ipc::RegisterID::kX64_rsp,
                       reinterpret_cast<uint64_t>(sp_), registers);
    }
  }

  void PopulateRegisters(debug_ipc::RegisterCategory& category) {
    category.type = debug_ipc::RegisterCategory::Type::kGeneral;
    PopulateRegisters(&category.registers);
  }

  void Step() {
    sp_ = stack_ + kMaxStackSizeInWords;
    first_register_read_ = false;
  }

  template <typename T>
  void AppendElements(std::string& result, size_t num, const T* a, const T* b) {
    std::ostringstream os;
    os << "actual      expected\n";
    for (size_t i = 0; i < num; i++) {
      os << std::left << std::setw(11) << static_cast<uint32_t>(a[i]);
      os << " ";
      os << std::left << std::setw(11) << static_cast<uint32_t>(b[i]);
      os << std::endl;
    }
    result.append(os.str());
  }

  static constexpr uint64_t kSyscallSymbolAddress = 0x100060;
  static constexpr uint64_t kReturnAddress = 0x123456798;
  static constexpr uint64_t kMaxStackSizeInWords = 0x100;
  static constexpr zx_txid_t kTxId = 0xaaaaaaaa;
  static constexpr uint32_t kReserved = 0x0;
  static constexpr uint64_t kOrdinal = 8639255294892834816lu;
  static constexpr char kElfSymbolBuildID[] = "123412341234";

 private:
  debug_ipc::RegisterID* param_regs_;
  size_t param_regs_count_;
  std::unique_ptr<SystemCallTest> syscall_;
  uint64_t stack_[kMaxStackSizeInWords];
  uint64_t* sp_;
  bool check_bytes_ = false;
  bool check_handles_ = false;
  fidl_message_header_t header_;
  zx_handle_t handles_[2] = {0x01234567, 0x89abcdef};
  debug_ipc::Arch arch_;
  bool first_register_read_ = true;
};

// Provides the infrastructure needed to provide the data above.
class InterceptionRemoteAPI : public zxdb::MockRemoteAPI {
 public:
  explicit InterceptionRemoteAPI(DataForSyscallTest& data) : data_(data) {}

  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      std::function<void(const zxdb::Err&,
                         debug_ipc::AddOrChangeBreakpointReply)>
          cb) override {
    breakpoints_[request.breakpoint.id] = request.breakpoint;
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

  void Resume(const debug_ipc::ResumeRequest& request,
              std::function<void(const zxdb::Err&, debug_ipc::ResumeReply)> cb)
      override {
    debug_ipc::ResumeReply reply;
    data_.Step();
    debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb, reply]() {
      cb(zxdb::Err(), reply);
      // This is so that the test can inject the next exception.
      debug_ipc::MessageLoop::Current()->QuitNow();
    });
  }

  void PopulateBreakpointIds(uint64_t address,
                             debug_ipc::NotifyException& notification) {
    for (auto& breakpoint : breakpoints_) {
      if (address == breakpoint.second.locations[0].address) {
        notification.hit_breakpoints.emplace_back();
        notification.hit_breakpoints.back().id = breakpoint.first;
      }
    }
  }

 private:
  std::map<uint32_t, debug_ipc::BreakpointSettings> breakpoints_;
  DataForSyscallTest& data_;
};

class InterceptionWorkflowTest : public zxdb::RemoteAPITest {
 public:
  explicit InterceptionWorkflowTest(debug_ipc::Arch arch) : data_(arch) {
    display_options_.pretty_print = true;
    display_options_.columns = 132;
    display_options_.needs_colors = true;
  }
  ~InterceptionWorkflowTest() override = default;

  InterceptionRemoteAPI& mock_remote_api() { return *mock_remote_api_; }

  std::unique_ptr<zxdb::RemoteAPI> GetRemoteAPIImpl() override {
    auto remote_api = std::make_unique<InterceptionRemoteAPI>(data_);
    mock_remote_api_ = remote_api.get();
    return std::move(remote_api);
  }

  DataForSyscallTest& data() { return data_; }

  void PerformCheckTest(std::unique_ptr<SystemCallTest> syscall);

  void PerformDisplayTest(std::unique_ptr<SystemCallTest> syscall,
                          const char* expected);

  void PerformTest(std::unique_ptr<SystemCallTest> syscall,
                   ProcessController* controller,
                   std::unique_ptr<SyscallDecoderDispatcher> dispatcher);

 private:
  DataForSyscallTest data_;
  InterceptionRemoteAPI* mock_remote_api_;  // Owned by the session.
  DisplayOptions display_options_;
  std::stringstream result_;
};

class InterceptionWorkflowTestX64 : public InterceptionWorkflowTest {
 public:
  InterceptionWorkflowTestX64() : InterceptionWorkflowTest(GetArch()) {}
  ~InterceptionWorkflowTestX64() override = default;

  virtual debug_ipc::Arch GetArch() const override {
    return debug_ipc::Arch::kX64;
  }
};

class InterceptionWorkflowTestArm : public InterceptionWorkflowTest {
 public:
  InterceptionWorkflowTestArm() : InterceptionWorkflowTest(GetArch()) {}
  ~InterceptionWorkflowTestArm() override = default;

  virtual debug_ipc::Arch GetArch() const override {
    return debug_ipc::Arch::kArm64;
  }
};

// This does process setup for the test.  It creates a fake process, injects
// modules with the appropriate symbols, attaches to the process, etc.
class ProcessController {
 public:
  ProcessController(InterceptionWorkflowTest* remote_api,
                    zxdb::Session& session,
                    debug_ipc::PlatformMessageLoop& loop);
  ~ProcessController();

  InterceptionWorkflowTest* remote_api() const { return remote_api_; }
  InterceptionWorkflow& workflow() { return workflow_; }

  void Initialize(zxdb::Session& session,
                  std::unique_ptr<SyscallDecoderDispatcher> dispatcher);
  void Detach();

  static constexpr uint64_t kProcessKoid = 1234;
  static constexpr uint64_t kThreadKoid = 5678;

 private:
  InterceptionWorkflowTest* remote_api_;
  InterceptionWorkflow workflow_;

  zxdb::Process* process_ = nullptr;
  zxdb::Target* target_ = nullptr;
};

class AlwaysQuit {
 public:
  AlwaysQuit(ProcessController* controller) : controller_(controller) {}
  ~AlwaysQuit() { controller_->Detach(); }

 private:
  ProcessController* controller_;
};

template <typename T>
void AppendElements(std::string& result, const T* a, const T* b, size_t num) {
  std::ostringstream os;
  os << "actual      expected\n";
  for (size_t i = 0; i < num; i++) {
    os << std::left << std::setw(11) << static_cast<uint32_t>(a[i]);
    os << " ";
    os << std::left << std::setw(11) << static_cast<uint32_t>(b[i]);
    os << std::endl;
  }
  result.append(os.str());
}

class SyscallCheck : public SyscallUse {
 public:
  explicit SyscallCheck(ProcessController* controller)
      : controller_(controller) {}

  void SyscallDecoded(SyscallDecoder* syscall) override {
    DataForSyscallTest& data = controller_->remote_api()->data();
    FXL_DCHECK(syscall->Value(0) == kHandle);  // handle
    FXL_DCHECK(syscall->Value(1) == 0);        // options
    FXL_DCHECK(syscall->Loaded(2, data.num_bytes()));
    uint8_t* bytes = syscall->Content(2);
    if (memcmp(bytes, data.bytes(), data.num_bytes()) != 0) {
      std::string result = "bytes not equivalent\n";
      AppendElements(result, bytes, data.bytes(), data.num_bytes());
      syscall->Destroy();
      FAIL() << result;
    }
    FXL_DCHECK(syscall->Value(3) == data.num_bytes());  // num_bytes
    FXL_DCHECK(syscall->Loaded(4, data.num_handles() * sizeof(zx_handle_t)));
    zx_handle_t* handles = reinterpret_cast<zx_handle_t*>(syscall->Content(4));
    if (memcmp(handles, data.handles(), data.num_handles()) != 0) {
      std::string result = "handles not equivalent";
      AppendElements(result, handles, data.handles(), data.num_handles());
      syscall->Destroy();
      FAIL() << result;
    }
    FXL_DCHECK(syscall->Value(5) == data.num_handles());  // num_handles
    syscall->Destroy();
  }

  void SyscallDecodingError(const SyscallDecoderError& error,
                            SyscallDecoder* syscall) override {
    SyscallUse::SyscallDecodingError(error, syscall);
    FAIL();
  }

 private:
  ProcessController* controller_;
};

class SyscallDecoderDispatcherTest : public SyscallDecoderDispatcher {
 public:
  SyscallDecoderDispatcherTest(ProcessController* controller)
      : controller_(controller) {}

  std::unique_ptr<SyscallDecoder> CreateDecoder(
      InterceptingThreadObserver* thread_observer, zxdb::Thread* thread,
      uint64_t thread_id, const Syscall* syscall) override {
    return std::make_unique<SyscallDecoder>(
        this, thread_observer, thread, thread_id, syscall,
        std::make_unique<SyscallCheck>(controller_));
  }

  void DeleteDecoder(SyscallDecoder* decoder) override {
    SyscallDecoderDispatcher::DeleteDecoder(decoder);
    AlwaysQuit aq(controller_);
  }

 private:
  ProcessController* controller_;
};

class SyscallDisplayDispatcherTest : public SyscallDisplayDispatcher {
 public:
  SyscallDisplayDispatcherTest(LibraryLoader* loader,
                               const DisplayOptions& display_options,
                               std::ostream& os, ProcessController* controller)
      : SyscallDisplayDispatcher(loader, display_options, os),
        controller_(controller) {}

  ProcessController* controller() const { return controller_; }

  void DeleteDecoder(SyscallDecoder* decoder) override {
    SyscallDisplayDispatcher::DeleteDecoder(decoder);
    AlwaysQuit aq(controller_);
  }

 private:
  ProcessController* controller_;
};

ProcessController::ProcessController(InterceptionWorkflowTest* remote_api,
                                     zxdb::Session& session,
                                     debug_ipc::PlatformMessageLoop& loop)
    : remote_api_(remote_api), workflow_(&session, &loop) {}

void ProcessController::Initialize(
    zxdb::Session& session,
    std::unique_ptr<SyscallDecoderDispatcher> dispatcher) {
  std::vector<std::string> blank;
  workflow_.Initialize(blank, std::move(dispatcher));

  // Create a fake process and thread.
  process_ = remote_api_->InjectProcess(kProcessKoid);
  zxdb::Thread* the_thread =
      remote_api_->InjectThread(kProcessKoid, kThreadKoid);

  // Observe thread.  This is usually done in workflow_::Attach, but
  // RemoteAPITest has its own ideas about attaching, so that method only
  // half-works (the half that registers the target with the workflow). We have
  // to register the observer manually.
  target_ = session.system().GetTargets()[0];
  workflow_.AddObserver(target_);
  workflow_.observer_.DidCreateProcess(target_, process_, false);
  workflow_.observer_.process_observer().DidCreateThread(process_, the_thread);

  // Attach to process.
  zxdb::Err err;
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [this]() {
    workflow_.Attach(kProcessKoid, [](const zxdb::Err& err) {
      // Because we are already attached, we don't get here.
      FAIL() << "Should not be reached";
    });
    debug_ipc::MessageLoop::Current()->QuitNow();
  });
  debug_ipc::MessageLoop::Current()->Run();

  // Load modules into program (including the one with the zx_channel_write
  // and zx_channel_read symbols)
  fxl::RefPtr<zxdb::SystemSymbols::ModuleRef> module_ref =
      remote_api_->data().GetModuleRef(&session);

  for (zxdb::Target* target : session.system().GetTargets()) {
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
}

ProcessController::~ProcessController() {
  process_->RemoveObserver(&workflow_.observer_.process_observer());
  target_->RemoveObserver(&workflow_.observer_);
}

void ProcessController::Detach() { workflow_.Detach(); }

void InterceptionWorkflowTest::PerformCheckTest(
    std::unique_ptr<SystemCallTest> syscall) {
  ProcessController controller(this, session(), loop());

  PerformTest(std::move(syscall), &controller,
              std::make_unique<SyscallDecoderDispatcherTest>(&controller));
}

void InterceptionWorkflowTest::PerformDisplayTest(
    std::unique_ptr<SystemCallTest> syscall, const char* expected) {
  ProcessController controller(this, session(), loop());

  PerformTest(std::move(syscall), &controller,
              std::make_unique<SyscallDisplayDispatcherTest>(
                  nullptr, display_options_, result_, &controller));

  // Check that the syscall generated the data we expect.
  ASSERT_EQ(result_.str(), expected);
}

void InterceptionWorkflowTest::PerformTest(
    std::unique_ptr<SystemCallTest> syscall, ProcessController* controller,
    std::unique_ptr<SyscallDecoderDispatcher> dispatcher) {
  data_.set_syscall(std::move(syscall));
  controller->Initialize(session(), std::move(dispatcher));

  {
    // Trigger breakpoint.
    debug_ipc::NotifyException notification;
    notification.type = debug_ipc::NotifyException::Type::kGeneral;
    notification.thread.process_koid = ProcessController::kProcessKoid;
    notification.thread.thread_koid = ProcessController::kThreadKoid;
    notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
    notification.thread.stack_amount =
        debug_ipc::ThreadRecord::StackAmount::kMinimal;
    debug_ipc::StackFrame frame(DataForSyscallTest::kSyscallSymbolAddress,
                                reinterpret_cast<uint64_t>(data_.sp()));
    data_.PopulateRegisters(&frame.regs);
    notification.thread.frames.push_back(frame);
    mock_remote_api().PopulateBreakpointIds(
        DataForSyscallTest::kSyscallSymbolAddress, notification);
    InjectException(notification);
  }

  debug_ipc::MessageLoop::Current()->Run();

  {
    // Trigger next breakpoint, when the syscall has completed.
    debug_ipc::NotifyException notification;
    notification.type = debug_ipc::NotifyException::Type::kGeneral;
    notification.thread.process_koid = ProcessController::kProcessKoid;
    notification.thread.thread_koid = ProcessController::kThreadKoid;
    notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
    notification.thread.stack_amount =
        debug_ipc::ThreadRecord::StackAmount::kMinimal;
    debug_ipc::StackFrame frame(DataForSyscallTest::kReturnAddress,
                                reinterpret_cast<uint64_t>(data_.sp()));
    data_.PopulateRegisters(&frame.regs);
    notification.thread.frames.push_back(frame);
    InjectException(notification);
  }

  debug_ipc::MessageLoop::Current()->Run();

  // Making sure shutdown works.
  debug_ipc::MessageLoop::Current()->Run();
}

#define WRITE_TEST_CONTENT(errno)                                    \
  data().set_check_bytes();                                          \
  data().set_check_handles();                                        \
  PerformCheckTest(SystemCallTest::ZxChannelWrite(                   \
      errno, #errno, kHandle, 0, data().bytes(), data().num_bytes(), \
      data().handles(), data().num_handles()))

#define WRITE_TEST(name, errno)                                            \
  TEST_F(InterceptionWorkflowTestX64, name) { WRITE_TEST_CONTENT(errno); } \
  TEST_F(InterceptionWorkflowTestArm, name) { WRITE_TEST_CONTENT(errno); }

WRITE_TEST(ZxChannelWriteCheck, ZX_OK);

#define WRITE_DISPLAY_TEST_CONTENT(errno, expected)                           \
  data().set_check_bytes();                                                   \
  data().set_check_handles();                                                 \
  PerformDisplayTest(                                                         \
      SystemCallTest::ZxChannelWrite(errno, #errno, kHandle, 0,               \
                                     data().bytes(), data().num_bytes(),      \
                                     data().handles(), data().num_handles()), \
      expected)

#define WRITE_DISPLAY_TEST(name, errno, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {     \
    WRITE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }                                               \
  TEST_F(InterceptionWorkflowTestArm, name) {     \
    WRITE_DISPLAY_TEST_CONTENT(errno, expected);  \
  }

WRITE_DISPLAY_TEST(
    ZxChannelWrite, ZX_OK,
    "test \x1B[31m1234\x1B[0m:\x1B[31m5678\x1B[0m zx_channel_write("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31m3472498096\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  \x1B[31mCan't decode message\x1B[0m num_bytes=16 num_handles=2 "
    "ordinal=8639255294892834816\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "\n");

WRITE_DISPLAY_TEST(
    ZxChannelWritePeerClosed, ZX_ERR_PEER_CLOSED,
    "test \x1B[31m1234\x1B[0m:\x1B[31m5678\x1B[0m zx_channel_write("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31m3472498096\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m)\n"
    "  \x1B[31mCan't decode message\x1B[0m num_bytes=16 num_handles=2 "
    "ordinal=8639255294892834816\n"
    "  -> \x1B[31mZX_ERR_PEER_CLOSED\x1B[0m\n"
    "\n");

#define READ_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected) \
  if (check_bytes)                                                             \
    data().set_check_bytes();                                                  \
  if (check_handles)                                                           \
    data().set_check_handles();                                                \
  uint32_t actual_bytes = data().num_bytes();                                  \
  uint32_t actual_handles = data().num_handles();                              \
  PerformDisplayTest(                                                          \
      SystemCallTest::ZxChannelRead(                                           \
          errno, #errno, kHandle, 0, data().bytes(), data().handles(), 100,    \
          64, check_bytes ? &actual_bytes : nullptr,                           \
          check_handles ? &actual_handles : nullptr),                          \
      expected);

#define READ_DISPLAY_TEST(name, errno, check_bytes, check_handles, expected) \
  TEST_F(InterceptionWorkflowTestX64, name) {                                \
    READ_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected);  \
  }                                                                          \
  TEST_F(InterceptionWorkflowTestArm, name) {                                \
    READ_DISPLAY_TEST_CONTENT(errno, check_bytes, check_handles, expected);  \
  }

READ_DISPLAY_TEST(
    ZxChannelRead, ZX_OK, true, true,
    "test \x1B[31m1234\x1B[0m:\x1B[31m5678\x1B[0m zx_channel_read("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31m3472498096\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
    "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
    "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "  \x1B[31mCan't decode message\x1B[0m num_bytes=16 num_handles=2 "
    "ordinal=8639255294892834816\n"
    "\n");

READ_DISPLAY_TEST(
    ZxChannelReadShouldWait, ZX_ERR_SHOULD_WAIT, true, true,
    "test \x1B[31m1234\x1B[0m:\x1B[31m5678\x1B[0m zx_channel_read("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31m3472498096\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
    "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
    "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
    "  -> \x1B[31mZX_ERR_SHOULD_WAIT\x1B[0m\n"
    "\n");

READ_DISPLAY_TEST(
    ZxChannelReadTooSmall, ZX_ERR_BUFFER_TOO_SMALL, true, true,
    "test \x1B[31m1234\x1B[0m:\x1B[31m5678\x1B[0m zx_channel_read("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31m3472498096\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
    "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
    "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
    "  -> \x1B[31mZX_ERR_BUFFER_TOO_SMALL\x1B[0m ("
    "actual_bytes:\x1B[32muint32\x1B[0m: \x1B[34m16\x1B[0m, "
    "actual_handles:\x1B[32muint32\x1B[0m: \x1B[34m2\x1B[0m)\n"
    "\n");

READ_DISPLAY_TEST(
    ZxChannelReadNoBytes, ZX_OK, false, true,
    "test \x1B[31m1234\x1B[0m:\x1B[31m5678\x1B[0m zx_channel_read("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31m3472498096\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
    "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
    "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "  \x1B[31mCan't decode message\x1B[0m num_bytes=0 num_handles=2\n"
    "\n");

READ_DISPLAY_TEST(
    ZxChannelReadNoHandles, ZX_OK, true, false,
    "test \x1B[31m1234\x1B[0m:\x1B[31m5678\x1B[0m zx_channel_read("
    "handle:\x1B[32mhandle\x1B[0m: \x1B[31m3472498096\x1B[0m, "
    "options:\x1B[32muint32\x1B[0m: \x1B[34m0\x1B[0m, "
    "num_bytes:\x1B[32muint32\x1B[0m: \x1B[34m100\x1B[0m, "
    "num_handles:\x1B[32muint32\x1B[0m: \x1B[34m64\x1B[0m)\n"
    "  -> \x1B[32mZX_OK\x1B[0m\n"
    "  \x1B[31mCan't decode message\x1B[0m num_bytes=16 num_handles=0 "
    "ordinal=8639255294892834816\n"
    "\n");

}  // namespace fidlcat
