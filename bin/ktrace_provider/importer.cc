// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/src/ktrace_provider/importer.h"

#include "apps/tracing/src/ktrace_provider/tags.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

using namespace tracing;
using namespace tracing::writer;

namespace ktrace_provider {
namespace {

constexpr mx_koid_t kNoProcess = 0u;
constexpr mx_koid_t kKernelThreadFlag = 0x100000000;

constexpr uint64_t ToUInt64(uint32_t lo, uint32_t hi) {
  return (static_cast<uint64_t>(hi) << 32) | lo;
}

}  // namespace

Importer::Importer(TraceWriter& writer)
    : writer_(writer),
      tags_(GetTags()),
      ipc_category_ref_(writer_.RegisterString("kernel:ipc")),
      irq_category_ref_(writer_.RegisterString("kernel:irq")),
      probe_category_ref_(writer_.RegisterString("kernel:probe")),
      syscall_category_ref_(writer_.RegisterString("kernel:syscall")),
      page_fault_name_ref_(writer_.RegisterString("page_fault")),
      vaddr_name_ref_(writer_.RegisterString("vaddr")),
      flags_name_ref_(writer_.RegisterString("flags")),
      arg0_name_ref_(writer_.RegisterString("arg0")),
      arg1_name_ref_(writer_.RegisterString("arg1")) {}

Importer::~Importer() = default;

bool Importer::Import(const uint8_t* buffer, size_t size) {
  if (!writer_)
    return false;

  writer_.WriteProcessDescription(kNoProcess, "kernel");

  const uint8_t* current = buffer;
  const uint8_t* end = current + size;
  while (current + sizeof(ktrace_header_t) <= end) {
    const ktrace_header* record =
        reinterpret_cast<const ktrace_header*>(current);

    size_t record_size = KTRACE_LEN(record->tag);
    if (record_size < sizeof(ktrace_header_t)) {
      FTL_VLOG(2) << "Skipped ktrace record with invalid size at " << std::hex
                  << (current - buffer) << ", tag=" << record->tag;
      current += sizeof(uint64_t);
      continue;
    }

    if (current + record_size > end)
      break;

    if (!ImportRecord(record, record_size)) {
      FTL_VLOG(2) << "Skipped ktrace record at " << std::hex
                  << (current - buffer) << ", tag=" << record->tag;
    }
    current += record_size;
  }
  return true;
}

bool Importer::ImportRecord(const ktrace_header_t* record, size_t record_size) {
  auto it = tags_.find(KTRACE_EVENT(record->tag));
  if (it != tags_.end()) {
    const TagInfo& tag_info = it->second;
    switch (tag_info.type) {
      case TagType::kBasic:
        return ImportBasicRecord(record, tag_info);
      case TagType::kQuad:
        if (sizeof(ktrace_rec_32b_t) > record_size)
          return false;
        return ImportQuadRecord(
            reinterpret_cast<const ktrace_rec_32b_t*>(record), tag_info);
      case TagType::kName:
        if (sizeof(ktrace_rec_name_t) > record_size)
          return false;
        return ImportNameRecord(
            reinterpret_cast<const ktrace_rec_name_t*>(record), tag_info);
    }
  }

  if (KTRACE_EVENT(record->tag) & 0x800)
    return ImportProbeRecord(record, record_size);

  return ImportUnknownRecord(record, record_size);
}

bool Importer::ImportBasicRecord(const ktrace_header_t* record,
                                 const TagInfo& tag_info) {
  FTL_VLOG(2) << "BASIC: tag=0x" << std::hex << record->tag << " ("
              << tag_info.name << "), tid=" << std::dec << record->tid
              << ", timestamp=" << record->ts;

  switch (KTRACE_EVENT(record->tag)) {
    case KTRACE_EVENT(TAG_IRQ_ENTER):
      return HandleIRQEnter(record->ts, record->tid & 0xff, record->tid >> 8);
    case KTRACE_EVENT(TAG_IRQ_EXIT):
      return HandleIRQExit(record->ts, record->tid & 0xff, record->tid >> 8);
    case KTRACE_EVENT(TAG_SYSCALL_ENTER):
      return HandleSyscallEnter(record->ts, record->tid & 0xff,
                                record->tid >> 8);
    case KTRACE_EVENT(TAG_SYSCALL_EXIT):
      return HandleSyscallExit(record->ts, record->tid & 0xff,
                               record->tid >> 8);
    default:
      return false;
  }
}

bool Importer::ImportQuadRecord(const ktrace_rec_32b_t* record,
                                const TagInfo& tag_info) {
  FTL_VLOG(2) << "QUAD: tag=0x" << std::hex << record->tag << " ("
              << tag_info.name << "), tid=" << std::dec << record->tid
              << ", timestamp=" << record->ts << ", a=0x" << std::hex
              << record->a << ", b=0x" << record->b << ", c=0x" << record->c
              << ", d=0x" << record->d;

  switch (KTRACE_EVENT(record->tag)) {
    case KTRACE_EVENT(TAG_VERSION):
      version_ = record->a;
      return true;
    case KTRACE_EVENT(TAG_TICKS_PER_MS): {
      Ticks kernel_ticks_per_second = ToUInt64(record->a, record->b) * 1000u;
      Ticks user_ticks_per_second = GetTicksPerSecond();
      if (kernel_ticks_per_second != user_ticks_per_second) {
        FTL_LOG(WARNING)
            << "Kernel and userspace are using different tracing timebases, "
               "tracks may be misaligned: "
            << "kernel_ticks_per_second=" << kernel_ticks_per_second
            << "user_ticks_per_second=" << user_ticks_per_second;
      }
      return true;
    }
    case KTRACE_EVENT(TAG_PAGE_FAULT):
      return HandlePageFault(record->ts, record->d,
                             ToUInt64(record->a, record->b), record->c);
    case KTRACE_EVENT(TAG_CONTEXT_SWITCH):
      return HandleContextSwitch(record->ts, record->b & 0xffff,
                                 static_cast<ThreadState>(record->b >> 16),
                                 record->tid, record->c, record->a, record->d);
    case KTRACE_EVENT(TAG_OBJECT_DELETE):
      return HandleObjectDelete(record->ts, record->tid, record->a);
    case KTRACE_EVENT(TAG_THREAD_CREATE):
      return HandleThreadCreate(record->ts, record->tid, record->a, record->b);
    case KTRACE_EVENT(TAG_THREAD_START):
      return HandleThreadStart(record->ts, record->tid, record->a);
    case KTRACE_EVENT(TAG_THREAD_EXIT):
      return HandleThreadExit(record->ts, record->tid);
    case KTRACE_EVENT(TAG_PROC_CREATE):
      return HandleProcessCreate(record->ts, record->tid, record->a);
    case KTRACE_EVENT(TAG_PROC_START):
      return HandleProcessStart(record->ts, record->tid, record->a, record->b);
    case KTRACE_EVENT(TAG_PROC_EXIT):
      return HandleProcessExit(record->ts, record->tid, record->a);
    case KTRACE_EVENT(TAG_CHANNEL_CREATE):
      return HandleChannelCreate(record->ts, record->tid, record->a, record->b,
                                 record->c);
    case KTRACE_EVENT(TAG_CHANNEL_WRITE):
      return HandleChannelWrite(record->ts, record->tid, record->a, record->b,
                                record->c);
    case KTRACE_EVENT(TAG_CHANNEL_READ):
      return HandleChannelRead(record->ts, record->tid, record->a, record->b,
                               record->c);
    case KTRACE_EVENT(TAG_PORT_WAIT):
      return HandlePortWait(record->ts, record->tid, record->a);
    case KTRACE_EVENT(TAG_PORT_WAIT_DONE):
      return HandlePortWaitDone(record->ts, record->tid, record->a, record->b);
    case KTRACE_EVENT(TAG_PORT_CREATE):
      return HandlePortCreate(record->ts, record->tid, record->a);
    case KTRACE_EVENT(TAG_PORT_QUEUE):
      return HandlePortQueue(record->ts, record->tid, record->a, record->b);
    case KTRACE_EVENT(TAG_WAIT_ONE):
      return HandleWaitOne(record->ts, record->tid, record->a, record->b,
                           ToUInt64(record->c, record->d));
    case KTRACE_EVENT(TAG_WAIT_ONE_DONE):
      return HandleWaitOneDone(record->ts, record->tid, record->a, record->b,
                               record->c);
    default:
      return false;
  }
}

bool Importer::ImportNameRecord(const ktrace_rec_name_t* record,
                                const TagInfo& tag_info) {
  char name[KTRACE_NAMESIZE + 1];
  memcpy(name, record->name, KTRACE_NAMESIZE);
  name[KTRACE_NAMESIZE] = '\0';
  FTL_VLOG(2) << "NAME: tag=0x" << std::hex << record->tag << " ("
              << tag_info.name << "), id=0x" << record->id << ", arg=0x"
              << record->arg << ", name='" << name << "'";

  switch (KTRACE_EVENT(record->tag)) {
    case KTRACE_EVENT(TAG_KTHREAD_NAME):
      return HandleKernelThreadName(record->id, name);
    case KTRACE_EVENT(TAG_THREAD_NAME):
      return HandleThreadName(record->id, record->arg, name);
    case KTRACE_EVENT(TAG_PROC_NAME):
      return HandleProcessName(record->id, name);
    case KTRACE_EVENT(TAG_SYSCALL_NAME):
      return HandleSyscallName(record->id, name);
    case KTRACE_EVENT(TAG_IRQ_NAME):
      return HandleIRQName(record->id, name);
    case KTRACE_EVENT(TAG_PROBE_NAME):
      return HandleProbeName(record->id, name);
    default:
      return false;
  }
}

bool Importer::ImportProbeRecord(const ktrace_header_t* record,
                                 size_t record_size) {
  uint32_t probe = record->tag & 0x7ff;
  if (record_size >= 24) {
    uint32_t arg0 = reinterpret_cast<const uint32_t*>(record + 1)[0];
    uint32_t arg1 = reinterpret_cast<const uint32_t*>(record + 1)[1];
    FTL_VLOG(2) << "PROBE: tag=0x" << std::hex << record->tag << ", probe=0x"
                << probe << ", tid=" << std::dec << record->tid
                << ", ts=" << record->ts << ", arg0=0x" << std::hex << arg0
                << ", arg1=0x" << arg1;
    return HandleProbe(record->ts, record->tid, probe, arg0, arg1);
  }

  FTL_VLOG(2) << "PROBE: tag=0x" << std::hex << record->tag << ", probe=0x"
              << probe << ", tid=" << std::dec << record->tid
              << ", ts=" << record->ts;
  return HandleProbe(record->ts, record->tid, probe);
}

bool Importer::ImportUnknownRecord(const ktrace_header_t* record,
                                   size_t record_size) {
  FTL_VLOG(2) << "UNKNOWN: tag=0x" << std::hex << record->tag
              << ", size=" << std::dec << record_size;
  return false;
}

bool Importer::HandleKernelThreadName(KernelThread kernel_thread,
                                      std::string name) {
  writer_.WriteThreadDescription(kNoProcess, kKernelThreadFlag | kernel_thread,
                                 std::move(name));
  kernel_thread_refs_.emplace(
      kernel_thread,
      writer_.RegisterThread(kNoProcess, kKernelThreadFlag | kernel_thread));
  return true;
}

bool Importer::HandleThreadName(mx_koid_t thread,
                                mx_koid_t process,
                                std::string name) {
  writer_.WriteThreadDescription(process, thread, std::move(name));
  thread_refs_.emplace(thread, writer_.RegisterThread(process, thread));
  return true;
}

bool Importer::HandleProcessName(mx_koid_t process, std::string name) {
  writer_.WriteProcessDescription(process, std::move(name));
  return true;
}

bool Importer::HandleSyscallName(uint32_t syscall, std::string name) {
  syscall_names_.emplace(syscall, writer_.RegisterStringCopy(std::move(name)));
  return true;
}

bool Importer::HandleIRQName(uint32_t irq, std::string name) {
  irq_names_.emplace(irq, writer_.RegisterStringCopy(std::move(name)));
  return true;
}

bool Importer::HandleProbeName(uint32_t probe, std::string name) {
  probe_names_.emplace(probe, writer_.RegisterStringCopy(std::move(name)));
  return true;
}

bool Importer::HandleIRQEnter(Ticks event_time,
                              CpuNumber cpu_number,
                              uint32_t irq) {
  ThreadRef thread_ref = GetCpuCurrentThread(cpu_number);
  if (!thread_ref.is_unknown()) {
    writer_.WriteDurationBeginEventRecord(event_time, thread_ref,
                                          irq_category_ref_,
                                          GetNameRef(irq_names_, "irq", irq));
  }
  return true;
}

bool Importer::HandleIRQExit(Ticks event_time,
                             CpuNumber cpu_number,
                             uint32_t irq) {
  ThreadRef thread_ref = GetCpuCurrentThread(cpu_number);
  if (!thread_ref.is_unknown()) {
    writer_.WriteDurationEndEventRecord(event_time, thread_ref,
                                        irq_category_ref_,
                                        GetNameRef(irq_names_, "irq", irq));
  }
  return true;
}

bool Importer::HandleSyscallEnter(Ticks event_time,
                                  CpuNumber cpu_number,
                                  uint32_t syscall) {
  ThreadRef thread_ref = GetCpuCurrentThread(cpu_number);
  if (!thread_ref.is_unknown()) {
    writer_.WriteDurationBeginEventRecord(
        event_time, thread_ref, syscall_category_ref_,
        GetNameRef(syscall_names_, "syscall", syscall));
  }
  return true;
}

bool Importer::HandleSyscallExit(Ticks event_time,
                                 CpuNumber cpu_number,
                                 uint32_t syscall) {
  ThreadRef thread_ref = GetCpuCurrentThread(cpu_number);
  if (!thread_ref.is_unknown()) {
    writer_.WriteDurationEndEventRecord(
        event_time, thread_ref, syscall_category_ref_,
        GetNameRef(syscall_names_, "syscall", syscall));
  }
  return true;
}

bool Importer::HandlePageFault(Ticks event_time,
                               CpuNumber cpu_number,
                               uint64_t virtual_address,
                               uint32_t flags) {
  ThreadRef thread_ref = GetCpuCurrentThread(cpu_number);
  if (!thread_ref.is_unknown()) {
    writer_.WriteInstantEventRecord(
        event_time, thread_ref, irq_category_ref_, page_fault_name_ref_,
        EventScope::kThread, PointerArgument(vaddr_name_ref_, virtual_address),
        Int32Argument(flags_name_ref_, flags));
  }
  return true;
}

bool Importer::HandleContextSwitch(Ticks event_time,
                                   CpuNumber cpu_number,
                                   ThreadState outgoing_thread_state,
                                   mx_koid_t outgoing_thread,
                                   KernelThread outgoing_kernel_thread,
                                   mx_koid_t incoming_thread,
                                   KernelThread incoming_kernel_thread) {
  ThreadRef outgoing_thread_ref = GetCpuCurrentThread(cpu_number);
  ThreadRef incoming_thread_ref =
      incoming_thread
          ? SwitchCpuToThread(cpu_number, incoming_thread)
          : SwitchCpuToKernelThread(cpu_number, incoming_kernel_thread);
  if (!outgoing_thread_ref.is_unknown() || !incoming_thread_ref.is_unknown()) {
    writer_.WriteContextSwitchRecord(event_time, cpu_number,
                                     outgoing_thread_state, outgoing_thread_ref,
                                     incoming_thread_ref);
  }
  return true;
}

bool Importer::HandleObjectDelete(Ticks event_time,
                                  mx_koid_t thread,
                                  mx_koid_t object) {
  return false;
}

bool Importer::HandleThreadCreate(Ticks event_time,
                                  mx_koid_t thread,
                                  mx_koid_t affected_thread,
                                  mx_koid_t affected_process) {
  return false;
}

bool Importer::HandleThreadStart(Ticks event_time,
                                 mx_koid_t thread,
                                 mx_koid_t affected_thread) {
  return false;
}

bool Importer::HandleThreadExit(Ticks event_time, mx_koid_t thread) {
  return false;
}

bool Importer::HandleProcessCreate(Ticks event_time,
                                   mx_koid_t thread,
                                   mx_koid_t affected_process) {
  return false;
}

bool Importer::HandleProcessStart(Ticks event_time,
                                  mx_koid_t thread,
                                  mx_koid_t affected_thread,
                                  mx_koid_t affected_process) {
  return false;
}

bool Importer::HandleProcessExit(Ticks event_time,
                                 mx_koid_t thread,
                                 mx_koid_t affected_process) {
  return false;
}

bool Importer::HandleChannelCreate(Ticks event_time,
                                   mx_koid_t thread,
                                   mx_koid_t channel0,
                                   mx_koid_t channel1,
                                   uint32_t flags) {
  return false;
}

bool Importer::HandleChannelWrite(Ticks event_time,
                                  mx_koid_t thread,
                                  mx_koid_t channel,
                                  uint32_t num_bytes,
                                  uint32_t num_handles) {
  return false;
}

bool Importer::HandleChannelRead(Ticks event_time,
                                 mx_koid_t thread,
                                 mx_koid_t channel,
                                 uint32_t num_bytes,
                                 uint32_t num_handles) {
  return false;
}

bool Importer::HandlePortWait(Ticks event_time,
                              mx_koid_t thread,
                              mx_koid_t port) {
  return false;
}

bool Importer::HandlePortWaitDone(Ticks event_time,
                                  mx_koid_t thread,
                                  mx_koid_t port,
                                  uint32_t status) {
  return false;
}

bool Importer::HandlePortCreate(Ticks event_time,
                                mx_koid_t thread,
                                mx_koid_t port) {
  return false;
}

bool Importer::HandlePortQueue(Ticks event_time,
                               mx_koid_t thread,
                               mx_koid_t port,
                               uint32_t num_bytes) {
  return false;
}

bool Importer::HandleWaitOne(Ticks event_time,
                             mx_koid_t thread,
                             mx_koid_t object,
                             uint32_t signals,
                             mx_time_t timeout) {
  return false;
}

bool Importer::HandleWaitOneDone(Ticks event_time,
                                 mx_koid_t thread,
                                 mx_koid_t object,
                                 uint32_t status,
                                 uint32_t pending) {
  return false;
}

bool Importer::HandleProbe(Ticks event_time, mx_koid_t thread, uint32_t probe) {
  writer_.WriteInstantEventRecord(
      event_time, GetThreadRef(thread), probe_category_ref_,
      GetNameRef(probe_names_, "probe", probe), EventScope::kThread);
  return true;
}

bool Importer::HandleProbe(Ticks event_time,
                           mx_koid_t thread,
                           uint32_t probe,
                           uint32_t arg0,
                           uint32_t arg1) {
  writer_.WriteInstantEventRecord(
      event_time, GetThreadRef(thread), probe_category_ref_,
      GetNameRef(probe_names_, "probe", probe), EventScope::kThread,
      Int32Argument(arg0_name_ref_, arg0), Int32Argument(arg1_name_ref_, arg1));
  return true;
}

Importer::ThreadRef Importer::GetCpuCurrentThread(CpuNumber cpu_number) {
  if (cpu_number >= cpu_infos_.size())
    return ThreadRef::MakeUnknown();
  return cpu_infos_[cpu_number].current_thread_ref;
}

ThreadRef Importer::SwitchCpuToThread(CpuNumber cpu_number, mx_koid_t thread) {
  if (cpu_number >= cpu_infos_.size())
    cpu_infos_.resize(cpu_number + 1u);
  return cpu_infos_[cpu_number].current_thread_ref = GetThreadRef(thread);
}

ThreadRef Importer::SwitchCpuToKernelThread(CpuNumber cpu_number,
                                            KernelThread kernel_thread) {
  if (cpu_number >= cpu_infos_.size())
    cpu_infos_.resize(cpu_number + 1u);
  return cpu_infos_[cpu_number].current_thread_ref =
             GetKernelThreadRef(kernel_thread);
}

const Importer::StringRef& Importer::GetNameRef(
    std::unordered_map<uint32_t, StringRef>& table,
    const char* kind,
    uint32_t id) {
  auto it = table.find(id);
  if (it == table.end()) {
    std::tie(it, std::ignore) = table.emplace(
        id, writer_.RegisterStringCopy(ftl::StringPrintf("%s 0x%d", kind, id)));
  }
  return it->second;
}

const Importer::ThreadRef& Importer::GetThreadRef(mx_koid_t thread) {
  auto it = thread_refs_.find(thread);
  if (it == thread_refs_.end()) {
    std::tie(it, std::ignore) = thread_refs_.emplace(
        thread, ThreadRef::MakeInlined(kNoProcess, thread));
  }
  return it->second;
}

const Importer::ThreadRef& Importer::GetKernelThreadRef(
    KernelThread kernel_thread) {
  auto it = kernel_thread_refs_.find(kernel_thread);
  if (it == kernel_thread_refs_.end()) {
    std::tie(it, std::ignore) = kernel_thread_refs_.emplace(
        kernel_thread,
        ThreadRef::MakeInlined(kNoProcess, kKernelThreadFlag | kernel_thread));
  }
  return it->second;
}

}  // namespace ktrace_provider
