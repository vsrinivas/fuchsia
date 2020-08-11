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

void InterceptionWorkflowTest::PerformCheckTest(const char* syscall_name,
                                                std::unique_ptr<SystemCallTest> syscall1,
                                                std::unique_ptr<SystemCallTest> syscall2) {
  ProcessController controller(this, session(), loop());

  PerformTest(syscall_name, std::move(syscall1), std::move(syscall2), &controller,
              std::make_unique<SyscallDecoderDispatcherTest>(decode_options_, &controller),
              /*interleaved_test=*/false, /*multi_thread=*/true);
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
  // The second output starts with "test_2718"
  size_t split = both_results.find("test_2718");
  ASSERT_NE(split, std::string::npos);
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
    debug_ipc::MessageLoop::Current()->Run();
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
  notification->type = debug_ipc::ExceptionType::kSoftwareBreakpoint;
  notification->thread.process_koid = process_koid;
  notification->thread.thread_koid = thread_koid;
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
    debug_ipc::MessageLoop::Current()->Run();
  }
}

void InterceptionWorkflowTest::TriggerCallerBreakpoint(uint64_t process_koid,
                                                       uint64_t thread_koid) {
  // Trigger next breakpoint, when the syscall has completed.
  debug_ipc::NotifyException notification;
  notification.type = debug_ipc::ExceptionType::kSoftwareBreakpoint;
  notification.thread.process_koid = process_koid;
  notification.thread.thread_koid = thread_koid;
  notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  notification.thread.stack_amount = debug_ipc::ThreadRecord::StackAmount::kMinimal;

  debug_ipc::StackFrame frame(DataForSyscallTest::kReturnAddress,
                              reinterpret_cast<uint64_t>(data_.sp()));

  data_.PopulateRegisters(process_koid, &frame.regs);
  notification.thread.frames.push_back(frame);

  mock_remote_api().PopulateBreakpointIds(DataForSyscallTest::kReturnAddress, notification);

  InjectException(notification);

  debug_ipc::MessageLoop::Current()->Run();
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

  debug_ipc::MessageLoop::Current()->Run();
}

void InterceptionWorkflowTest::TriggerException(uint64_t process_koid, uint64_t thread_koid,
                                                debug_ipc::ExceptionType type) {
  // Trigger breakpoint.
  debug_ipc::NotifyException notification;
  notification.type = type;
  notification.thread.process_koid = process_koid;
  notification.thread.thread_koid = thread_koid;
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
                                syscall.second.get());
      break;
    }
  }

  debug_ipc::MessageLoop::Current()->Run();
}

ProcessController::ProcessController(InterceptionWorkflowTest* remote_api, zxdb::Session& session,
                                     debug_ipc::MessageLoop& loop)
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
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [this]() {
    workflow_.Attach(process_koids_);
    debug_ipc::MessageLoop::Current()->QuitNow();
  });
  debug_ipc::MessageLoop::Current()->Run();

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
            debug_ipc::MessageLoop::Current()->QuitNow();
          });
      debug_ipc::MessageLoop::Current()->Run();
    }
  }
}

void ProcessController::Detach() {
  if (++detached_processes_ == processes_.size()) {
    workflow_.Shutdown();
  }
}

// This test keep track of all syscalls which can be printed using a values.
// It will be destroyed when everything will be implemented.
TEST_F(InterceptionWorkflowTestX64, ValuesOK) {
  std::set<std::string> actual;
  SyscallDecoderDispatcherTest dispatcher(decode_options_, nullptr);
  for (const auto& syscall : dispatcher.syscalls()) {
    if (syscall.second->fidl_codec_values_ready()) {
      actual.insert(syscall.second->name());
    }
  }

  std::set<std::string> expected = {"__libc_extensions_init",
                                    "processargs_extract_handles",
                                    "zx_bti_create",
                                    "zx_bti_pin",
                                    "zx_bti_release_quarantine",
                                    "zx_cache_flush",
                                    "zx_channel_call",
                                    "zx_channel_create",
                                    "zx_channel_read",
                                    "zx_channel_read_etc",
                                    "zx_channel_write",
                                    "zx_clock_adjust",
                                    "zx_clock_get",
                                    "zx_clock_get_monotonic",
                                    "zx_cprng_add_entropy",
                                    "zx_cprng_draw",
                                    "zx_deadline_after",
                                    "zx_debug_send_command",
                                    "zx_debug_write",
                                    "zx_debuglog_create",
                                    "zx_debuglog_read",
                                    "zx_debuglog_write",
                                    "zx_event_create",
                                    "zx_eventpair_create",
                                    "zx_exception_get_process",
                                    "zx_exception_get_thread",
                                    "zx_fifo_create",
                                    "zx_framebuffer_get_info",
                                    "zx_framebuffer_set_range",
                                    "zx_guest_create",
                                    "zx_handle_close",
                                    "zx_handle_close_many",
                                    "zx_handle_duplicate",
                                    "zx_handle_replace",
                                    "zx_interrupt_ack",
                                    "zx_interrupt_bind",
                                    "zx_interrupt_bind_vcpu",
                                    "zx_interrupt_destroy",
                                    "zx_ioports_release",
                                    "zx_ioports_request",
                                    "zx_job_create",
                                    "zx_ktrace_write",
                                    "zx_mtrace_control",
                                    "zx_nanosleep",
                                    "zx_object_get_child",
                                    "zx_object_get_property",
                                    "zx_object_set_profile",
                                    "zx_object_set_property",
                                    "zx_object_signal",
                                    "zx_object_signal_peer",
                                    "zx_object_wait_async",
                                    "zx_object_wait_one",
                                    "zx_pager_create",
                                    "zx_pager_create_vmo",
                                    "zx_pager_detach_vmo",
                                    "zx_pager_supply_pages",
                                    "zx_pc_firmware_tables",
                                    "zx_pci_add_subtract_io_range",
                                    "zx_pci_cfg_pio_rw",
                                    "zx_pci_config_read",
                                    "zx_pci_config_write",
                                    "zx_pci_enable_bus_master",
                                    "zx_pci_get_bar",
                                    "zx_pci_get_nth_device",
                                    "zx_pci_init",
                                    "zx_pci_map_interrupt",
                                    "zx_pci_query_irq_mode",
                                    "zx_pci_reset_device",
                                    "zx_pci_set_irq_mode",
                                    "zx_pmt_unpin",
                                    "zx_port_cancel",
                                    "zx_port_create",
                                    "zx_port_queue",
                                    "zx_port_wait",
                                    "zx_process_create",
                                    "zx_process_exit",
                                    "zx_process_read_memory",
                                    "zx_process_start",
                                    "zx_process_write_memory",
                                    "zx_profile_create",
                                    "zx_smc_call",
                                    "zx_system_get_dcache_line_size",
                                    "zx_system_get_num_cpus",
                                    "zx_system_get_physmem",
                                    "zx_system_get_version",
                                    "zx_system_mexec",
                                    "zx_system_mexec_payload_get",
                                    "zx_task_create_exception_channel",
                                    "zx_task_kill",
                                    "zx_task_suspend",
                                    "zx_task_suspend_token",
                                    "zx_thread_create",
                                    "zx_thread_exit",
                                    "zx_thread_start",
                                    "zx_ticks_get",
                                    "zx_ticks_per_second",
                                    "zx_timer_cancel",
                                    "zx_timer_create",
                                    "zx_vcpu_create",
                                    "zx_vcpu_interrupt",
                                    "zx_vcpu_resume",
                                    "zx_vmar_destroy",
                                    "zx_vmar_unmap",
                                    "zx_vmar_unmap_handle_close_thread_exit",
                                    "zx_vmo_create_contiguous",
                                    "zx_vmo_create_physical",
                                    "zx_vmo_get_size",
                                    "zx_vmo_read",
                                    "zx_vmo_replace_as_executable",
                                    "zx_vmo_set_cache_policy",
                                    "zx_vmo_set_size",
                                    "zx_vmo_write"};

  ASSERT_EQ(expected, actual);
}

// This test keep track of all syscalls which are still directly printed.
// It will be destroyed when everything will be implemented.
TEST_F(InterceptionWorkflowTestX64, ValuesNotImplemented) {
  std::set<std::string> actual;
  SyscallDecoderDispatcherTest dispatcher(decode_options_, nullptr);
  for (const auto& syscall : dispatcher.syscalls()) {
    if (!syscall.second->fidl_codec_values_ready()) {
      actual.insert(syscall.second->name());
    }
  }

  std::set<std::string> expected = {"zx_debug_read",
                                    "zx_fifo_read",
                                    "zx_fifo_write",
                                    "zx_futex_get_owner",
                                    "zx_futex_requeue",
                                    "zx_futex_requeue_single_owner",
                                    "zx_futex_wait",
                                    "zx_futex_wake",
                                    "zx_futex_wake_handle_close_thread_exit",
                                    "zx_futex_wake_single_owner",
                                    "zx_guest_set_trap",
                                    "zx_interrupt_create",
                                    "zx_interrupt_trigger",
                                    "zx_interrupt_wait",
                                    "zx_iommu_create",
                                    "zx_job_set_policy",
                                    "zx_ktrace_control",
                                    "zx_ktrace_read",
                                    "zx_object_get_info",
                                    "zx_object_wait_many",
                                    "zx_resource_create",
                                    "zx_socket_create",
                                    "zx_socket_read",
                                    "zx_socket_shutdown",
                                    "zx_socket_write",
                                    "zx_system_get_event",
                                    "zx_system_get_features",
                                    "zx_system_powerctl",
                                    "zx_thread_read_state",
                                    "zx_thread_write_state",
                                    "zx_timer_set",
                                    "zx_vcpu_read_state",
                                    "zx_vcpu_write_state",
                                    "zx_vmar_allocate",
                                    "zx_vmar_map",
                                    "zx_vmar_protect",
                                    "zx_vmo_create",
                                    "zx_vmo_create_child",
                                    "zx_vmo_op_range"};

  ASSERT_EQ(expected, actual);
}

}  // namespace fidlcat
