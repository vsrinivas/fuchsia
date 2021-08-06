// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/interception_tests/interception_workflow_test.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/expr/expr_parser.h"

namespace fidlcat {

// We only test one syscall at a time. We always use the same address for all the syscalls.
constexpr uint64_t kSyscallAddress = 0x100060;
// Address used to generate an exception.
constexpr uint64_t kExceptionAddress = 0x12345678;

constexpr int kFrame1Line = 25;
constexpr int kFrame1Column = 8;
constexpr int kFrame2Line = 50;
constexpr int kFrame2Column = 4;
constexpr int kFrame3Line = 10;
constexpr int kFrame3Column = 2;

constexpr int kFrame2Sp = 0x126790;
constexpr int kFrame3Sp = 0x346712;

constexpr uint64_t kTestTimestampDefault = 0x74657374l;  // hexadecimal for "test" in ascii

static std::vector<debug_ipc::RegisterID> aarch64_regs = {
    debug_ipc::RegisterID::kARMv8_x0, debug_ipc::RegisterID::kARMv8_x1,
    debug_ipc::RegisterID::kARMv8_x2, debug_ipc::RegisterID::kARMv8_x3,
    debug_ipc::RegisterID::kARMv8_x4, debug_ipc::RegisterID::kARMv8_x5,
    debug_ipc::RegisterID::kARMv8_x6, debug_ipc::RegisterID::kARMv8_x7};

static std::vector<debug_ipc::RegisterID> amd64_regs = {
    debug_ipc::RegisterID::kX64_rdi, debug_ipc::RegisterID::kX64_rsi,
    debug_ipc::RegisterID::kX64_rdx, debug_ipc::RegisterID::kX64_rcx,
    debug_ipc::RegisterID::kX64_r8,  debug_ipc::RegisterID::kX64_r9};

SyscallDecoderDispatcher* global_dispatcher = nullptr;

DataForSyscallTest::DataForSyscallTest(debug_ipc::Arch arch) : arch_(arch) {
  param_regs_ = (arch_ == debug_ipc::Arch::kArm64) ? &aarch64_regs : &amd64_regs;
  header_.txid = kTxId;
  header_.magic_number = kFidlWireFormatMagicNumberInitial;
  header_.flags[0] = 0;
  header_.flags[1] = 0;
  header_.flags[2] = 0;
  header_.ordinal = kOrdinal;
  header2_.txid = kTxId2;
  header2_.magic_number = kFidlWireFormatMagicNumberInitial;
  header2_.flags[0] = 0;
  header2_.flags[1] = 0;
  header2_.flags[2] = 0;
  header2_.ordinal = kOrdinal2;

  for (int i = 0; i < 100; ++i) {
    large_bytes_.push_back(i * i);
  }
  sp_ = stack_ + kMaxStackSizeInWords;
}

void InterceptionWorkflowTest::PerformDisplayTest(const char* syscall_name,
                                                  std::unique_ptr<SystemCallTest> syscall,
                                                  const char* expected,
                                                  fidl_codec::LibraryLoader* loader) {
  ProcessController controller(this, session(), loop());
  PerformDisplayTest(&controller, syscall_name, std::move(syscall), expected, loader);
  last_decoder_dispatcher_ = controller.GetBackDispatcher();
}

void InterceptionWorkflowTest::PerformDisplayTest(ProcessController* controller,
                                                  const char* syscall_name,
                                                  std::unique_ptr<SystemCallTest> syscall,
                                                  const char* expected,
                                                  fidl_codec::LibraryLoader* loader) {
  PerformTest(syscall_name, std::move(syscall), nullptr, controller,
              std::make_unique<SyscallDisplayDispatcherTest>(
                  loader, decode_options_, display_options_, result_, controller, aborted_),
              /*interleaved_test=*/false, /*multi_thread=*/true);
  std::string both_results = result_.str();
  // The second output starts with "\x1B[32m0.000000\x1B[0m test_2718"
  size_t split = both_results.find("\x1B[32m0.000000\x1B[0m test_2718");
  ASSERT_NE(split, std::string::npos) << both_results;
  if (!display_options_.with_process_info) {
    // When we don't have the process info on each line, the first displayed line is empty (instead
    // of having the process name, process id and thread id). Go back one position to add this line
    // to the second comparison (and remove it from the first comparison);
    --split;
  }
  std::string first = both_results.substr(0, split);
  std::string second = both_results.substr(split);

  // Check that the two syscalls generated the data we expect.
  ASSERT_EQ(expected, first);
  ASSERT_NE(expected, second);

  std::string str_expected(expected);
  // The expected and the second should have the same data from different pids.  Replace
  // the pid from the expected with the pid from the second, and they should look
  // the same.
  size_t i = 0;
  std::string first_pid = std::to_string(kFirstPid);
  std::string second_pid = std::to_string(kSecondPid);
  while ((i = str_expected.find(first_pid, i)) != std::string::npos) {
    str_expected.replace(i, first_pid.length(), second_pid);
    i += second_pid.length();
  }
  // Do it also for thread koids.
  i = 0;
  std::string first_thread_koid = std::to_string(kFirstThreadKoid);
  std::string second_thread_koid = std::to_string(kSecondThreadKoid);
  while ((i = str_expected.find(first_thread_koid, i)) != std::string::npos) {
    str_expected.replace(i, first_thread_koid.length(), second_thread_koid);
    i += second_thread_koid.length();
  }
  ASSERT_EQ(str_expected, second);
}

void InterceptionWorkflowTest::PerformOneThreadDisplayTest(const char* syscall_name,
                                                           std::unique_ptr<SystemCallTest> syscall,
                                                           const char* expected) {
  ProcessController controller(this, session(), loop());

  PerformTest(syscall_name, std::move(syscall), nullptr, &controller,
              std::make_unique<SyscallDisplayDispatcherTest>(
                  nullptr, decode_options_, display_options_, result_, &controller, aborted_),
              /*interleaved_test=*/false, /*multi_thread=*/false);
  ASSERT_EQ(expected, result_.str());
}

void InterceptionWorkflowTest::PerformInterleavedDisplayTest(
    const char* syscall_name, std::unique_ptr<SystemCallTest> syscall, const char* expected) {
  ProcessController controller(this, session(), loop());
  PerformInterleavedDisplayTest(&controller, syscall_name, std::move(syscall), expected);
}

void InterceptionWorkflowTest::PerformInterleavedDisplayTest(
    ProcessController* controller, const char* syscall_name,
    std::unique_ptr<SystemCallTest> syscall, const char* expected) {
  PerformTest(syscall_name, std::move(syscall), nullptr, controller,
              std::make_unique<SyscallDisplayDispatcherTest>(
                  nullptr, decode_options_, display_options_, result_, controller, aborted_),
              /*interleaved_test=*/true, /*multi_thread=*/true);
  ASSERT_EQ(expected, result_.str());
}

void InterceptionWorkflowTest::PerformNoReturnDisplayTest(const char* syscall_name,
                                                          std::unique_ptr<SystemCallTest> syscall,
                                                          const char* expected) {
  ProcessController controller(this, session(), loop());
  controller.Initialize(
      session(),
      std::make_unique<SyscallDisplayDispatcherTest>(nullptr, decode_options_, display_options_,
                                                     result_, &controller, aborted_),
      syscall_name);

  data_.set_syscall(std::move(syscall));
  data_.load_syscall_data();

  TriggerSyscallBreakpoint(kFirstPid, kFirstThreadKoid);

  ASSERT_EQ(expected, result_.str());
}

void InterceptionWorkflowTest::PerformTest(const char* syscall_name,
                                           std::unique_ptr<SystemCallTest> syscall1,
                                           std::unique_ptr<SystemCallTest> syscall2,
                                           ProcessController* controller,
                                           std::unique_ptr<SyscallDecoderDispatcher> dispatcher,
                                           bool interleaved_test, bool multi_thread) {
  controller->Initialize(session(), std::move(dispatcher), syscall_name);

  SimulateSyscall(std::move(syscall1), controller, interleaved_test, multi_thread);

  if (multi_thread) {
    debug::MessageLoop::Current()->Run();
  }

  if (syscall2 != nullptr) {
    data_.set_use_alternate_data();
    SimulateSyscall(std::move(syscall2), controller, interleaved_test, multi_thread);
  }
}

void InterceptionWorkflowTest::PerformAbortedTest(const char* syscall_name,
                                                  std::unique_ptr<SystemCallTest> syscall,
                                                  const char* expected) {
  ProcessController controller(this, session(), loop());
  auto decoder = std::make_unique<SyscallDisplayDispatcherTest>(
      nullptr, decode_options_, display_options_, result_, &controller, aborted_);
  controller.Initialize(session(), std::move(decoder), syscall_name);
  data_.set_syscall(std::move(syscall));
  data_.load_syscall_data();
  TriggerSyscallBreakpoint(kFirstPid, kFirstThreadKoid);
  ASSERT_EQ(expected, result_.str());
}

void InterceptionWorkflowTest::SimulateSyscall(std::unique_ptr<SystemCallTest> syscall,
                                               ProcessController* controller, bool interleaved_test,
                                               bool multi_thread) {
  data_.set_syscall(std::move(syscall));
  if (multi_thread) {
    if (interleaved_test) {
      for (uint64_t process_koid : controller->process_koids()) {
        data_.load_syscall_data();
        TriggerSyscallBreakpoint(process_koid, controller->thread_koid(process_koid));
      }
      for (uint64_t process_koid : controller->process_koids()) {
        TriggerCallerBreakpoint(process_koid, controller->thread_koid(process_koid));
      }
    } else {
      for (uint64_t process_koid : controller->process_koids()) {
        data_.load_syscall_data();
        uint64_t thread_koid = controller->thread_koid(process_koid);
        TriggerSyscallBreakpoint(process_koid, thread_koid);
        TriggerCallerBreakpoint(process_koid, thread_koid);
      }
    }
  } else {
    data_.load_syscall_data();
    TriggerSyscallBreakpoint(kFirstPid, kFirstThreadKoid);
    if (update_data_ != nullptr) {
      update_data_();
    }
    TriggerCallerBreakpoint(kFirstPid, kFirstThreadKoid);
  }
}

// Fill a NotifyException object with all the information we need to simulate a breakpoint.
std::vector<std::unique_ptr<zxdb::Frame>> InterceptionWorkflowTest::FillBreakpoint(
    debug_ipc::NotifyException* notification, uint64_t process_koid, uint64_t thread_koid) {
  notification->timestamp = 0;
  notification->type = debug_ipc::ExceptionType::kSoftwareBreakpoint;
  notification->thread.id = {.process = process_koid, .thread = thread_koid};
  notification->thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  notification->thread.stack_amount = debug_ipc::ThreadRecord::StackAmount::kMinimal;

  std::vector<std::unique_ptr<zxdb::Frame>> frames;

  if (!bad_stack_) {
    debug_ipc::StackFrame frame1(kSyscallAddress, reinterpret_cast<uint64_t>(data_.sp()));
    debug_ipc::StackFrame frame2(kSyscallAddress, kFrame2Sp);
    debug_ipc::StackFrame frame3(kSyscallAddress, kFrame3Sp);

    data_.PopulateRegisters(process_koid, &frame1.regs);
    notification->thread.frames.push_back(frame1);

    zxdb::SymbolContext context(0);
    frames.emplace_back(std::make_unique<zxdb::FrameImpl>(
        threads_[thread_koid], frame1,
        zxdb::Location(kExceptionAddress, zxdb::FileLine("fidlcat/foo.cc", kFrame1Line),
                       kFrame1Column, context)));
    frames.emplace_back(std::make_unique<zxdb::FrameImpl>(
        threads_[thread_koid], frame2,
        zxdb::Location(kExceptionAddress, zxdb::FileLine("fidlcat/foo.cc", kFrame2Line),
                       kFrame2Column, context)));
    frames.emplace_back(std::make_unique<zxdb::FrameImpl>(
        threads_[thread_koid], frame2,
        zxdb::Location(kExceptionAddress, zxdb::FileLine("fidlcat/main.cc", kFrame3Line),
                       kFrame3Column, context)));
  }
  return frames;
}

void InterceptionWorkflowTest::TriggerSyscallBreakpoint(uint64_t process_koid,
                                                        uint64_t thread_koid) {
  // Trigger breakpoint.
  debug_ipc::NotifyException notification;
  std::vector<std::unique_ptr<zxdb::Frame>> frames =
      FillBreakpoint(&notification, process_koid, thread_koid);

  mock_remote_api().PopulateBreakpointIds(kSyscallAddress, notification);

  InjectExceptionWithStack(notification, std::move(frames), /*has_all_frames=*/true);

  if (!aborted_ && !bad_stack_) {
    debug::MessageLoop::Current()->Run();
  }
}

void InterceptionWorkflowTest::TriggerCallerBreakpoint(uint64_t process_koid,
                                                       uint64_t thread_koid) {
  // Trigger next breakpoint, when the syscall has completed.
  debug_ipc::NotifyException notification;
  notification.timestamp = 0;
  notification.type = debug_ipc::ExceptionType::kSoftwareBreakpoint;
  notification.thread.id = {.process = process_koid, .thread = thread_koid};
  notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  notification.thread.stack_amount = debug_ipc::ThreadRecord::StackAmount::kMinimal;

  debug_ipc::StackFrame frame(DataForSyscallTest::kReturnAddress,
                              reinterpret_cast<uint64_t>(data_.sp()));

  data_.PopulateRegisters(process_koid, &frame.regs);
  notification.thread.frames.push_back(frame);

  mock_remote_api().PopulateBreakpointIds(DataForSyscallTest::kReturnAddress, notification);

  InjectException(notification);

  debug::MessageLoop::Current()->Run();
}

void InterceptionWorkflowTest::PerformExceptionDisplayTest(debug_ipc::ExceptionType type,
                                                           const char* expected) {
  ProcessController controller(this, session(), loop());

  PerformExceptionTest(
      &controller,
      std::make_unique<SyscallDisplayDispatcherTest>(nullptr, decode_options_, display_options_,
                                                     result_, &controller, aborted_),
      type);
  ASSERT_EQ(result_.str(), expected);
}

void InterceptionWorkflowTest::PerformExceptionTest(
    ProcessController* controller, std::unique_ptr<SyscallDecoderDispatcher> dispatcher,
    debug_ipc::ExceptionType type) {
  controller->Initialize(session(), std::move(dispatcher), "");

  TriggerException(kFirstPid, kFirstThreadKoid, type);

  debug::MessageLoop::Current()->Run();
}

void InterceptionWorkflowTest::TriggerException(uint64_t process_koid, uint64_t thread_koid,
                                                debug_ipc::ExceptionType type) {
  // Trigger breakpoint.
  debug_ipc::NotifyException notification;
  notification.timestamp = 0;
  notification.type = type;
  notification.thread.id = {.process = process_koid, .thread = thread_koid};
  notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  notification.thread.stack_amount = debug_ipc::ThreadRecord::StackAmount::kMinimal;

  debug_ipc::StackFrame frame1(kExceptionAddress, reinterpret_cast<uint64_t>(data_.sp()));
  debug_ipc::StackFrame frame2(kExceptionAddress, kFrame2Sp);
  debug_ipc::StackFrame frame3(kExceptionAddress, kFrame3Sp);

  data_.PopulateRegisters(process_koid, &frame1.regs);
  notification.thread.frames.push_back(frame1);

  mock_remote_api().PopulateBreakpointIds(kExceptionAddress, notification);

  zxdb::SymbolContext context(0);
  std::vector<std::unique_ptr<zxdb::Frame>> frames;
  frames.emplace_back(std::make_unique<zxdb::FrameImpl>(
      threads_[thread_koid], frame1,
      zxdb::Location(kExceptionAddress, zxdb::FileLine("fidlcat/foo.cc", kFrame1Line),
                     kFrame1Column, context)));
  frames.emplace_back(std::make_unique<zxdb::FrameImpl>(
      threads_[thread_koid], frame2,
      zxdb::Location(kExceptionAddress, zxdb::FileLine("fidlcat/foo.cc", kFrame2Line),
                     kFrame2Column, context)));
  frames.emplace_back(std::make_unique<zxdb::FrameImpl>(
      threads_[thread_koid], frame2,
      zxdb::Location(kExceptionAddress, zxdb::FileLine("fidlcat/main.cc", kFrame3Line),
                     kFrame3Column, context)));

  InjectExceptionWithStack(notification, std::move(frames), /*has_all_frames=*/true);
}

// Functions are different from syscalls because syscalls have a '@' in their name.
// Because of that, zxdb handles the syscalls differently.
// For functions, we can't use TriggerSyscallBreakpoint because the breakpoint is not
// recognized.
void InterceptionWorkflowTest::PerformFunctionTest(ProcessController* controller,
                                                   const char* syscall_name,
                                                   std::unique_ptr<SystemCallTest> syscall,
                                                   uint64_t pid, uint64_t tid) {
  if (!controller->initialized()) {
    controller->Initialize(
        session(),
        std::make_unique<SyscallDisplayDispatcherTest>(nullptr, decode_options_, display_options_,
                                                       result_, controller, aborted_),
        syscall_name);
  }
  data_.set_syscall(std::move(syscall));
  data_.load_syscall_data();

  debug_ipc::NotifyException notification;
  // Fill the breakpoint.
  std::vector<std::unique_ptr<zxdb::Frame>> frames = FillBreakpoint(&notification, pid, tid);
  threads_[tid]->GetStack().SetFramesForTest(std::move(frames),
                                             /*has_all_frames=*/true);

  // Instead of using PopulateBreakpointIds and InjectException, we need to directly
  // call our function interception code.
  SyscallDecoderDispatcher* dispatcher = controller->workflow().syscall_decoder_dispatcher();
  for (const auto& syscall : dispatcher->syscalls()) {
    if (syscall.second->name() == syscall_name) {
      dispatcher->DecodeSyscall(&controller->workflow().thread_observer(), threads_[tid],
                                syscall.second.get(), kTestTimestampDefault);
      break;
    }
  }

  debug::MessageLoop::Current()->Run();
}

ProcessController::ProcessController(InterceptionWorkflowTest* remote_api, zxdb::Session& session,
                                     debug::MessageLoop& loop)
    : remote_api_(remote_api), workflow_(&session, &loop) {
  process_koids_ = {kFirstPid, kSecondPid};
  thread_koids_[kFirstPid] = kFirstThreadKoid;
  thread_koids_[kSecondPid] = kSecondThreadKoid;
}

ProcessController::~ProcessController() {}

void ProcessController::InjectProcesses(zxdb::Session& session) {
  for (auto process_koid : process_koids_) {
    std::string test_name = "test_" + std::to_string(process_koid);
    zxdb::TargetImpl* target = session.system().CreateNewTargetImpl(nullptr);
    target->CreateProcessForTesting(process_koid, test_name);
    processes_.push_back(target->GetProcess());
  }
}

void ProcessController::Initialize(zxdb::Session& session,
                                   std::unique_ptr<SyscallDecoderDispatcher> dispatcher,
                                   const char* syscall_name) {
  initialized_ = true;
  global_dispatcher = dispatcher.get();
  std::vector<std::string> blank;
  workflow_.Initialize(blank, blank, blank, blank, std::nullopt, blank, std::move(dispatcher),
                       false);

  // Create fake processes and threads.
  InjectProcesses(session);

  for (zxdb::Process* process : processes_) {
    zxdb::Thread* the_thread =
        remote_api_->InjectThread(process->GetKoid(), thread_koids_[process->GetKoid()]);

    // Observe thread.  This is usually done in workflow_::Attach, but
    // RemoteAPITest has its own ideas about attaching, so that method only
    // half-works (the half that registers the target with the workflow). We
    // have to register the observer manually.
    zxdb::Target* target = process->GetTarget();
    targets_.push_back(target);
    remote_api_->AddThread(the_thread);
  }

  // Attach to processes.
  debug::MessageLoop::Current()->PostTask(FROM_HERE, [this]() {
    workflow_.Attach(process_koids_);
    debug::MessageLoop::Current()->QuitNow();
  });
  debug::MessageLoop::Current()->Run();

  // Load modules into program (including the one with the |syscall_name| symbol)
  auto module_symbols = fxl::MakeRefCounted<zxdb::MockModuleSymbols>("zx.so");
  session.system().GetSymbols()->InjectModuleForTesting(DataForSyscallTest::kElfSymbolBuildID,
                                                        module_symbols.get());

  // Inject the syscall symbol if requested. Use the full parser to parse the input identifier to
  // handle all possible cases.
  if (strlen(syscall_name) > 0) {
    zxdb::Identifier syscall_identifier;
    zxdb::Err err = zxdb::ExprParser::ParseIdentifier(syscall_name, &syscall_identifier);
    EXPECT_TRUE(err.ok()) << err.msg();
    module_symbols->AddSymbolLocations(
        syscall_identifier, {zxdb::Location(zxdb::Location::State::kSymbolized, kSyscallAddress)});
  }

  for (zxdb::Target* target : session.system().GetTargets()) {
    zxdb::Err err;
    std::vector<debug_ipc::Module> modules;
    // Force system to load modules.  Callback doesn't need to do anything
    // interesting.
    if (target->GetProcess() != nullptr) {
      target->GetProcess()->GetModules(
          [](const zxdb::Err& /*err*/, std::vector<debug_ipc::Module> /*modules*/) {
            debug::MessageLoop::Current()->QuitNow();
          });
      debug::MessageLoop::Current()->Run();
    }
  }
}

void ProcessController::Detach() {
  if (++detached_processes_ == processes_.size()) {
    workflow_.Shutdown();
  }
}

// This tests keeps track of which syscalls have automation.
// It will be destroyed when everything is implemented.
TEST_F(InterceptionWorkflowTestX64, SyscallsAutomated) {
  std::stringstream actual;
  uint32_t actual_fully_automated = 0;
  uint32_t actual_cant_be_automated = 0;
  uint32_t actual_partially_automated = 0;
  uint32_t actual_not_automated = 0;
  ProcessController controller(this, session(), loop());
  controller.Initialize(
      session(),
      std::make_unique<SyscallDisplayDispatcherTest>(nullptr, decode_options_, display_options_,
                                                     result_, &controller, aborted_),
      "");
  SyscallDecoderDispatcher* dispatcher = controller.workflow().syscall_decoder_dispatcher();
  for (const auto& syscall : dispatcher->syscalls()) {
    if (syscall.second->fully_automated()) {
      if (syscall.second->invoked_bp_instructions().size() +
              syscall.second->exit_bp_instructions().size() >
          0) {
        actual << syscall.second->name() << " fully automated\n";
        ++actual_fully_automated;
      } else {
        actual << syscall.second->name() << " doesn't need automation\n";
        ++actual_cant_be_automated;
      }
    } else {
      if (syscall.second->invoked_bp_instructions().size() +
              syscall.second->exit_bp_instructions().size() >
          0) {
        actual << syscall.second->name() << " partially automated\n";
        ++actual_partially_automated;
      } else {
        actual << syscall.second->name() << " not automated\n";
        ++actual_not_automated;
      }
    }
  }
  std::string expected =
      "__libc_extensions_init partially automated\n"
      "processargs_extract_handles fully automated\n"
      "zx_bti_create fully automated\n"
      "zx_bti_pin fully automated\n"
      "zx_bti_release_quarantine doesn't need automation\n"
      "zx_cache_flush doesn't need automation\n"
      "zx_channel_call partially automated\n"
      "zx_channel_call_etc partially automated\n"
      "zx_channel_create fully automated\n"
      "zx_channel_read partially automated\n"
      "zx_channel_read_etc partially automated\n"
      "zx_channel_write fully automated\n"
      "zx_channel_write_etc fully automated\n"
      "zx_clock_adjust doesn't need automation\n"
      "zx_clock_get fully automated\n"
      "zx_clock_get_monotonic doesn't need automation\n"
      "zx_cprng_add_entropy fully automated\n"
      "zx_cprng_draw fully automated\n"
      "zx_deadline_after doesn't need automation\n"
      "zx_debug_read partially automated\n"
      "zx_debug_send_command fully automated\n"
      "zx_debug_write fully automated\n"
      "zx_debuglog_create fully automated\n"
      "zx_debuglog_read fully automated\n"
      "zx_debuglog_write fully automated\n"
      "zx_event_create fully automated\n"
      "zx_eventpair_create fully automated\n"
      "zx_exception_get_process fully automated\n"
      "zx_exception_get_thread fully automated\n"
      "zx_fifo_create fully automated\n"
      "zx_fifo_read not automated\n"
      "zx_fifo_write not automated\n"
      "zx_framebuffer_get_info fully automated\n"
      "zx_framebuffer_set_range doesn't need automation\n"
      "zx_futex_get_owner fully automated\n"
      "zx_futex_requeue doesn't need automation\n"
      "zx_futex_requeue_single_owner doesn't need automation\n"
      "zx_futex_wait doesn't need automation\n"
      "zx_futex_wake doesn't need automation\n"
      "zx_futex_wake_handle_close_thread_exit doesn't need automation\n"
      "zx_futex_wake_single_owner doesn't need automation\n"
      "zx_guest_create fully automated\n"
      "zx_guest_set_trap doesn't need automation\n"
      "zx_handle_close doesn't need automation\n"
      "zx_handle_close_many fully automated\n"
      "zx_handle_duplicate fully automated\n"
      "zx_handle_replace fully automated\n"
      "zx_interrupt_ack doesn't need automation\n"
      "zx_interrupt_bind doesn't need automation\n"
      "zx_interrupt_bind_vcpu doesn't need automation\n"
      "zx_interrupt_create fully automated\n"
      "zx_interrupt_destroy doesn't need automation\n"
      "zx_interrupt_trigger doesn't need automation\n"
      "zx_interrupt_wait fully automated\n"
      "zx_iommu_create partially automated\n"
      "zx_ioports_release doesn't need automation\n"
      "zx_ioports_request doesn't need automation\n"
      "zx_job_create fully automated\n"
      "zx_job_set_policy not automated\n"
      "zx_ktrace_control not automated\n"
      "zx_ktrace_read partially automated\n"
      "zx_ktrace_write doesn't need automation\n"
      "zx_mtrace_control fully automated\n"
      "zx_nanosleep doesn't need automation\n"
      "zx_object_get_child fully automated\n"
      "zx_object_get_info partially automated\n"
      "zx_object_get_property fully automated\n"
      "zx_object_set_profile doesn't need automation\n"
      "zx_object_set_property fully automated\n"
      "zx_object_signal doesn't need automation\n"
      "zx_object_signal_peer doesn't need automation\n"
      "zx_object_wait_async doesn't need automation\n"
      "zx_object_wait_many not automated\n"
      "zx_object_wait_one fully automated\n"
      "zx_pager_create fully automated\n"
      "zx_pager_create_vmo fully automated\n"
      "zx_pager_detach_vmo doesn't need automation\n"
      "zx_pager_supply_pages doesn't need automation\n"
      "zx_pc_firmware_tables fully automated\n"
      "zx_pci_add_subtract_io_range doesn't need automation\n"
      "zx_pci_cfg_pio_rw fully automated\n"
      "zx_pci_config_read fully automated\n"
      "zx_pci_config_write doesn't need automation\n"
      "zx_pci_enable_bus_master doesn't need automation\n"
      "zx_pci_get_bar partially automated\n"
      "zx_pci_get_nth_device partially automated\n"
      "zx_pci_init not automated\n"
      "zx_pci_map_interrupt fully automated\n"
      "zx_pci_query_irq_mode fully automated\n"
      "zx_pci_reset_device doesn't need automation\n"
      "zx_pci_set_irq_mode doesn't need automation\n"
      "zx_pmt_unpin doesn't need automation\n"
      "zx_port_cancel doesn't need automation\n"
      "zx_port_create fully automated\n"
      "zx_port_queue not automated\n"
      "zx_port_wait not automated\n"
      "zx_process_create fully automated\n"
      "zx_process_exit doesn't need automation\n"
      "zx_process_read_memory fully automated\n"
      "zx_process_start doesn't need automation\n"
      "zx_process_write_memory fully automated\n"
      "zx_profile_create partially automated\n"
      "zx_resource_create fully automated\n"
      "zx_smc_call not automated\n"
      "zx_socket_create fully automated\n"
      "zx_socket_read partially automated\n"
      "zx_socket_set_disposition doesn't need automation\n"
      "zx_socket_shutdown doesn't need automation\n"
      "zx_socket_write partially automated\n"
      "zx_system_get_dcache_line_size doesn't need automation\n"
      "zx_system_get_event fully automated\n"
      "zx_system_get_features fully automated\n"
      "zx_system_get_num_cpus doesn't need automation\n"
      "zx_system_get_physmem doesn't need automation\n"
      "zx_system_get_version fully automated\n"
      "zx_system_mexec doesn't need automation\n"
      "zx_system_mexec_payload_get fully automated\n"
      "zx_system_powerctl not automated\n"
      "zx_task_create_exception_channel fully automated\n"
      "zx_task_kill doesn't need automation\n"
      "zx_task_suspend fully automated\n"
      "zx_task_suspend_token fully automated\n"
      "zx_thread_create fully automated\n"
      "zx_thread_exit doesn't need automation\n"
      "zx_thread_read_state partially automated\n"
      "zx_thread_start doesn't need automation\n"
      "zx_thread_write_state partially automated\n"
      "zx_ticks_get doesn't need automation\n"
      "zx_ticks_per_second doesn't need automation\n"
      "zx_timer_cancel doesn't need automation\n"
      "zx_timer_create fully automated\n"
      "zx_timer_set doesn't need automation\n"
      "zx_vcpu_create fully automated\n"
      "zx_vcpu_interrupt doesn't need automation\n"
      "zx_vcpu_read_state partially automated\n"
      "zx_vcpu_resume not automated\n"
      "zx_vcpu_write_state not automated\n"
      "zx_vmar_allocate fully automated\n"
      "zx_vmar_destroy doesn't need automation\n"
      "zx_vmar_map fully automated\n"
      "zx_vmar_protect doesn't need automation\n"
      "zx_vmar_unmap doesn't need automation\n"
      "zx_vmar_unmap_handle_close_thread_exit doesn't need automation\n"
      "zx_vmo_create fully automated\n"
      "zx_vmo_create_child fully automated\n"
      "zx_vmo_create_contiguous fully automated\n"
      "zx_vmo_create_physical fully automated\n"
      "zx_vmo_get_size fully automated\n"
      "zx_vmo_op_range doesn't need automation\n"
      "zx_vmo_read fully automated\n"
      "zx_vmo_replace_as_executable fully automated\n"
      "zx_vmo_set_cache_policy doesn't need automation\n"
      "zx_vmo_set_size doesn't need automation\n"
      "zx_vmo_write fully automated\n";
  uint32_t expected_fully_automated = 66;
  uint32_t expected_cant_be_automated = 59;
  uint32_t expected_partially_automated = 17;
  uint32_t expected_not_automated = 12;
  EXPECT_EQ(actual_fully_automated, expected_fully_automated);
  EXPECT_EQ(actual_cant_be_automated, expected_cant_be_automated);
  EXPECT_EQ(actual_partially_automated, expected_partially_automated);
  EXPECT_EQ(actual_not_automated, expected_not_automated);
  ASSERT_EQ(actual.str(), expected);
}

}  // namespace fidlcat
