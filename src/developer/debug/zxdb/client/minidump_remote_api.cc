// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/minidump_remote_api.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

#include "src/developer/debug/ipc/decode_exception.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/ipc/unwinder_support.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "third_party/crashpad/snapshot/memory_map_region_snapshot.h"

using debug::RegisterCategory;
using debug::RegisterID;

namespace zxdb {

namespace {

class X64ExceptionInfo : public debug_ipc::X64ExceptionInfo {
 public:
  explicit X64ExceptionInfo(const crashpad::ExceptionSnapshot* snapshot) : snapshot_(snapshot) {}

  std::optional<debug_ipc::X64ExceptionInfo::DebugRegs> FetchDebugRegs() const override {
    debug_ipc::X64ExceptionInfo::DebugRegs ret;
    auto context = snapshot_->Context()->x86_64;

    ret.dr0 = context->dr0;
    ret.dr1 = context->dr1;
    ret.dr2 = context->dr2;
    ret.dr3 = context->dr3;
    ret.dr6 = context->dr6;
    ret.dr7 = context->dr7;

    return ret;
  }

 private:
  const crashpad::ExceptionSnapshot* snapshot_;
};

class Arm64ExceptionInfo : public debug_ipc::Arm64ExceptionInfo {
 public:
  explicit Arm64ExceptionInfo(const crashpad::ExceptionSnapshot* snapshot) : snapshot_(snapshot) {}

  std::optional<uint32_t> FetchESR() const override { return snapshot_->ExceptionInfo(); }

 private:
  const crashpad::ExceptionSnapshot* snapshot_;
};

Err ErrNoLive() { return Err(ErrType::kNoConnection, "System is no longer live"); }

Err ErrNoDump() { return Err("Core dump failed to open"); }

Err ErrNoArch() { return Err("Architecture not supported"); }

template <typename ReplyType>
void ErrNoLive(fit::callback<void(const Err&, ReplyType)> cb) {
  debug::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb)]() mutable { cb(ErrNoLive(), ReplyType()); });
}

template <typename ReplyType>
void ErrNoDump(fit::callback<void(const Err&, ReplyType)> cb) {
  debug::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb)]() mutable { cb(ErrNoDump(), ReplyType()); });
}

template <typename ReplyType>
void ErrNoArch(fit::callback<void(const Err&, ReplyType)> cb) {
  debug::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb = std::move(cb)]() mutable { cb(ErrNoArch(), ReplyType()); });
}

template <typename ReplyType>
void Succeed(fit::callback<void(const Err&, ReplyType)> cb, ReplyType r) {
  debug::MessageLoop::Current()->PostTask(FROM_HERE,
                                          [cb = std::move(cb), r]() mutable { cb(Err(), r); });
}

template <typename ValueType>
void AddReg(debug::RegisterID id, const ValueType& value,
            std::vector<debug::RegisterValue>* output) {
  auto& reg = output->emplace_back();
  reg.id = id;
  reg.data.resize(sizeof(ValueType));
  std::memcpy(reg.data.data(), &value, reg.data.size());
}

void PopulateRegistersARM64General(const crashpad::CPUContextARM64& ctx,
                                   std::vector<debug::RegisterValue>* output) {
  AddReg(RegisterID::kARMv8_x0, ctx.regs[0], output);
  AddReg(RegisterID::kARMv8_x1, ctx.regs[1], output);
  AddReg(RegisterID::kARMv8_x2, ctx.regs[2], output);
  AddReg(RegisterID::kARMv8_x3, ctx.regs[3], output);
  AddReg(RegisterID::kARMv8_x4, ctx.regs[4], output);
  AddReg(RegisterID::kARMv8_x5, ctx.regs[5], output);
  AddReg(RegisterID::kARMv8_x6, ctx.regs[6], output);
  AddReg(RegisterID::kARMv8_x7, ctx.regs[7], output);
  AddReg(RegisterID::kARMv8_x8, ctx.regs[8], output);
  AddReg(RegisterID::kARMv8_x9, ctx.regs[9], output);
  AddReg(RegisterID::kARMv8_x10, ctx.regs[10], output);
  AddReg(RegisterID::kARMv8_x11, ctx.regs[11], output);
  AddReg(RegisterID::kARMv8_x12, ctx.regs[12], output);
  AddReg(RegisterID::kARMv8_x13, ctx.regs[13], output);
  AddReg(RegisterID::kARMv8_x14, ctx.regs[14], output);
  AddReg(RegisterID::kARMv8_x15, ctx.regs[15], output);
  AddReg(RegisterID::kARMv8_x16, ctx.regs[16], output);
  AddReg(RegisterID::kARMv8_x17, ctx.regs[17], output);
  AddReg(RegisterID::kARMv8_x18, ctx.regs[18], output);
  AddReg(RegisterID::kARMv8_x19, ctx.regs[19], output);
  AddReg(RegisterID::kARMv8_x20, ctx.regs[20], output);
  AddReg(RegisterID::kARMv8_x21, ctx.regs[21], output);
  AddReg(RegisterID::kARMv8_x22, ctx.regs[22], output);
  AddReg(RegisterID::kARMv8_x23, ctx.regs[23], output);
  AddReg(RegisterID::kARMv8_x24, ctx.regs[24], output);
  AddReg(RegisterID::kARMv8_x25, ctx.regs[25], output);
  AddReg(RegisterID::kARMv8_x26, ctx.regs[26], output);
  AddReg(RegisterID::kARMv8_x27, ctx.regs[27], output);
  AddReg(RegisterID::kARMv8_x28, ctx.regs[28], output);
  AddReg(RegisterID::kARMv8_x29, ctx.regs[29], output);
  AddReg(RegisterID::kARMv8_lr, ctx.regs[30], output);
  AddReg(RegisterID::kARMv8_sp, ctx.sp, output);
  AddReg(RegisterID::kARMv8_pc, ctx.pc, output);
  AddReg(RegisterID::kARMv8_cpsr, ctx.spsr, output);
}

void PopulateRegistersARM64Vector(const crashpad::CPUContextARM64& ctx,
                                  std::vector<debug::RegisterValue>* output) {
  AddReg(RegisterID::kARMv8_fpcr, ctx.fpcr, output);
  AddReg(RegisterID::kARMv8_fpsr, ctx.fpsr, output);
  AddReg(RegisterID::kARMv8_v0, ctx.fpsimd[0], output);
  AddReg(RegisterID::kARMv8_v1, ctx.fpsimd[1], output);
  AddReg(RegisterID::kARMv8_v2, ctx.fpsimd[2], output);
  AddReg(RegisterID::kARMv8_v3, ctx.fpsimd[3], output);
  AddReg(RegisterID::kARMv8_v4, ctx.fpsimd[4], output);
  AddReg(RegisterID::kARMv8_v5, ctx.fpsimd[5], output);
  AddReg(RegisterID::kARMv8_v6, ctx.fpsimd[6], output);
  AddReg(RegisterID::kARMv8_v7, ctx.fpsimd[7], output);
  AddReg(RegisterID::kARMv8_v8, ctx.fpsimd[8], output);
  AddReg(RegisterID::kARMv8_v9, ctx.fpsimd[9], output);
  AddReg(RegisterID::kARMv8_v10, ctx.fpsimd[10], output);
  AddReg(RegisterID::kARMv8_v11, ctx.fpsimd[11], output);
  AddReg(RegisterID::kARMv8_v12, ctx.fpsimd[12], output);
  AddReg(RegisterID::kARMv8_v13, ctx.fpsimd[13], output);
  AddReg(RegisterID::kARMv8_v14, ctx.fpsimd[14], output);
  AddReg(RegisterID::kARMv8_v15, ctx.fpsimd[15], output);
  AddReg(RegisterID::kARMv8_v16, ctx.fpsimd[16], output);
  AddReg(RegisterID::kARMv8_v17, ctx.fpsimd[17], output);
  AddReg(RegisterID::kARMv8_v18, ctx.fpsimd[18], output);
  AddReg(RegisterID::kARMv8_v19, ctx.fpsimd[19], output);
  AddReg(RegisterID::kARMv8_v20, ctx.fpsimd[20], output);
  AddReg(RegisterID::kARMv8_v21, ctx.fpsimd[21], output);
  AddReg(RegisterID::kARMv8_v22, ctx.fpsimd[22], output);
  AddReg(RegisterID::kARMv8_v23, ctx.fpsimd[23], output);
  AddReg(RegisterID::kARMv8_v24, ctx.fpsimd[24], output);
  AddReg(RegisterID::kARMv8_v25, ctx.fpsimd[25], output);
  AddReg(RegisterID::kARMv8_v26, ctx.fpsimd[26], output);
  AddReg(RegisterID::kARMv8_v27, ctx.fpsimd[27], output);
  AddReg(RegisterID::kARMv8_v28, ctx.fpsimd[28], output);
  AddReg(RegisterID::kARMv8_v29, ctx.fpsimd[29], output);
  AddReg(RegisterID::kARMv8_v30, ctx.fpsimd[30], output);
  AddReg(RegisterID::kARMv8_v31, ctx.fpsimd[31], output);
}

void PopulateRegistersARM64(const crashpad::CPUContextARM64& ctx,
                            const debug_ipc::ReadRegistersRequest& request,
                            debug_ipc::ReadRegistersReply* reply) {
  for (const RegisterCategory cat : request.categories) {
    if (cat == RegisterCategory::kGeneral)
      PopulateRegistersARM64General(ctx, &reply->registers);
    if (cat == RegisterCategory::kVector)
      PopulateRegistersARM64Vector(ctx, &reply->registers);
  }
}

void PopulateRegistersX64General(const crashpad::CPUContextX86_64& ctx,
                                 std::vector<debug::RegisterValue>* output) {
  AddReg(RegisterID::kX64_rax, ctx.rax, output);
  AddReg(RegisterID::kX64_rbx, ctx.rbx, output);
  AddReg(RegisterID::kX64_rcx, ctx.rcx, output);
  AddReg(RegisterID::kX64_rdx, ctx.rdx, output);
  AddReg(RegisterID::kX64_rsi, ctx.rsi, output);
  AddReg(RegisterID::kX64_rdi, ctx.rdi, output);
  AddReg(RegisterID::kX64_rbp, ctx.rbp, output);
  AddReg(RegisterID::kX64_rsp, ctx.rsp, output);
  AddReg(RegisterID::kX64_r8, ctx.r8, output);
  AddReg(RegisterID::kX64_r9, ctx.r9, output);
  AddReg(RegisterID::kX64_r10, ctx.r10, output);
  AddReg(RegisterID::kX64_r11, ctx.r11, output);
  AddReg(RegisterID::kX64_r12, ctx.r12, output);
  AddReg(RegisterID::kX64_r13, ctx.r13, output);
  AddReg(RegisterID::kX64_r14, ctx.r14, output);
  AddReg(RegisterID::kX64_r15, ctx.r15, output);
  AddReg(RegisterID::kX64_rip, ctx.rip, output);
  AddReg(RegisterID::kX64_rflags, ctx.rflags, output);
}

void PopulateRegistersX64Float(const crashpad::CPUContextX86_64& ctx,
                               std::vector<debug::RegisterValue>* output) {
  AddReg(RegisterID::kX64_fcw, ctx.fxsave.fcw, output);
  AddReg(RegisterID::kX64_fsw, ctx.fxsave.fsw, output);
  AddReg(RegisterID::kX64_ftw, ctx.fxsave.ftw, output);
  AddReg(RegisterID::kX64_fop, ctx.fxsave.fop, output);
  AddReg(RegisterID::kX64_fip, ctx.fxsave.fpu_ip_64, output);
  AddReg(RegisterID::kX64_fdp, ctx.fxsave.fpu_dp_64, output);
  AddReg(RegisterID::kX64_st0, ctx.fxsave.st_mm[0], output);
  AddReg(RegisterID::kX64_st1, ctx.fxsave.st_mm[1], output);
  AddReg(RegisterID::kX64_st2, ctx.fxsave.st_mm[2], output);
  AddReg(RegisterID::kX64_st3, ctx.fxsave.st_mm[3], output);
  AddReg(RegisterID::kX64_st4, ctx.fxsave.st_mm[4], output);
  AddReg(RegisterID::kX64_st5, ctx.fxsave.st_mm[5], output);
  AddReg(RegisterID::kX64_st6, ctx.fxsave.st_mm[6], output);
  AddReg(RegisterID::kX64_st7, ctx.fxsave.st_mm[7], output);
}

void PopulateRegistersX64Vector(const crashpad::CPUContextX86_64& ctx,
                                std::vector<debug::RegisterValue>* output) {
  AddReg(RegisterID::kX64_mxcsr, ctx.fxsave.mxcsr, output);
  AddReg(RegisterID::kX64_xmm0, ctx.fxsave.xmm[0], output);
  AddReg(RegisterID::kX64_xmm1, ctx.fxsave.xmm[1], output);
  AddReg(RegisterID::kX64_xmm2, ctx.fxsave.xmm[2], output);
  AddReg(RegisterID::kX64_xmm3, ctx.fxsave.xmm[3], output);
  AddReg(RegisterID::kX64_xmm4, ctx.fxsave.xmm[4], output);
  AddReg(RegisterID::kX64_xmm5, ctx.fxsave.xmm[5], output);
  AddReg(RegisterID::kX64_xmm6, ctx.fxsave.xmm[6], output);
  AddReg(RegisterID::kX64_xmm7, ctx.fxsave.xmm[7], output);
  AddReg(RegisterID::kX64_xmm8, ctx.fxsave.xmm[8], output);
  AddReg(RegisterID::kX64_xmm9, ctx.fxsave.xmm[9], output);
  AddReg(RegisterID::kX64_xmm10, ctx.fxsave.xmm[10], output);
  AddReg(RegisterID::kX64_xmm11, ctx.fxsave.xmm[11], output);
  AddReg(RegisterID::kX64_xmm12, ctx.fxsave.xmm[12], output);
  AddReg(RegisterID::kX64_xmm13, ctx.fxsave.xmm[13], output);
  AddReg(RegisterID::kX64_xmm14, ctx.fxsave.xmm[14], output);
  AddReg(RegisterID::kX64_xmm15, ctx.fxsave.xmm[15], output);
}

void PopulateRegistersX64Debug(const crashpad::CPUContextX86_64& ctx,
                               std::vector<debug::RegisterValue>* output) {
  AddReg(RegisterID::kX64_dr0, ctx.dr0, output);
  AddReg(RegisterID::kX64_dr1, ctx.dr1, output);
  AddReg(RegisterID::kX64_dr2, ctx.dr2, output);
  AddReg(RegisterID::kX64_dr3, ctx.dr3, output);
  AddReg(RegisterID::kX64_dr6, ctx.dr6, output);
  AddReg(RegisterID::kX64_dr7, ctx.dr7, output);
}

void PopulateRegistersX86_64(const crashpad::CPUContextX86_64& ctx,
                             const debug_ipc::ReadRegistersRequest& request,
                             debug_ipc::ReadRegistersReply* reply) {
  for (const RegisterCategory cat : request.categories) {
    if (cat == RegisterCategory::kGeneral)
      PopulateRegistersX64General(ctx, &reply->registers);
    if (cat == RegisterCategory::kFloatingPoint)
      PopulateRegistersX64Float(ctx, &reply->registers);
    if (cat == RegisterCategory::kVector)
      PopulateRegistersX64Vector(ctx, &reply->registers);
    if (cat == RegisterCategory::kDebug)
      PopulateRegistersX64Debug(ctx, &reply->registers);
  }
}

}  // namespace

MinidumpRemoteAPI::MinidumpRemoteAPI(Session* session) : session_(session) {
  session_->AddDownloadObserver(this);
}

MinidumpRemoteAPI::~MinidumpRemoteAPI() { session_->RemoveDownloadObserver(this); }

std::string MinidumpRemoteAPI::ProcessName() {
  if (!minidump_) {
    return std::string();
  }

  auto mods = minidump_->Modules();

  if (mods.empty()) {
    return "<core dump>";
  }

  return mods[0]->Name();
}

std::vector<debug_ipc::Module> MinidumpRemoteAPI::GetModules() {
  if (!minidump_) {
    return {};
  }

  std::vector<debug_ipc::Module> ret;

  for (const auto& minidump_mod : minidump_->Modules()) {
    auto& mod = ret.emplace_back();
    mod.name = minidump_mod->Name();
    mod.base = minidump_mod->Address();

    mod.build_id = MinidumpGetBuildId(*minidump_mod);
  }

  return ret;
}

const crashpad::ThreadSnapshot* MinidumpRemoteAPI::GetThreadById(uint64_t id) {
  for (const auto& item : minidump_->Threads()) {
    if (item->ThreadID() == id) {
      return item;
    }
  }

  return nullptr;
}

void MinidumpRemoteAPI::CollectMemory() {
  memory_ = std::make_unique<MinidumpMemory>(*minidump_,
                                             session_->system().GetSymbols()->build_id_index());
}

void MinidumpRemoteAPI::OnDownloadsStopped(size_t num_succeeded, size_t num_failed) {
  // If we just downloaded new binary files, more memory information might be available than when we
  // last collected memory.
  if (minidump_) {
    CollectMemory();
  }
}

Err MinidumpRemoteAPI::Open(const std::string& path) {
  crashpad::FileReader reader;

  if (minidump_) {
    return Err("Dump already open");
  }

  if (!reader.Open(base::FilePath(path))) {
    return Err(fxl::StringPrintf("Could not open %s", path.c_str()));
  }

  minidump_ = std::make_unique<crashpad::ProcessSnapshotMinidump>();
  bool success = minidump_->Initialize(&reader);
  reader.Close();

  if (!success) {
    minidump_.reset();
    return Err(fxl::StringPrintf("Minidump %s not valid", path.c_str()));
  }

  CollectMemory();

  return Err();
}

Err MinidumpRemoteAPI::Close() {
  if (!minidump_) {
    return Err("No open dump to close");
  }

  memory_.reset();
  minidump_.reset();
  return Err();
}

void MinidumpRemoteAPI::Hello(const debug_ipc::HelloRequest& request,
                              fit::callback<void(const Err&, debug_ipc::HelloReply)> cb) {
  if (!minidump_) {
    ErrNoDump(std::move(cb));
    return;
  }

  debug_ipc::HelloReply reply;

  const auto& threads = minidump_->Threads();
  if (threads.empty()) {
    Succeed(std::move(cb), reply);
    return;
  }

  const auto& context = *threads[0]->Context();

  switch (context.architecture) {
    case crashpad::CPUArchitecture::kCPUArchitectureARM64:
      reply.arch = debug::Arch::kArm64;
      break;
    case crashpad::CPUArchitecture::kCPUArchitectureX86_64:
      reply.arch = debug::Arch::kX64;
      break;
    default:
      break;
  }

  // Assume 4K page size since minidumps don't include this information.
  reply.page_size = 4096;

  Succeed(std::move(cb), reply);
}

void MinidumpRemoteAPI::Launch(const debug_ipc::LaunchRequest& request,
                               fit::callback<void(const Err&, debug_ipc::LaunchReply)> cb) {
  ErrNoLive(std::move(cb));
}

void MinidumpRemoteAPI::Kill(const debug_ipc::KillRequest& request,
                             fit::callback<void(const Err&, debug_ipc::KillReply)> cb) {
  ErrNoLive(std::move(cb));
}

void MinidumpRemoteAPI::Attach(const debug_ipc::AttachRequest& request,
                               fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) {
  if (!minidump_) {
    ErrNoDump(std::move(cb));
    return;
  }

  debug_ipc::AttachReply reply;
  reply.name = ProcessName();

  if (static_cast<pid_t>(request.koid) != minidump_->ProcessID()) {
    reply.status = debug::Status("Process " + std::to_string(request.koid) +
                                 " is not in this minidump, there is only " +
                                 std::to_string(minidump_->ProcessID()));
    Succeed(std::move(cb), reply);
    return;
  }

  reply.status = debug::Status();
  attached_ = true;

  std::vector<debug_ipc::NotifyThreadStarting> notifications;

  for (const auto& thread : minidump_->Threads()) {
    auto& notification = notifications.emplace_back();

    notification.record.id.process = minidump_->ProcessID();
    notification.record.id.thread = thread->ThreadID();
    notification.record.state = debug_ipc::ThreadRecord::State::kCoreDump;
  }

  Session* session = session_;
  debug_ipc::NotifyModules mod_notification;
  debug_ipc::NotifyException exception_notification;

  mod_notification.process_koid = minidump_->ProcessID();
  mod_notification.modules = GetModules();

  if (auto exception = minidump_->Exception()) {
    switch (exception->Context()->architecture) {
      case crashpad::CPUArchitecture::kCPUArchitectureARM64: {
        Arm64ExceptionInfo info(exception);
        exception_notification.type = debug_ipc::DecodeException(exception->Exception(), info);

        // The |codes| vector is populated in this order:
        //  [0] = zircon exception (this is the same as |ExceptionSnapshot.Exception()|)
        //  [1] = zircon err_code (this is the same as |ExceptionSnapshot.ExceptionInfo()|, in the
        //        case of arm64 this is also equivalent to the esr register)
        //  [2] = arm64 far register
        exception_notification.exception.arch.arm64.esr = exception->ExceptionInfo();
        exception_notification.exception.arch.arm64.far = exception->Codes()[2];
        exception_notification.exception.valid = true;

        break;
      }
      case crashpad::CPUArchitecture::kCPUArchitectureX86_64: {
        X64ExceptionInfo info(exception);
        exception_notification.type = debug_ipc::DecodeException(exception->Exception(), info);

        // The |codes| vector is populated in this order:
        //  [0] = zircon exception (this is the same as |ExceptionSnapshot.Exception()|)
        //  [1] = zircon err_code (this is the same as |ExceptionSnapshot.ExceptionInfo()|)
        //  [2] = x64 exception vector
        //  [3] = x64 cr2
        exception_notification.exception.arch.x64.err_code = exception->ExceptionInfo();
        exception_notification.exception.arch.x64.vector = exception->Codes()[2];
        exception_notification.exception.arch.x64.cr2 = exception->Codes()[3];
        exception_notification.exception.valid = true;

        break;
      }
      default:
        exception_notification.type = debug_ipc::ExceptionType::kUnknown;
        break;
    }

    exception_notification.thread.id.process = minidump_->ProcessID();
    exception_notification.thread.id.thread = exception->ThreadID();
    exception_notification.thread.state = debug_ipc::ThreadRecord::State::kCoreDump;
  }

  fit::callback<void(const Err&, debug_ipc::AttachReply)> new_cb =
      [cb = std::move(cb), notifications, mod_notification, exception_notification, session](
          const Err& e, debug_ipc::AttachReply a) mutable {
        cb(e, std::move(a));

        for (const auto& notification : notifications) {
          session->DispatchNotifyThreadStarting(notification);
        }

        session->DispatchNotifyModules(mod_notification);

        if (exception_notification.type != debug_ipc::ExceptionType::kNone) {
          session->DispatchNotifyException(exception_notification);
        }
      };

  Succeed(std::move(new_cb), reply);
}

void MinidumpRemoteAPI::Detach(const debug_ipc::DetachRequest& request,
                               fit::callback<void(const Err&, debug_ipc::DetachReply)> cb) {
  if (!minidump_) {
    ErrNoDump(std::move(cb));
    return;
  }

  debug_ipc::DetachReply reply;

  if (static_cast<pid_t>(request.koid) == minidump_->ProcessID() && attached_) {
    reply.status = debug::Status();
    attached_ = false;
  } else {
    reply.status = debug::Status("Process not found in this minidump.");
  }

  Succeed(std::move(cb), reply);
}

void MinidumpRemoteAPI::Modules(const debug_ipc::ModulesRequest& request,
                                fit::callback<void(const Err&, debug_ipc::ModulesReply)> cb) {
  if (!minidump_) {
    ErrNoDump(std::move(cb));
    return;
  }

  debug_ipc::ModulesReply reply;

  if (static_cast<pid_t>(request.process_koid) != minidump_->ProcessID()) {
    Succeed(std::move(cb), reply);
    return;
  }

  reply.modules = GetModules();

  Succeed(std::move(cb), reply);
}

void MinidumpRemoteAPI::Pause(const debug_ipc::PauseRequest& request,
                              fit::callback<void(const Err&, debug_ipc::PauseReply)> cb) {
  ErrNoLive(std::move(cb));
}

void MinidumpRemoteAPI::Resume(const debug_ipc::ResumeRequest& request,
                               fit::callback<void(const Err&, debug_ipc::ResumeReply)> cb) {
  ErrNoLive(std::move(cb));
}

void MinidumpRemoteAPI::ProcessTree(
    const debug_ipc::ProcessTreeRequest& request,
    fit::callback<void(const Err&, debug_ipc::ProcessTreeReply)> cb) {
  if (!minidump_) {
    ErrNoDump(std::move(cb));
    return;
  }

  debug_ipc::ProcessTreeRecord record;

  record.type = debug_ipc::ProcessTreeRecord::Type::kProcess;
  record.name = ProcessName();
  record.koid = minidump_->ProcessID();

  debug_ipc::ProcessTreeReply reply{
      .root = record,
  };

  Succeed(std::move(cb), reply);
}

void MinidumpRemoteAPI::Threads(const debug_ipc::ThreadsRequest& request,
                                fit::callback<void(const Err&, debug_ipc::ThreadsReply)> cb) {
  if (!minidump_) {
    ErrNoDump(std::move(cb));
    return;
  }

  debug_ipc::ThreadsReply reply;

  if (static_cast<pid_t>(request.process_koid) == minidump_->ProcessID()) {
    for (const auto& thread : minidump_->Threads()) {
      auto& record = reply.threads.emplace_back();

      record.id.process = request.process_koid;
      record.id.thread = thread->ThreadID();
      record.state = debug_ipc::ThreadRecord::State::kCoreDump;
    }
  }

  Succeed(std::move(cb), reply);
}

void MinidumpRemoteAPI::ReadMemory(const debug_ipc::ReadMemoryRequest& request,
                                   fit::callback<void(const Err&, debug_ipc::ReadMemoryReply)> cb) {
  if (!minidump_) {
    ErrNoDump(std::move(cb));
    return;
  }

  debug_ipc::ReadMemoryReply reply;

  if (static_cast<pid_t>(request.process_koid) == minidump_->ProcessID()) {
    reply.blocks = memory_->ReadMemoryBlocks(request.address, request.size);
  } else {
    auto block = reply.blocks.emplace_back();
    reply.blocks.back().address = request.address;
    reply.blocks.back().valid = false;
    reply.blocks.back().size = request.size;
  }

  Succeed(std::move(cb), reply);
}

void MinidumpRemoteAPI::ReadRegisters(
    const debug_ipc::ReadRegistersRequest& request,
    fit::callback<void(const Err&, debug_ipc::ReadRegistersReply)> cb) {
  if (!minidump_) {
    ErrNoDump(std::move(cb));
    return;
  }

  debug_ipc::ReadRegistersReply reply;

  if (static_cast<pid_t>(request.id.process) != minidump_->ProcessID()) {
    Succeed(std::move(cb), reply);
    return;
  }

  const crashpad::ThreadSnapshot* thread = GetThreadById(request.id.thread);

  if (thread == nullptr) {
    Succeed(std::move(cb), reply);
    return;
  }

  const auto& context = *thread->Context();

  switch (context.architecture) {
    case crashpad::CPUArchitecture::kCPUArchitectureARM64:
      PopulateRegistersARM64(*context.arm64, request, &reply);
      break;
    case crashpad::CPUArchitecture::kCPUArchitectureX86_64:
      PopulateRegistersX86_64(*context.x86_64, request, &reply);
      break;
    default:
      ErrNoArch(std::move(cb));
      return;
  }

  Succeed(std::move(cb), reply);
}

void MinidumpRemoteAPI::AddOrChangeBreakpoint(
    const debug_ipc::AddOrChangeBreakpointRequest& request,
    fit::callback<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) {
  ErrNoLive(std::move(cb));
}

void MinidumpRemoteAPI::RemoveBreakpoint(
    const debug_ipc::RemoveBreakpointRequest& request,
    fit::callback<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb) {
  ErrNoLive(std::move(cb));
}

void MinidumpRemoteAPI::SysInfo(const debug_ipc::SysInfoRequest& request,
                                fit::callback<void(const Err&, debug_ipc::SysInfoReply)> cb) {
  if (!minidump_) {
    ErrNoDump(std::move(cb));
    return;
  }

  debug_ipc::SysInfoReply reply;
  reply.version = minidump_->System()->OSVersionFull();
  reply.num_cpus = minidump_->System()->CPUCount();
  reply.memory_mb = 0;
  reply.hw_breakpoint_count = 0;
  reply.hw_watchpoint_count = 0;
  Succeed(std::move(cb), reply);
}

void MinidumpRemoteAPI::ThreadStatus(
    const debug_ipc::ThreadStatusRequest& request,
    fit::callback<void(const Err&, debug_ipc::ThreadStatusReply)> cb) {
  if (!minidump_) {
    ErrNoDump(std::move(cb));
    return;
  }

  debug_ipc::ThreadStatusReply reply;

  if (static_cast<pid_t>(request.id.process) != minidump_->ProcessID()) {
    Succeed(std::move(cb), reply);
    return;
  }

  const crashpad::ThreadSnapshot* thread = GetThreadById(request.id.thread);

  if (thread == nullptr) {
    Succeed(std::move(cb), reply);
    return;
  }

  reply.record.id = request.id;
  reply.record.state = debug_ipc::ThreadRecord::State::kCoreDump;
  reply.record.stack_amount = debug_ipc::ThreadRecord::StackAmount::kFull;

  unwinder::Memory* stack_memory = nullptr;
  if (auto stack = thread->Stack()) {
    stack_memory = memory_->GetMemoryRegion(stack->Address());
  }

  const auto& context = *thread->Context();
  unwinder::Registers regs(unwinder::Registers::Arch::kArm64);

  switch (context.architecture) {
    case crashpad::CPUArchitecture::kCPUArchitectureARM64:
      for (int i = 0; i < static_cast<int>(unwinder::RegisterID::kArm64_last); i++) {
        regs.Set(static_cast<unwinder::RegisterID>(i),
                 reinterpret_cast<uint64_t*>(context.arm64)[i]);
      }
      break;
    case crashpad::CPUArchitecture::kCPUArchitectureX86_64:
      regs = unwinder::Registers(unwinder::Registers::Arch::kX64);
      // The first 6 registers are out of order.
      regs.Set(unwinder::RegisterID::kX64_rax, context.x86_64->rax);
      regs.Set(unwinder::RegisterID::kX64_rbx, context.x86_64->rbx);
      regs.Set(unwinder::RegisterID::kX64_rcx, context.x86_64->rcx);
      regs.Set(unwinder::RegisterID::kX64_rdx, context.x86_64->rdx);
      regs.Set(unwinder::RegisterID::kX64_rdi, context.x86_64->rdi);
      regs.Set(unwinder::RegisterID::kX64_rsi, context.x86_64->rsi);
      for (int i = 6; i < static_cast<int>(unwinder::RegisterID::kX64_last); i++) {
        regs.Set(static_cast<unwinder::RegisterID>(i),
                 reinterpret_cast<uint64_t*>(context.x86_64)[i]);
      }
      break;
    default:
      ErrNoArch(std::move(cb));
      return;
  }

  // TODO(dangyi): consider having a new unwinder interface so that the index of .debug_frame could
  // be cached.
  auto frames = unwinder::Unwind(stack_memory, memory_->GetDebugModuleMap(), regs);
  reply.record.frames = debug_ipc::ConvertFrames(frames);
  Succeed(std::move(cb), reply);
}

void MinidumpRemoteAPI::AddressSpace(
    const debug_ipc::AddressSpaceRequest& request,
    fit::callback<void(const Err&, debug_ipc::AddressSpaceReply)> cb) {
  if (!minidump_) {
    ErrNoDump(std::move(cb));
    return;
  }

  debug_ipc::AddressSpaceReply reply;

  if (static_cast<pid_t>(request.process_koid) == minidump_->ProcessID()) {
    for (const auto& region_object : minidump_->MemoryMap()) {
      const auto& region = region_object->AsMinidumpMemoryInfo();

      if (request.address > 0 && (request.address < region.BaseAddress ||
                                  request.address >= region.BaseAddress + region.RegionSize)) {
        continue;
      }

      auto& record = reply.map.emplace_back();

      record.base = region.BaseAddress;
      record.size = region.RegionSize;
    }
  }

  Succeed(std::move(cb), reply);
}

void MinidumpRemoteAPI::UpdateFilter(
    const debug_ipc::UpdateFilterRequest& request,
    fit::callback<void(const Err&, debug_ipc::UpdateFilterReply)> cb) {
  ErrNoLive(std::move(cb));
}

void MinidumpRemoteAPI::WriteMemory(
    const debug_ipc::WriteMemoryRequest& request,
    fit::callback<void(const Err&, debug_ipc::WriteMemoryReply)> cb) {
  ErrNoLive(std::move(cb));
}

void MinidumpRemoteAPI::SaveMinidump(
    const debug_ipc::SaveMinidumpRequest& request,
    fit::callback<void(const Err&, debug_ipc::SaveMinidumpReply)> cb) {
  ErrNoLive(std::move(cb));
}

}  // namespace zxdb
