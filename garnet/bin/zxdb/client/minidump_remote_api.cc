// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/minidump_remote_api.h"

#include <cstring>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"
#include "src/developer/debug/ipc/client_protocol.h"
#include "src/developer/debug/shared/message_loop.h"
#include "third_party/crashpad/snapshot/memory_map_region_snapshot.h"
#include "third_party/crashpad/snapshot/memory_snapshot.h"
#include "third_party/crashpad/snapshot/minidump/process_snapshot_minidump.h"
#include "third_party/crashpad/util/misc/uuid.h"

namespace zxdb {

namespace {

Err ErrNoLive() {
  return Err(ErrType::kNoConnection, "System is no longer live");
}

Err ErrNoImpl() { return Err("Feature not implemented for minidump"); }

Err ErrNoDump() { return Err("Core dump failed to open"); }

Err ErrNoArch() { return Err("Architecture not supported"); }

template <typename ReplyType>
void ErrNoLive(std::function<void(const Err&, ReplyType)> cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb]() { cb(ErrNoLive(), ReplyType()); });
}

template <typename ReplyType>
void ErrNoImpl(std::function<void(const Err&, ReplyType)> cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb]() { cb(ErrNoImpl(), ReplyType()); });
}

template <typename ReplyType>
void ErrNoDump(std::function<void(const Err&, ReplyType)> cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb]() { cb(ErrNoDump(), ReplyType()); });
}

template <typename ReplyType>
void ErrNoArch(std::function<void(const Err&, ReplyType)> cb) {
  debug_ipc::MessageLoop::Current()->PostTask(
      FROM_HERE, [cb]() { cb(ErrNoArch(), ReplyType()); });
}

template <typename ReplyType>
void Succeed(std::function<void(const Err&, ReplyType)> cb, ReplyType r) {
  debug_ipc::MessageLoop::Current()->PostTask(FROM_HERE,
                                              [cb, r]() { cb(Err(), r); });
}

template <typename ValueType>
void AddReg(debug_ipc::RegisterCategory* category, debug_ipc::RegisterID id,
            const ValueType& value) {
  auto& reg = category->registers.emplace_back();
  reg.id = id;
  reg.data.resize(sizeof(ValueType));
  std::memcpy(reg.data.data(), reinterpret_cast<const void*>(&value),
              reg.data.size());
}

template <typename IterType>
debug_ipc::RegisterCategory* MakeCategory(
    IterType& pos, debug_ipc::RegisterCategory::Type type,
    debug_ipc::ReadRegistersReply* reply) {
  if (*pos == type) {
    pos++;
    auto category = &reply->categories.emplace_back();
    category->type = type;
    return category;
  }

  return nullptr;
}

void PopulateRegistersARM64(const crashpad::CPUContextARM64& ctx,
                            const debug_ipc::ReadRegistersRequest& request,
                            debug_ipc::ReadRegistersReply* reply) {
  auto pos = request.categories.begin();

  using R = debug_ipc::RegisterID;

  auto category =
      MakeCategory(pos, debug_ipc::RegisterCategory::Type::kGeneral, reply);
  if (category != nullptr) {
    AddReg(category, R::kARMv8_x0, ctx.regs[0]);
    AddReg(category, R::kARMv8_x1, ctx.regs[1]);
    AddReg(category, R::kARMv8_x2, ctx.regs[2]);
    AddReg(category, R::kARMv8_x3, ctx.regs[3]);
    AddReg(category, R::kARMv8_x4, ctx.regs[4]);
    AddReg(category, R::kARMv8_x5, ctx.regs[5]);
    AddReg(category, R::kARMv8_x6, ctx.regs[6]);
    AddReg(category, R::kARMv8_x7, ctx.regs[7]);
    AddReg(category, R::kARMv8_x8, ctx.regs[8]);
    AddReg(category, R::kARMv8_x9, ctx.regs[9]);
    AddReg(category, R::kARMv8_x10, ctx.regs[10]);
    AddReg(category, R::kARMv8_x11, ctx.regs[11]);
    AddReg(category, R::kARMv8_x12, ctx.regs[12]);
    AddReg(category, R::kARMv8_x13, ctx.regs[13]);
    AddReg(category, R::kARMv8_x14, ctx.regs[14]);
    AddReg(category, R::kARMv8_x15, ctx.regs[15]);
    AddReg(category, R::kARMv8_x16, ctx.regs[16]);
    AddReg(category, R::kARMv8_x17, ctx.regs[17]);
    AddReg(category, R::kARMv8_x18, ctx.regs[18]);
    AddReg(category, R::kARMv8_x19, ctx.regs[19]);
    AddReg(category, R::kARMv8_x20, ctx.regs[20]);
    AddReg(category, R::kARMv8_x21, ctx.regs[21]);
    AddReg(category, R::kARMv8_x22, ctx.regs[22]);
    AddReg(category, R::kARMv8_x23, ctx.regs[23]);
    AddReg(category, R::kARMv8_x24, ctx.regs[24]);
    AddReg(category, R::kARMv8_x25, ctx.regs[25]);
    AddReg(category, R::kARMv8_x26, ctx.regs[26]);
    AddReg(category, R::kARMv8_x27, ctx.regs[27]);
    AddReg(category, R::kARMv8_x28, ctx.regs[28]);
    AddReg(category, R::kARMv8_x29, ctx.regs[29]);
    AddReg(category, R::kARMv8_lr, ctx.regs[30]);
    AddReg(category, R::kARMv8_sp, ctx.sp);
    AddReg(category, R::kARMv8_pc, ctx.pc);
    AddReg(category, R::kARMv8_cpsr, ctx.spsr);
  }

  // ARM doesn't define any registers in this category.
  MakeCategory(pos, debug_ipc::RegisterCategory::Type::kFP, reply);

  category =
      MakeCategory(pos, debug_ipc::RegisterCategory::Type::kVector, reply);
  if (category != nullptr) {
    AddReg(category, R::kARMv8_fpcr, ctx.fpcr);
    AddReg(category, R::kARMv8_fpsr, ctx.fpsr);
    AddReg(category, R::kARMv8_v0, ctx.fpsimd[0]);
    AddReg(category, R::kARMv8_v1, ctx.fpsimd[1]);
    AddReg(category, R::kARMv8_v2, ctx.fpsimd[2]);
    AddReg(category, R::kARMv8_v3, ctx.fpsimd[3]);
    AddReg(category, R::kARMv8_v4, ctx.fpsimd[4]);
    AddReg(category, R::kARMv8_v5, ctx.fpsimd[5]);
    AddReg(category, R::kARMv8_v6, ctx.fpsimd[6]);
    AddReg(category, R::kARMv8_v7, ctx.fpsimd[7]);
    AddReg(category, R::kARMv8_v8, ctx.fpsimd[8]);
    AddReg(category, R::kARMv8_v9, ctx.fpsimd[9]);
    AddReg(category, R::kARMv8_v10, ctx.fpsimd[10]);
    AddReg(category, R::kARMv8_v11, ctx.fpsimd[11]);
    AddReg(category, R::kARMv8_v12, ctx.fpsimd[12]);
    AddReg(category, R::kARMv8_v13, ctx.fpsimd[13]);
    AddReg(category, R::kARMv8_v14, ctx.fpsimd[14]);
    AddReg(category, R::kARMv8_v15, ctx.fpsimd[15]);
    AddReg(category, R::kARMv8_v16, ctx.fpsimd[16]);
    AddReg(category, R::kARMv8_v17, ctx.fpsimd[17]);
    AddReg(category, R::kARMv8_v18, ctx.fpsimd[18]);
    AddReg(category, R::kARMv8_v19, ctx.fpsimd[19]);
    AddReg(category, R::kARMv8_v20, ctx.fpsimd[20]);
    AddReg(category, R::kARMv8_v21, ctx.fpsimd[21]);
    AddReg(category, R::kARMv8_v22, ctx.fpsimd[22]);
    AddReg(category, R::kARMv8_v23, ctx.fpsimd[23]);
    AddReg(category, R::kARMv8_v24, ctx.fpsimd[24]);
    AddReg(category, R::kARMv8_v25, ctx.fpsimd[25]);
    AddReg(category, R::kARMv8_v26, ctx.fpsimd[26]);
    AddReg(category, R::kARMv8_v27, ctx.fpsimd[27]);
    AddReg(category, R::kARMv8_v28, ctx.fpsimd[28]);
    AddReg(category, R::kARMv8_v29, ctx.fpsimd[29]);
    AddReg(category, R::kARMv8_v30, ctx.fpsimd[30]);
    AddReg(category, R::kARMv8_v31, ctx.fpsimd[31]);
  }

  // ARM Doesn't define any registers in this category either.
  MakeCategory(pos, debug_ipc::RegisterCategory::Type::kDebug, reply);
}

void PopulateRegistersX86_64(const crashpad::CPUContextX86_64& ctx,
                             const debug_ipc::ReadRegistersRequest& request,
                             debug_ipc::ReadRegistersReply* reply) {
  auto pos = request.categories.begin();

  using R = debug_ipc::RegisterID;

  auto category =
      MakeCategory(pos, debug_ipc::RegisterCategory::Type::kGeneral, reply);
  if (category != nullptr) {
    AddReg(category, R::kX64_rax, ctx.rax);
    AddReg(category, R::kX64_rbx, ctx.rbx);
    AddReg(category, R::kX64_rcx, ctx.rcx);
    AddReg(category, R::kX64_rdx, ctx.rdx);
    AddReg(category, R::kX64_rsi, ctx.rsi);
    AddReg(category, R::kX64_rdi, ctx.rdi);
    AddReg(category, R::kX64_rbp, ctx.rbp);
    AddReg(category, R::kX64_rsp, ctx.rsp);
    AddReg(category, R::kX64_r8, ctx.r8);
    AddReg(category, R::kX64_r9, ctx.r9);
    AddReg(category, R::kX64_r10, ctx.r10);
    AddReg(category, R::kX64_r11, ctx.r11);
    AddReg(category, R::kX64_r12, ctx.r12);
    AddReg(category, R::kX64_r13, ctx.r13);
    AddReg(category, R::kX64_r14, ctx.r14);
    AddReg(category, R::kX64_r15, ctx.r15);
    AddReg(category, R::kX64_rip, ctx.rip);
    AddReg(category, R::kX64_rflags, ctx.rflags);
  }

  category = MakeCategory(pos, debug_ipc::RegisterCategory::Type::kFP, reply);
  if (category != nullptr) {
    AddReg(category, R::kX64_fcw, ctx.fxsave.fcw);
    AddReg(category, R::kX64_fsw, ctx.fxsave.fsw);
    AddReg(category, R::kX64_ftw, ctx.fxsave.ftw);
    AddReg(category, R::kX64_fop, ctx.fxsave.fop);
    AddReg(category, R::kX64_fip, ctx.fxsave.fpu_ip_64);
    AddReg(category, R::kX64_fdp, ctx.fxsave.fpu_dp_64);
    AddReg(category, R::kX64_st0, ctx.fxsave.st_mm[0]);
    AddReg(category, R::kX64_st1, ctx.fxsave.st_mm[1]);
    AddReg(category, R::kX64_st2, ctx.fxsave.st_mm[2]);
    AddReg(category, R::kX64_st3, ctx.fxsave.st_mm[3]);
    AddReg(category, R::kX64_st4, ctx.fxsave.st_mm[4]);
    AddReg(category, R::kX64_st5, ctx.fxsave.st_mm[5]);
    AddReg(category, R::kX64_st6, ctx.fxsave.st_mm[6]);
    AddReg(category, R::kX64_st7, ctx.fxsave.st_mm[7]);
  }

  category =
      MakeCategory(pos, debug_ipc::RegisterCategory::Type::kVector, reply);
  if (category != nullptr) {
    AddReg(category, R::kX64_mxcsr, ctx.fxsave.mxcsr);
    AddReg(category, R::kX64_xmm0, ctx.fxsave.xmm[0]);
    AddReg(category, R::kX64_xmm1, ctx.fxsave.xmm[1]);
    AddReg(category, R::kX64_xmm2, ctx.fxsave.xmm[2]);
    AddReg(category, R::kX64_xmm3, ctx.fxsave.xmm[3]);
    AddReg(category, R::kX64_xmm4, ctx.fxsave.xmm[4]);
    AddReg(category, R::kX64_xmm5, ctx.fxsave.xmm[5]);
    AddReg(category, R::kX64_xmm6, ctx.fxsave.xmm[6]);
    AddReg(category, R::kX64_xmm7, ctx.fxsave.xmm[7]);
    AddReg(category, R::kX64_xmm8, ctx.fxsave.xmm[8]);
    AddReg(category, R::kX64_xmm9, ctx.fxsave.xmm[9]);
    AddReg(category, R::kX64_xmm10, ctx.fxsave.xmm[10]);
    AddReg(category, R::kX64_xmm11, ctx.fxsave.xmm[11]);
    AddReg(category, R::kX64_xmm12, ctx.fxsave.xmm[12]);
    AddReg(category, R::kX64_xmm13, ctx.fxsave.xmm[13]);
    AddReg(category, R::kX64_xmm14, ctx.fxsave.xmm[14]);
    AddReg(category, R::kX64_xmm15, ctx.fxsave.xmm[15]);

    // YMM registers are missing from minidump at this time.
  }

  category =
      MakeCategory(pos, debug_ipc::RegisterCategory::Type::kDebug, reply);
  if (category != nullptr) {
    AddReg(category, R::kX64_dr0, ctx.dr0);
    AddReg(category, R::kX64_dr1, ctx.dr1);
    AddReg(category, R::kX64_dr2, ctx.dr2);
    AddReg(category, R::kX64_dr3, ctx.dr3);
    AddReg(category, R::kX64_dr6, ctx.dr6);
    AddReg(category, R::kX64_dr7, ctx.dr7);
  }
}

}  // namespace

class MinidumpReadDelegate : public crashpad::MemorySnapshot::Delegate {
 public:
  // Construct a delegate object for reading minidump memory regions.
  //
  // Minidump will always give us a pointer to the whole region and its size.
  // We give an offset and size of a portion of that region to read. Then when
  // the MemorySnapshotDelegateRead function is called, just that section will
  // be copied out into the ptr we give here.
  explicit MinidumpReadDelegate(uint64_t offset, size_t size, uint8_t* ptr)
      : offset_(offset), size_(size), ptr_(ptr) {}

  bool MemorySnapshotDelegateRead(void* data, size_t size) override {
    if (offset_ + size_ > size) {
      return false;
    }

    auto data_u8 = reinterpret_cast<uint8_t*>(data);
    data_u8 += offset_;

    std::copy(data_u8, data_u8 + size_, ptr_);
    return true;
  }

 private:
  uint64_t offset_;
  size_t size_;
  uint8_t* ptr_;
};

MinidumpRemoteAPI::MemoryRegion::MemoryRegion(
    const crashpad::MemorySnapshot* snapshot)
    : start(snapshot->Address()), size(snapshot->Size()), snapshot_(snapshot) {}

std::optional<std::vector<uint8_t>> MinidumpRemoteAPI::MemoryRegion::Read(
    uint64_t offset, size_t size) const {
  std::vector<uint8_t> data;
  data.resize(size);

  MinidumpReadDelegate d(offset, size, data.data());

  if (!snapshot_->Read(&d)) {
    return std::nullopt;
  }

  return std::move(data);
}

MinidumpRemoteAPI::MinidumpRemoteAPI(Session* session) : session_(session) {}

MinidumpRemoteAPI::~MinidumpRemoteAPI() = default;

std::string MinidumpRemoteAPI::ProcessName() {
  if (!minidump_) {
    return std::string();
  }

  auto mods = minidump_->Modules();

  if (mods.size() == 0) {
    return "<core dump>";
  }

  return mods[0]->Name();
}

void MinidumpRemoteAPI::CollectMemory() {
  for (const auto& thread : minidump_->Threads()) {
    const auto& stack = thread->Stack();

    if (!stack) {
      continue;
    }

    memory_.push_back(std::make_unique<MinidumpRemoteAPI::MemoryRegion>(stack));
  }

  std::sort(memory_.begin(), memory_.end(),
            [](const std::unique_ptr<MinidumpRemoteAPI::MemoryRegion>& a,
               const std::unique_ptr<MinidumpRemoteAPI::MemoryRegion>& b) {
              return a->start < b->start;
            });
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
    minidump_.release();
    return Err(fxl::StringPrintf("Minidump %s not valid", path.c_str()));
  }

  CollectMemory();

  return Err();
}

Err MinidumpRemoteAPI::Close() {
  if (!minidump_) {
    return Err("No open dump to close");
  }

  minidump_.reset();
  return Err();
}

void MinidumpRemoteAPI::Hello(
    const debug_ipc::HelloRequest& request,
    std::function<void(const Err&, debug_ipc::HelloReply)> cb) {
  Succeed(cb, debug_ipc::HelloReply());
}

void MinidumpRemoteAPI::Launch(
    const debug_ipc::LaunchRequest& request,
    std::function<void(const Err&, debug_ipc::LaunchReply)> cb) {
  ErrNoLive(cb);
}

void MinidumpRemoteAPI::Kill(
    const debug_ipc::KillRequest& request,
    std::function<void(const Err&, debug_ipc::KillReply)> cb) {
  ErrNoLive(cb);
}

constexpr uint32_t kAttachOk = 0;
constexpr uint32_t kAttachNotFound = 1;

void MinidumpRemoteAPI::Attach(
    const debug_ipc::AttachRequest& request,
    std::function<void(const Err&, debug_ipc::AttachReply)> cb) {
  if (!minidump_) {
    ErrNoDump(cb);
    return;
  }

  debug_ipc::AttachReply reply;
  reply.name = ProcessName();

  if (static_cast<pid_t>(request.koid) != minidump_->ProcessID()) {
    reply.status = kAttachNotFound;
    Succeed(cb, reply);
    return;
  }

  reply.status = kAttachOk;
  attached_ = true;

  std::vector<debug_ipc::NotifyThread> notifications;

  for (const auto& thread : minidump_->Threads()) {
    auto& notification = notifications.emplace_back();

    notification.process_koid = minidump_->ProcessID();
    notification.record.koid = thread->ThreadID();
    notification.record.state = debug_ipc::ThreadRecord::State::kCoreDump;
  }

  Session* session = session_;

  std::function<void(const Err&, debug_ipc::AttachReply)> new_cb =
      [cb, notifications, session](const Err& e, debug_ipc::AttachReply a) {
        cb(e, a);

        for (const auto& notification : notifications) {
          session->DispatchNotifyThread(
              debug_ipc::MsgHeader::Type::kNotifyThreadStarting, notification);
        }
      };

  Succeed(new_cb, reply);
}

void MinidumpRemoteAPI::Detach(
    const debug_ipc::DetachRequest& request,
    std::function<void(const Err&, debug_ipc::DetachReply)> cb) {
  if (!minidump_) {
    ErrNoDump(cb);
    return;
  }

  debug_ipc::DetachReply reply;

  if (static_cast<pid_t>(request.koid) == minidump_->ProcessID() && attached_) {
    reply.status = kAttachOk;
    attached_ = false;
  } else {
    reply.status = kAttachNotFound;
  }

  Succeed(cb, reply);
}

void MinidumpRemoteAPI::Modules(
    const debug_ipc::ModulesRequest& request,
    std::function<void(const Err&, debug_ipc::ModulesReply)> cb) {
  if (!minidump_) {
    ErrNoDump(cb);
    return;
  }

  debug_ipc::ModulesReply reply;

  if (static_cast<pid_t>(request.process_koid) != minidump_->ProcessID()) {
    Succeed(cb, reply);
    return;
  }

  for (const auto& minidump_mod : minidump_->Modules()) {
    auto& mod = reply.modules.emplace_back();
    mod.name = minidump_mod->Name();
    mod.base = minidump_mod->Address();

    crashpad::UUID uuid;
    uint32_t unused_;
    minidump_mod->UUIDAndAge(&uuid, &unused_);
    mod.build_id = uuid.ToString();
  }

  Succeed(cb, reply);
}

void MinidumpRemoteAPI::Pause(
    const debug_ipc::PauseRequest& request,
    std::function<void(const Err&, debug_ipc::PauseReply)> cb) {
  ErrNoLive(cb);
}

void MinidumpRemoteAPI::Resume(
    const debug_ipc::ResumeRequest& request,
    std::function<void(const Err&, debug_ipc::ResumeReply)> cb) {
  ErrNoLive(cb);
}

void MinidumpRemoteAPI::ProcessTree(
    const debug_ipc::ProcessTreeRequest& request,
    std::function<void(const Err&, debug_ipc::ProcessTreeReply)> cb) {
  if (!minidump_) {
    ErrNoDump(cb);
    return;
  }

  debug_ipc::ProcessTreeRecord record;

  record.type = debug_ipc::ProcessTreeRecord::Type::kProcess;
  record.name = ProcessName();
  record.koid = minidump_->ProcessID();

  debug_ipc::ProcessTreeReply reply{
      .root = record,
  };

  Succeed(cb, reply);
}

void MinidumpRemoteAPI::Threads(
    const debug_ipc::ThreadsRequest& request,
    std::function<void(const Err&, debug_ipc::ThreadsReply)> cb) {
  if (!minidump_) {
    ErrNoDump(cb);
    return;
  }

  debug_ipc::ThreadsReply reply;

  if (static_cast<pid_t>(request.process_koid) == minidump_->ProcessID()) {
    for (const auto& thread : minidump_->Threads()) {
      auto& record = reply.threads.emplace_back();

      record.koid = thread->ThreadID();
      record.state = debug_ipc::ThreadRecord::State::kCoreDump;
    }
  }

  Succeed(cb, reply);
}

void MinidumpRemoteAPI::ReadMemory(
    const debug_ipc::ReadMemoryRequest& request,
    std::function<void(const Err&, debug_ipc::ReadMemoryReply)> cb) {
  if (!minidump_) {
    ErrNoDump(cb);
    return;
  }

  debug_ipc::ReadMemoryReply reply;
  uint64_t loc = request.address;
  uint64_t end = request.address + request.size;

  if (static_cast<pid_t>(request.process_koid) != minidump_->ProcessID()) {
    Succeed(cb, reply);
    return;
  }

  for (const auto& reg : memory_) {
    if (loc == end) {
      break;
    }

    if (reg->start + reg->size <= loc) {
      continue;
    }

    if (reg->start > loc) {
      uint64_t stop = std::min(reg->start, end);
      reply.blocks.emplace_back();

      reply.blocks.back().address = loc;
      reply.blocks.back().valid = false;
      reply.blocks.back().size = static_cast<uint32_t>(stop - loc);

      loc = stop;

      if (loc == end) {
        break;
      }
    }

    uint64_t stop = std::min(reg->start + reg->size, end);
    auto data = reg->Read(loc - reg->start, stop - loc);
    reply.blocks.emplace_back();
    reply.blocks.back().address = loc;
    reply.blocks.back().valid = !!data;
    reply.blocks.back().size = static_cast<uint32_t>(stop - loc);
    reply.blocks.back().data = std::move(*data);

    loc += reply.blocks.back().size;
  }

  Succeed(cb, reply);
}

void MinidumpRemoteAPI::ReadRegisters(
    const debug_ipc::ReadRegistersRequest& request,
    std::function<void(const Err&, debug_ipc::ReadRegistersReply)> cb) {
  if (!minidump_) {
    ErrNoDump(cb);
    return;
  }

  debug_ipc::ReadRegistersReply reply;

  if (static_cast<pid_t>(request.process_koid) != minidump_->ProcessID()) {
    Succeed(cb, reply);
    return;
  }

  const crashpad::ThreadSnapshot* thread = nullptr;

  for (const auto& item : minidump_->Threads()) {
    if (item->ThreadID() == request.thread_koid) {
      thread = item;
      break;
    }
  }

  if (thread == nullptr) {
    Succeed(cb, reply);
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
      ErrNoArch(cb);
      return;
  }

  Succeed(cb, reply);
}

void MinidumpRemoteAPI::AddOrChangeBreakpoint(
    const debug_ipc::AddOrChangeBreakpointRequest& request,
    std::function<void(const Err&, debug_ipc::AddOrChangeBreakpointReply)> cb) {
  ErrNoLive(cb);
}

void MinidumpRemoteAPI::RemoveBreakpoint(
    const debug_ipc::RemoveBreakpointRequest& request,
    std::function<void(const Err&, debug_ipc::RemoveBreakpointReply)> cb) {
  ErrNoLive(cb);
}

void MinidumpRemoteAPI::ThreadStatus(
    const debug_ipc::ThreadStatusRequest& request,
    std::function<void(const Err&, debug_ipc::ThreadStatusReply)> cb) {
  // TODO
  ErrNoImpl(cb);
}

void MinidumpRemoteAPI::AddressSpace(
    const debug_ipc::AddressSpaceRequest& request,
    std::function<void(const Err&, debug_ipc::AddressSpaceReply)> cb) {
  if (!minidump_) {
    ErrNoDump(cb);
    return;
  }

  debug_ipc::AddressSpaceReply reply;

  if (static_cast<pid_t>(request.process_koid) == minidump_->ProcessID()) {
    for (const auto& region_object : minidump_->MemoryMap()) {
      const auto& region = region_object->AsMinidumpMemoryInfo();

      if (request.address > 0 &&
          (request.address < region.BaseAddress ||
           request.address >= region.BaseAddress + region.RegionSize)) {
        continue;
      }

      auto& record = reply.map.emplace_back();

      record.base = region.BaseAddress;
      record.size = region.RegionSize;
    }
  }

  Succeed(cb, reply);
}

void MinidumpRemoteAPI::JobFilter(
    const debug_ipc::JobFilterRequest& request,
    std::function<void(const Err&, debug_ipc::JobFilterReply)> cb) {
  ErrNoLive(cb);
}

void MinidumpRemoteAPI::WriteMemory(
    const debug_ipc::WriteMemoryRequest& request,
    std::function<void(const Err&, debug_ipc::WriteMemoryReply)> cb) {
  ErrNoLive(cb);
}

}  // namespace zxdb
