// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "interception_workflow_test.h"

#include "gtest/gtest.h"

namespace fidlcat {

// We only test one syscall at a time. We always use the same address for all the syscalls.
constexpr uint64_t kSyscallAddress = 0x100060;

static debug_ipc::RegisterID aarch64_regs[] = {
    debug_ipc::RegisterID::kARMv8_x0, debug_ipc::RegisterID::kARMv8_x1,
    debug_ipc::RegisterID::kARMv8_x2, debug_ipc::RegisterID::kARMv8_x3,
    debug_ipc::RegisterID::kARMv8_x4, debug_ipc::RegisterID::kARMv8_x5,
    debug_ipc::RegisterID::kARMv8_x6, debug_ipc::RegisterID::kARMv8_x7};
constexpr size_t aarch64_regs_count = sizeof(aarch64_regs) / sizeof(debug_ipc::RegisterID);

static debug_ipc::RegisterID amd64_regs[] = {
    debug_ipc::RegisterID::kX64_rdi, debug_ipc::RegisterID::kX64_rsi,
    debug_ipc::RegisterID::kX64_rdx, debug_ipc::RegisterID::kX64_rcx,
    debug_ipc::RegisterID::kX64_r8,  debug_ipc::RegisterID::kX64_r9};
constexpr size_t amd64_regs_count = sizeof(amd64_regs) / sizeof(debug_ipc::RegisterID);

DataForSyscallTest::DataForSyscallTest(debug_ipc::Arch arch) : arch_(arch) {
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
  header2_.txid = kTxId2;
  header2_.reserved0 = kReserved;
  header2_.ordinal = kOrdinal2;

  sp_ = stack_ + kMaxStackSizeInWords;
}

void InterceptionWorkflowTest::PerformCheckTest(const char* syscall_name,
                                                std::unique_ptr<SystemCallTest> syscall1,
                                                std::unique_ptr<SystemCallTest> syscall2) {
  ProcessController controller(this, session(), loop());

  PerformTest(syscall_name, std::move(syscall1), std::move(syscall2), &controller,
              std::make_unique<SyscallDecoderDispatcherTest>(decode_options_, &controller),
              /*interleaved_test=*/false);
}

void InterceptionWorkflowTest::PerformDisplayTest(const char* syscall_name,
                                                  std::unique_ptr<SystemCallTest> syscall,
                                                  const char* expected) {
  ProcessController controller(this, session(), loop());

  PerformTest(syscall_name, std::move(syscall), nullptr, &controller,
              std::make_unique<SyscallDisplayDispatcherTest>(
                  nullptr, decode_options_, display_options_, result_, &controller),
              /*interleaved_test=*/false);
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

void InterceptionWorkflowTest::PerformInterleavedDisplayTest(
    const char* syscall_name, std::unique_ptr<SystemCallTest> syscall, const char* expected) {
  ProcessController controller(this, session(), loop());

  PerformTest(syscall_name, std::move(syscall), nullptr, &controller,
              std::make_unique<SyscallDisplayDispatcherTest>(
                  nullptr, decode_options_, display_options_, result_, &controller),
              /*interleaved_test=*/true);
  ASSERT_EQ(expected, result_.str());
}

void InterceptionWorkflowTest::PerformTest(const char* syscall_name,
                                           std::unique_ptr<SystemCallTest> syscall1,
                                           std::unique_ptr<SystemCallTest> syscall2,
                                           ProcessController* controller,
                                           std::unique_ptr<SyscallDecoderDispatcher> dispatcher,
                                           bool interleaved_test) {
  controller->Initialize(session(), std::move(dispatcher), syscall_name);

  SimulateSyscall(std::move(syscall1), controller, interleaved_test);

  debug_ipc::MessageLoop::Current()->Run();

  if (syscall2 != nullptr) {
    data_.set_use_alternate_data();
    SimulateSyscall(std::move(syscall2), controller, interleaved_test);
  }
}

void InterceptionWorkflowTest::SimulateSyscall(std::unique_ptr<SystemCallTest> syscall,
                                               ProcessController* controller,
                                               bool interleaved_test) {
  data_.set_syscall(std::move(syscall));
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
}

void InterceptionWorkflowTest::TriggerSyscallBreakpoint(uint64_t process_koid,
                                                        uint64_t thread_koid) {
  // Trigger breakpoint.
  debug_ipc::NotifyException notification;
  notification.type = debug_ipc::NotifyException::Type::kGeneral;
  notification.thread.process_koid = process_koid;
  notification.thread.thread_koid = thread_koid;
  notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  notification.thread.stack_amount = debug_ipc::ThreadRecord::StackAmount::kMinimal;
  debug_ipc::StackFrame frame(kSyscallAddress, reinterpret_cast<uint64_t>(data_.sp()));
  data_.PopulateRegisters(process_koid, &frame.regs);
  notification.thread.frames.push_back(frame);
  mock_remote_api().PopulateBreakpointIds(kSyscallAddress, notification);
  InjectException(notification);
  debug_ipc::MessageLoop::Current()->Run();
}

void InterceptionWorkflowTest::TriggerCallerBreakpoint(uint64_t process_koid,
                                                       uint64_t thread_koid) {
  // Trigger next breakpoint, when the syscall has completed.
  debug_ipc::NotifyException notification;
  notification.type = debug_ipc::NotifyException::Type::kGeneral;
  notification.thread.process_koid = process_koid;
  notification.thread.thread_koid = thread_koid;
  notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  notification.thread.stack_amount = debug_ipc::ThreadRecord::StackAmount::kMinimal;
  debug_ipc::StackFrame frame(DataForSyscallTest::kReturnAddress,
                              reinterpret_cast<uint64_t>(data_.sp()));
  data_.PopulateRegisters(process_koid, &frame.regs);
  notification.thread.frames.push_back(frame);
  InjectException(notification);

  debug_ipc::MessageLoop::Current()->Run();
}

ProcessController::ProcessController(InterceptionWorkflowTest* remote_api, zxdb::Session& session,
                                     debug_ipc::PlatformMessageLoop& loop)
    : remote_api_(remote_api), workflow_(&session, &loop) {
  process_koids_ = {kFirstPid, kSecondPid};
  thread_koids_[kFirstPid] = kFirstThreadKoid;
  thread_koids_[kSecondPid] = kSecondThreadKoid;
}

ProcessController::~ProcessController() {
  for (zxdb::Process* process : processes_) {
    process->RemoveObserver(&workflow_.observer_.process_observer());
  }
  for (zxdb::Target* target : targets_) {
    target->RemoveObserver(&workflow_.observer_);
  }
}

void ProcessController::InjectProcesses(zxdb::Session& session) {
  for (auto process_koid : process_koids_) {
    zxdb::TargetImpl* found_target = nullptr;
    for (zxdb::TargetImpl* target : session.system_impl().GetTargetImpls()) {
      if (target->GetState() == zxdb::Target::State::kNone && target->GetArgs().empty()) {
        found_target = target;
        break;
      }
    }

    if (!found_target) {  // No empty target, make a new one.
      found_target = session.system_impl().CreateNewTargetImpl(nullptr);
    }

    std::string test_name = "test_" + std::to_string(process_koid);
    found_target->CreateProcessForTesting(process_koid, test_name);
    processes_.push_back(found_target->GetProcess());
  }
}

void ProcessController::Initialize(zxdb::Session& session,
                                   std::unique_ptr<SyscallDecoderDispatcher> dispatcher,
                                   const char* syscall_name) {
  std::vector<std::string> blank;
  workflow_.Initialize(blank, std::move(dispatcher));

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
    workflow_.AddObserver(target);
    workflow_.observer_.DidCreateProcess(target, process, false);
    workflow_.observer_.process_observer().DidCreateThread(process, the_thread);
  }

  // Attach to processes.
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE, [this]() {
    workflow_.Attach(process_koids_, [](const zxdb::Err& err, uint64_t process_koid) {
      // Because we are already attached, we don't get here.
      FAIL() << "Should not be reached";
    });
    debug_ipc::MessageLoop::Current()->QuitNow();
  });
  debug_ipc::MessageLoop::Current()->Run();

  // Load modules into program (including the one with the zx_channel_write
  // and zx_channel_read symbols)
  std::unique_ptr<zxdb::MockModuleSymbols> module =
      std::make_unique<zxdb::MockModuleSymbols>("zx.so");
  module->AddSymbolLocations(syscall_name,
                             {zxdb::Location(zxdb::Location::State::kSymbolized, kSyscallAddress)});
  fxl::RefPtr<zxdb::SystemSymbols::ModuleRef> module_ref =
      session.system().GetSymbols()->InjectModuleForTesting(DataForSyscallTest::kElfSymbolBuildID,
                                                            std::move(module));

  for (zxdb::Target* target : session.system().GetTargets()) {
    zxdb::Err err;
    std::vector<debug_ipc::Module> modules;
    // Force system to load modules.  Callback doesn't need to do anything
    // interesting.
    target->GetProcess()->GetModules([](const zxdb::Err&, std::vector<debug_ipc::Module>) {
      debug_ipc::MessageLoop::Current()->QuitNow();
    });
    debug_ipc::MessageLoop::Current()->Run();
  }
}

void ProcessController::Detach() { workflow_.Detach(); }

}  // namespace fidlcat
