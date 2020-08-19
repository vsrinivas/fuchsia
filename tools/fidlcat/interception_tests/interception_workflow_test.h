// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_INTERCEPTION_TESTS_INTERCEPTION_WORKFLOW_TEST_H_
#define TOOLS_FIDLCAT_INTERCEPTION_TESTS_INTERCEPTION_WORKFLOW_TEST_H_

#include <zircon/fidl.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/frame_impl.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/target_impl.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "tools/fidlcat/lib/interception_workflow.h"
#include "tools/fidlcat/lib/replay.h"

namespace fidlcat {

class ProcessController;
class SyscallDecoderDispatcherTest;

constexpr uint64_t kFirstPid = 3141;
constexpr uint64_t kSecondPid = 2718;

constexpr uint64_t kFirstThreadKoid = 8764;
constexpr uint64_t kSecondThreadKoid = 8765;

constexpr uint32_t kHandle = 0xcefa1db0;
constexpr uint64_t kHandleKoid = 1000828;
constexpr uint32_t kHandle2 = 0xcefa1222;
constexpr uint64_t kHandle2Koid = 1000829;
constexpr uint32_t kHandle3 = 0xcefa1333;
constexpr uint32_t kHandleOut = 0xbde90caf;
constexpr uint32_t kHandleOut2 = 0xbde90222;
constexpr uint32_t kPort = 0xdf0b2ec1;
constexpr uint64_t kKey = 1234;
constexpr uint64_t kKoid = 4252;
constexpr uint64_t kKoid2 = 5242;
constexpr zx_futex_t kFutex = 56789;
constexpr zx_futex_t kFutex2 = 98765;

extern SyscallDecoderDispatcher* global_dispatcher;

class SystemCallTest {
 public:
  SystemCallTest(const char* name, int64_t result, std::string_view result_name)
      : name_(name), result_(result), result_name_(result_name) {}

  const std::string& name() const { return name_; }
  int64_t result() const { return result_; }
  const std::string& result_name() const { return result_name_; }
  const std::vector<uint64_t>& inputs() const { return inputs_; }

  void AddInput(uint64_t input) { inputs_.push_back(input); }

 private:
  const std::string name_;
  const int64_t result_;
  const std::string result_name_;
  std::vector<uint64_t> inputs_;
};

// Data for syscall tests.
class DataForSyscallTest {
 public:
  DataForSyscallTest(debug_ipc::Arch arch);

  const SystemCallTest* syscall() const { return syscall_.get(); }

  void set_syscall(std::unique_ptr<SystemCallTest> syscall) { syscall_ = std::move(syscall); }

  bool use_alternate_data() const { return use_alternate_data_; }

  void set_use_alternate_data() { use_alternate_data_ = true; }

  void load_syscall_data() {
    size_t argument_count = syscall_->inputs().size();
    if (argument_count > param_regs_->size()) {
      argument_count -= param_regs_->size();
      for (auto input = syscall_->inputs().crbegin();
           (input != syscall_->inputs().crend()) && (argument_count > 0);
           ++input, --argument_count) {
        *(--sp_) = *input;
      }
    }
    if (arch_ == debug_ipc::Arch::kX64) {
      *(--sp_) = kReturnAddress;
    }
    stepped_processes_.clear();
  }

  uint64_t* sp() const { return sp_; }

  void set_check_bytes() { check_bytes_ = true; }
  void set_check_handles() { check_handles_ = true; }

  uint8_t* bytes() { return reinterpret_cast<uint8_t*>(&header_); }

  size_t num_bytes() const { return sizeof(header_); }

  uint8_t* large_bytes() { return large_bytes_.data(); }

  size_t num_large_bytes() const { return large_bytes_.size(); }

  zx_handle_t* handles() { return handles_; }

  size_t num_handles() const { return sizeof(handles_) / sizeof(handles_[0]); }

  zx_handle_info_t* handle_infos() { return handle_infos_; }

  size_t num_handle_infos() const { return sizeof(handle_infos_) / sizeof(handle_infos_[0]); }

  uint8_t* bytes2() { return reinterpret_cast<uint8_t*>(&header2_); }

  size_t num_bytes2() const { return sizeof(header2_); }

  zx_handle_t* handles2() { return handles2_; }

  size_t num_handles2() const { return sizeof(handles2_) / sizeof(handles2_[0]); }

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
    std::copy(reinterpret_cast<uint8_t*>(address), reinterpret_cast<uint8_t*>(address + size),
              std::back_inserter(block.data));
    FX_DCHECK(size == block.data.size())
        << "expected size: " << size << " and actual size: " << block.data.size();
  }

  void PopulateRegister(debug_ipc::RegisterID register_id, uint64_t value,
                        std::vector<debug_ipc::Register>* registers) {
    debug_ipc::Register& reg = registers->emplace_back();
    reg.id = register_id;
    for (int i = 0; i < 64; i += 8) {
      reg.data.push_back((value >> i) & 0xff);
    }
  }

  void PopulateRegisters(uint64_t process_koid, std::vector<debug_ipc::Register>* registers) {
    if (syscall_ != nullptr) {
      if (stepped_processes_.find(process_koid) == stepped_processes_.end()) {
        size_t count = std::min(param_regs_->size(), syscall_->inputs().size());
        for (size_t i = 0; i < count; ++i) {
          PopulateRegister((*param_regs_)[i], syscall_->inputs()[i], registers);
        }
      } else {
        if (arch_ == debug_ipc::Arch::kArm64) {
          PopulateRegister(debug_ipc::RegisterID::kARMv8_x0, syscall_->result(), registers);
        } else {
          PopulateRegister(debug_ipc::RegisterID::kX64_rax, syscall_->result(), registers);
        }
      }
    }

    if (arch_ == debug_ipc::Arch::kArm64) {
      // stack pointer
      PopulateRegister(debug_ipc::RegisterID::kARMv8_sp, reinterpret_cast<uint64_t>(sp_),
                       registers);
      // link register
      PopulateRegister(debug_ipc::RegisterID::kARMv8_lr, kReturnAddress, registers);
    } else if (arch_ == debug_ipc::Arch::kX64) {
      // stack pointer
      PopulateRegister(debug_ipc::RegisterID::kX64_rsp, reinterpret_cast<uint64_t>(sp_), registers);
    }
  }

  void Step(uint64_t process_koid) {
    // Increment the stack pointer to make it look as if we've stepped out of
    // the zx_channel function.
    sp_ = stack_ + kMaxStackSizeInWords;
    stepped_processes_.insert(process_koid);
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

  static constexpr uint64_t kReturnAddress = 0x123456798;
  static constexpr uint64_t kMaxStackSizeInWords = 0x100;
  static constexpr zx_txid_t kTxId = 0xaaaaaaaa;
  static constexpr zx_txid_t kTxId2 = 0x88888888;
  static constexpr uint32_t kFidlWireFormatMagicNumberInitial = 0x1;
  static constexpr uint64_t kOrdinal = 0x77e4cceb00000000lu;
  static constexpr uint64_t kOrdinal2 = 1234567890123456789lu;
  static constexpr char kElfSymbolBuildID[] = "123412341234";

 private:
  const std::vector<debug_ipc::RegisterID>* param_regs_;
  std::unique_ptr<SystemCallTest> syscall_;
  bool use_alternate_data_ = false;
  uint64_t stack_[kMaxStackSizeInWords];
  uint64_t* sp_;
  bool check_bytes_ = false;
  bool check_handles_ = false;
  fidl_message_header_t header_;
  fidl_message_header_t header2_;
  std::vector<uint8_t> large_bytes_;
  zx_handle_t handles_[2] = {0x01234567, 0x89abcdef};
  zx_handle_info_t handle_infos_[2] = {
      {0x01234567, ZX_OBJ_TYPE_CHANNEL,
       ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL | ZX_RIGHT_SIGNAL_PEER |
           ZX_RIGHT_WAIT | ZX_RIGHT_INSPECT,
       0},
      {0x89abcdef, ZX_OBJ_TYPE_LOG,
       ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL | ZX_RIGHT_WAIT |
           ZX_RIGHT_INSPECT,
       0}};
  zx_handle_t handles2_[2] = {0x76543210, 0xfedcba98};
  debug_ipc::Arch arch_;
  std::set<uint64_t> stepped_processes_;
};

// Provides the infrastructure needed to provide the data above.
class InterceptionRemoteAPI : public zxdb::MockRemoteAPI {
 public:
  InterceptionRemoteAPI(DataForSyscallTest& data, bool aborted) : data_(data), aborted_(aborted) {}

  void AddOrChangeBreakpoint(
      const debug_ipc::AddOrChangeBreakpointRequest& request,
      fit::callback<void(const zxdb::Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) override {
    breakpoints_[request.breakpoint.id] = request.breakpoint;
    MockRemoteAPI::AddOrChangeBreakpoint(request, std::move(cb));
  }

  void Attach(const debug_ipc::AttachRequest& request,
              fit::callback<void(const zxdb::Err&, debug_ipc::AttachReply)> cb) override {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb)]() mutable { cb(zxdb::Err(), debug_ipc::AttachReply()); });
  }

  void Modules(const debug_ipc::ModulesRequest& request,
               fit::callback<void(const zxdb::Err&, debug_ipc::ModulesReply)> cb) override {
    debug_ipc::ModulesReply reply;
    data_.PopulateModules(reply.modules);
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb), reply]() mutable { cb(zxdb::Err(), reply); });
  }

  void ReadMemory(const debug_ipc::ReadMemoryRequest& request,
                  fit::callback<void(const zxdb::Err&, debug_ipc::ReadMemoryReply)> cb) override {
    if (aborted_) {
      aborted_ = false;
      Process* process = global_dispatcher->SearchProcess(kFirstPid);
      FX_DCHECK(process != nullptr);
      int64_t timestamp = time(nullptr);
      global_dispatcher->AddStopMonitoringEvent(
          std::make_shared<StopMonitoringEvent>(timestamp, process));
    }
    debug_ipc::ReadMemoryReply reply;
    data_.PopulateMemoryBlockForAddress(request.address, request.size, reply.blocks.emplace_back());
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb), reply]() mutable { cb(zxdb::Err(), reply); });
  }

  void ReadRegisters(
      const debug_ipc::ReadRegistersRequest& request,
      fit::callback<void(const zxdb::Err&, debug_ipc::ReadRegistersReply)> cb) override {
    // TODO: Parameterize this so we can have more than one test.
    debug_ipc::ReadRegistersReply reply;
    data_.PopulateRegisters(request.process_koid, &reply.registers);
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb), reply]() mutable { cb(zxdb::Err(), reply); });
  }

  void Resume(const debug_ipc::ResumeRequest& request,
              fit::callback<void(const zxdb::Err&, debug_ipc::ResumeReply)> cb) override {
    debug_ipc::ResumeReply reply;
    data_.Step(request.process_koid);
    debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [cb = std::move(cb), reply]() mutable {
      cb(zxdb::Err(), reply);
      // This is so that the test can inject the next exception.
      debug_ipc::MessageLoop::Current()->QuitNow();
    });
  }

  void PopulateBreakpointIds(uint64_t address, debug_ipc::NotifyException& notification) {
    for (auto& breakpoint : breakpoints_) {
      if (address == breakpoint.second.locations[0].address) {
        notification.hit_breakpoints.emplace_back();
        notification.hit_breakpoints.back().id = breakpoint.first;
      }
    }
  }

  void LoadInfoHandleTable(
      const debug_ipc::LoadInfoHandleTableRequest& request,
      fit::callback<void(const zxdb::Err&, debug_ipc::LoadInfoHandleTableReply)> cb) override {
    debug_ipc::LoadInfoHandleTableReply reply;
    reply.handles.push_back(debug_ipc::InfoHandleExtended{
        .type = ZX_OBJ_TYPE_CHANNEL,
        .handle_value = kHandle,
        .rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL |
                  ZX_RIGHT_SIGNAL_PEER | ZX_RIGHT_WAIT | ZX_RIGHT_INSPECT,
        .koid = kHandleKoid,
        .related_koid = kHandle2Koid,
        .peer_owner_koid = 0});
    reply.handles.push_back(debug_ipc::InfoHandleExtended{
        .type = ZX_OBJ_TYPE_CHANNEL,
        .handle_value = kHandle2,
        .rights = ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_SIGNAL |
                  ZX_RIGHT_SIGNAL_PEER | ZX_RIGHT_WAIT | ZX_RIGHT_INSPECT,
        .koid = kHandle2Koid,
        .related_koid = kHandleKoid,
        .peer_owner_koid = 0});
    cb(zxdb::Err(), std::move(reply));
  }

 private:
  std::map<uint32_t, debug_ipc::BreakpointSettings> breakpoints_;
  DataForSyscallTest& data_;
  bool aborted_;
};

class InterceptionWorkflowTest : public zxdb::RemoteAPITest {
 public:
  InterceptionWorkflowTest(debug_ipc::Arch arch, bool aborted) : data_(arch), aborted_(aborted) {
    decode_options_.output_mode = OutputMode::kStandard;
    display_options_.pretty_print = true;
    display_options_.columns = 132;
    display_options_.needs_colors = true;
  }
  ~InterceptionWorkflowTest() override = default;

  InterceptionRemoteAPI& mock_remote_api() { return *mock_remote_api_; }

  std::unique_ptr<zxdb::RemoteAPI> GetRemoteAPIImpl() override {
    auto remote_api = std::make_unique<InterceptionRemoteAPI>(data_, aborted_);
    mock_remote_api_ = remote_api.get();
    return std::move(remote_api);
  }

  DataForSyscallTest& data() { return data_; }

  void set_with_process_info() { display_options_.with_process_info = true; }
  void set_dump_messages(bool dump_messages) { display_options_.dump_messages = dump_messages; }

  void set_bad_stack() { bad_stack_ = true; }

  void AddThread(zxdb::Thread* thread) { threads_[thread->GetKoid()] = thread; }

  void PerformCheckTest(const char* syscall_name, std::unique_ptr<SystemCallTest> syscall1,
                        std::unique_ptr<SystemCallTest> syscall2);

  void PerformDisplayTest(const char* syscall_name, std::unique_ptr<SystemCallTest> syscall,
                          const char* expected, fidl_codec::LibraryLoader* loader = nullptr);
  void PerformDisplayTest(ProcessController* controller, const char* syscall_name,
                          std::unique_ptr<SystemCallTest> syscall, const char* expected,
                          fidl_codec::LibraryLoader* loader = nullptr);

  void PerformOneThreadDisplayTest(const char* syscall_name,
                                   std::unique_ptr<SystemCallTest> syscall, const char* expected);

  void PerformInterleavedDisplayTest(const char* syscall_name,
                                     std::unique_ptr<SystemCallTest> syscall, const char* expected);
  void PerformInterleavedDisplayTest(ProcessController* controller, const char* syscall_name,
                                     std::unique_ptr<SystemCallTest> syscall, const char* expected);

  void PerformNoReturnDisplayTest(const char* syscall_name, std::unique_ptr<SystemCallTest> syscall,
                                  const char* expected);

  void PerformTest(const char* syscall_name, std::unique_ptr<SystemCallTest> syscall1,
                   std::unique_ptr<SystemCallTest> syscall2, ProcessController* controller,
                   std::unique_ptr<SyscallDecoderDispatcher> dispatcher, bool interleaved_test,
                   bool multi_thread);

  void PerformAbortedTest(const char* syscall_name, std::unique_ptr<SystemCallTest> syscall,
                          const char* expected);

  void SimulateSyscall(std::unique_ptr<SystemCallTest> syscall, ProcessController* controller,
                       bool interleaved_test, bool multi_thread);
  std::vector<std::unique_ptr<zxdb::Frame>> FillBreakpoint(debug_ipc::NotifyException* notification,
                                                           uint64_t process_koid,
                                                           uint64_t thread_koid);
  void TriggerSyscallBreakpoint(uint64_t process_koid, uint64_t thread_koid);
  void TriggerCallerBreakpoint(uint64_t process_koid, uint64_t thread_koid);

  void PerformExceptionDisplayTest(debug_ipc::ExceptionType type, const char* expected);
  void PerformExceptionTest(ProcessController* controller,
                            std::unique_ptr<SyscallDecoderDispatcher> dispatcher,
                            debug_ipc::ExceptionType type);
  void TriggerException(uint64_t process_koid, uint64_t thread_koid, debug_ipc::ExceptionType type);
  void PerformFunctionTest(ProcessController* controller, const char* syscall_name,
                           std::unique_ptr<SystemCallTest> syscall, uint64_t pid, uint64_t tid);

 protected:
  DataForSyscallTest data_;
  bool aborted_;
  InterceptionRemoteAPI* mock_remote_api_;  // Owned by the session.
  DecodeOptions decode_options_;
  DisplayOptions display_options_;
  std::stringstream result_;
  std::map<uint64_t, zxdb::Thread*> threads_;
  // Function which can simulate the fact that the syscall can modify some data.
  std::function<void()> update_data_;
  bool bad_stack_ = false;
  std::unique_ptr<SyscallDecoderDispatcher> last_decoder_dispatcher_;
};

class InterceptionWorkflowTestX64 : public InterceptionWorkflowTest {
 public:
  InterceptionWorkflowTestX64() : InterceptionWorkflowTest(GetArch(), false) {}
  ~InterceptionWorkflowTestX64() override = default;

  virtual debug_ipc::Arch GetArch() const override { return debug_ipc::Arch::kX64; }
};

class InterceptionWorkflowTestArm : public InterceptionWorkflowTest {
 public:
  InterceptionWorkflowTestArm() : InterceptionWorkflowTest(GetArch(), false) {}
  ~InterceptionWorkflowTestArm() override = default;

  virtual debug_ipc::Arch GetArch() const override { return debug_ipc::Arch::kArm64; }
};

class InterceptionWorkflowTestX64Aborted : public InterceptionWorkflowTest {
 public:
  InterceptionWorkflowTestX64Aborted() : InterceptionWorkflowTest(GetArch(), true) {}
  ~InterceptionWorkflowTestX64Aborted() override = default;

  virtual debug_ipc::Arch GetArch() const override { return debug_ipc::Arch::kX64; }
};

class InterceptionWorkflowTestArmAborted : public InterceptionWorkflowTest {
 public:
  InterceptionWorkflowTestArmAborted() : InterceptionWorkflowTest(GetArch(), true) {}
  ~InterceptionWorkflowTestArmAborted() override = default;

  virtual debug_ipc::Arch GetArch() const override { return debug_ipc::Arch::kArm64; }
};

// This does process setup for the test.  It creates fake processes, injects
// modules with the appropriate symbols, attaches to the processes, etc.
class ProcessController {
 public:
  ProcessController(InterceptionWorkflowTest* remote_api, zxdb::Session& session,
                    debug_ipc::MessageLoop& loop);
  ~ProcessController();

  InterceptionWorkflowTest* remote_api() const { return remote_api_; }
  InterceptionWorkflow& workflow() { return workflow_; }
  const std::vector<uint64_t>& process_koids() { return process_koids_; }
  uint64_t thread_koid(uint64_t process_koid) { return thread_koids_[process_koid]; }
  bool initialized() const { return initialized_; }

  std::unique_ptr<SyscallDecoderDispatcher> GetBackDispatcher() {
    return workflow_.GetBackDispatcher();
  }

  void InjectProcesses(zxdb::Session& session);

  // The syscall_name can be the empty string if no mock syscall is needed.
  void Initialize(zxdb::Session& session, std::unique_ptr<SyscallDecoderDispatcher> dispatcher,
                  const char* syscall_name);
  void Detach();

 private:
  InterceptionWorkflowTest* remote_api_;
  std::vector<uint64_t> process_koids_;
  std::map<uint64_t, uint64_t> thread_koids_;
  InterceptionWorkflow workflow_;

  std::vector<zxdb::Process*> processes_;
  std::vector<zxdb::Target*> targets_;
  size_t detached_processes_ = 0;
  bool initialized_ = false;
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
  explicit SyscallCheck(ProcessController* controller) : controller_(controller) {}

  void SyscallOutputsDecoded(SyscallDecoder* decoder) override {
    if (decoder->syscall()->name() == "zx_channel_write") {
      DataForSyscallTest& data = controller_->remote_api()->data();
      FX_DCHECK(decoder->ArgumentValue(0) == kHandle);  // handle
      FX_DCHECK(decoder->ArgumentValue(1) == 0);        // options
      FX_DCHECK(decoder->ArgumentLoaded(Stage::kEntry, 2, data.num_bytes()));
      uint8_t* bytes = decoder->ArgumentContent(Stage::kEntry, 2);
      if (memcmp(bytes, data.bytes(), data.num_bytes()) != 0) {
        std::string result = "bytes not equivalent\n";
        AppendElements(result, bytes, data.bytes(), data.num_bytes());
        FAIL() << result;
      }
      FX_DCHECK(decoder->ArgumentValue(3) == data.num_bytes());  // num_bytes
      FX_DCHECK(
          decoder->ArgumentLoaded(Stage::kEntry, 4, data.num_handles() * sizeof(zx_handle_t)));
      zx_handle_t* handles =
          reinterpret_cast<zx_handle_t*>(decoder->ArgumentContent(Stage::kEntry, 4));
      if (memcmp(handles, data.handles(), data.num_handles()) != 0) {
        std::string result = "handles not equivalent";
        AppendElements(result, handles, data.handles(), data.num_handles());
        FAIL() << result;
      }
      FX_DCHECK(decoder->ArgumentValue(5) == data.num_handles());  // num_handles
    } else if (decoder->syscall()->name() == "zx_channel_call") {
      DataForSyscallTest& data = controller_->remote_api()->data();
      FX_DCHECK(decoder->ArgumentValue(0) == kHandle);           // handle
      FX_DCHECK(decoder->ArgumentValue(1) == 0);                 // options
      FX_DCHECK(decoder->ArgumentValue(2) == ZX_TIME_INFINITE);  // deadline
      FX_DCHECK(decoder->ArgumentLoaded(Stage::kEntry, 3, sizeof(zx_channel_call_args_t)));
      const zx_channel_call_args_t* args = reinterpret_cast<const zx_channel_call_args_t*>(
          decoder->ArgumentContent(Stage::kEntry, 3));
      uint8_t* ref_bytes;
      uint32_t ref_num_bytes;
      if (data.use_alternate_data()) {
        ref_bytes = data.bytes2();
        ref_num_bytes = data.num_bytes2();
      } else {
        ref_bytes = data.bytes();
        ref_num_bytes = data.num_bytes();
      }
      FX_DCHECK(args->wr_num_bytes == ref_num_bytes);
      FX_DCHECK(decoder->BufferLoaded(Stage::kExit, uint64_t(args->wr_bytes), args->wr_num_bytes));
      uint8_t* bytes = decoder->BufferContent(Stage::kExit, uint64_t(args->wr_bytes));
      if (memcmp(bytes, ref_bytes, ref_num_bytes) != 0) {
        std::string result = "bytes not equivalent\n";
        AppendElements(result, bytes, ref_bytes, ref_num_bytes);
        FAIL() << result;
      }
    } else {
      FAIL() << "can't check " << decoder->syscall()->name();
    }
  }

  void SyscallDecodingError(const DecoderError& error, SyscallDecoder* decoder) override {
    SyscallUse::SyscallDecodingError(error, decoder);
    FAIL();
  }

 private:
  ProcessController* controller_;
};

class SyscallDecoderDispatcherTest : public SyscallDecoderDispatcher {
 public:
  SyscallDecoderDispatcherTest(const DecodeOptions& decode_options, ProcessController* controller)
      : SyscallDecoderDispatcher(decode_options), controller_(controller) {}

  std::unique_ptr<SyscallDecoder> CreateDecoder(InterceptingThreadObserver* thread_observer,
                                                zxdb::Thread* thread,
                                                const Syscall* syscall) override {
    return std::make_unique<SyscallDecoder>(this, thread_observer, thread, syscall,
                                            std::make_unique<SyscallCheck>(controller_));
  }

  std::unique_ptr<ExceptionDecoder> CreateDecoder(InterceptionWorkflow* workflow,
                                                  zxdb::Thread* thread) override {
    return nullptr;
  }

  void DeleteDecoder(SyscallDecoder* decoder) override {
    SyscallDecoderDispatcher::DeleteDecoder(decoder);
    AlwaysQuit aq(controller_);
  }

  void DeleteDecoder(ExceptionDecoder* decoder) override {
    SyscallDecoderDispatcher::DeleteDecoder(decoder);
    AlwaysQuit aq(controller_);
  }

 private:
  ProcessController* controller_;
};

class SyscallDisplayDispatcherTest : public SyscallDisplayDispatcher {
 public:
  SyscallDisplayDispatcherTest(fidl_codec::LibraryLoader* loader,
                               const DecodeOptions& decode_options,
                               const DisplayOptions& display_options, std::ostream& os,
                               ProcessController* controller, bool aborted)
      : SyscallDisplayDispatcher(loader, decode_options, display_options, os),
        controller_(controller),
        aborted_(aborted),
        replay_dispatcher_(std::make_unique<SyscallDisplayDispatcher>(loader, decode_options,
                                                                      display_options, os)),
        replay_(replay_dispatcher_.get()) {}

  ProcessController* controller() const { return controller_; }

  void DeleteDecoder(SyscallDecoder* decoder) override {
    SyscallDisplayDispatcher::DeleteDecoder(decoder);
    AlwaysQuit aq(controller_);
  }

  void DeleteDecoder(ExceptionDecoder* decoder) override {
    SyscallDecoderDispatcher::DeleteDecoder(decoder);
    AlwaysQuit aq(controller_);
  }

  void AddProcessLaunchedEvent(std::shared_ptr<ProcessLaunchedEvent> event) override {}

  void AddProcessMonitoredEvent(std::shared_ptr<ProcessMonitoredEvent> event) override {}

  void AddStopMonitoringEvent(std::shared_ptr<StopMonitoringEvent> event) override {
    if (aborted_) {
      SyscallDisplayDispatcher::AddStopMonitoringEvent(std::move(event));
    }
  }

  // For events, instead of dispatching them using this dispatcher, we dispatch them using the
  // replay dispatcher. This method ensures that the thread/process used by an event which has
  // been created in this dispatcher is also created in the replay dispatcher.
  void CreateReplayThread(Thread* thread) {
    Thread* replay_thread = replay_dispatcher_->SearchThread(thread->koid());
    if (replay_thread == nullptr) {
      Process* process = thread->process();
      Process* replay_process = replay_dispatcher_->SearchProcess(process->koid());
      if (replay_process == nullptr) {
        replay_process =
            replay_dispatcher_->CreateProcess(process->name(), process->koid(), nullptr);
      }
      replay_dispatcher_->CreateThread(thread->koid(), replay_process);
    }
  }

  void AddInvokedEvent(std::shared_ptr<InvokedEvent> invoked_event) override {
    // Set the invoked event id (this is usually done by SyscallDisplayDispatcher).
    invoked_event->set_id(GetNextInvokedEventId());
    // Ensure that the thread/process are created for the replay dispatcher.
    CreateReplayThread(invoked_event->thread());
    // Create a proto event.
    proto::Event proto_event;
    invoked_event->Write(&proto_event);
    // Replay the proto event. This will dispatch the event to the replay dispatcher. Because both
    // this dispatcher and the replay dispatcher have the output stream, the output must be
    // unchanged.
    replay_.DecodeAndDispatchEvent(proto_event);
  }

  void AddOutputEvent(std::shared_ptr<OutputEvent> output_event) override {
    // Create a proto event.
    proto::Event proto_event;
    output_event->Write(&proto_event);
    // Replay the proto event. This will dispatch the event to the replay dispatcher. Because both
    // this dispatcher and the replay dispatcher have the output stream, the output must be
    // unchanged.
    replay_.DecodeAndDispatchEvent(proto_event);
  }

  void AddExceptionEvent(std::shared_ptr<ExceptionEvent> exception_event) override {
    // Ensure that the thread/process are created for the replay dispatcher.
    CreateReplayThread(exception_event->thread());
    // Create a proto event.
    proto::Event proto_event;
    exception_event->Write(&proto_event);
    // Replay the proto event. This will dispatch the event to the replay dispatcher. Because both
    // this dispatcher and the replay dispatcher have the output stream, the output must be
    // unchanged.
    replay_.DecodeAndDispatchEvent(proto_event);
  }

 private:
  ProcessController* controller_;
  bool aborted_;
  // Dispatcher used to test the save/replay of events.
  std::unique_ptr<SyscallDisplayDispatcher> replay_dispatcher_;
  // Used to replay saved events.
  Replay replay_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_INTERCEPTION_TESTS_INTERCEPTION_WORKFLOW_TEST_H_
