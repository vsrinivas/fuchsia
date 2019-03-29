// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ktrace_provider/importer.h"

#include <fbl/algorithm.h>
#include <fbl/string_printf.h>
#include <zircon/syscalls.h>

#include "garnet/bin/ktrace_provider/reader.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/time/time_point.h"

namespace ktrace_provider {
namespace {

constexpr zx_koid_t kNoProcess = 0u;
constexpr zx_koid_t kKernelThreadFlag = 0x100000000;

constexpr zx_koid_t kKernelPseudoKoidBase = 0x00000000'70000000u;
constexpr zx_koid_t kKernelPseudoCpuBase =
    kKernelPseudoKoidBase + 0x00000000'01000000u;

constexpr uint64_t ToUInt64(uint32_t lo, uint32_t hi) {
  return (static_cast<uint64_t>(hi) << 32) | lo;
}

// The kernel reports different thread state values through ktrace.
// These values must line up with those in "kernel/include/kernel/thread.h".
constexpr trace_thread_state_t ToTraceThreadState(int value) {
  switch (value) {
    case 0:  // THREAD_INITIAL
    case 1:  // THREAD_READY
      return ZX_THREAD_STATE_NEW;
    case 2:  // THREAD_RUNNING
      return ZX_THREAD_STATE_RUNNING;
    case 3:  // THREAD_BLOCKED
    case 4:  // THREAD_BLOCKED_READ_LOCK
    case 5:  // THREAD_SLEEPING
      return ZX_THREAD_STATE_BLOCKED;
    case 6:  // THREAD_SUSPENDED
      return ZX_THREAD_STATE_SUSPENDED;
    case 7:  // THREAD_DEATH
      return ZX_THREAD_STATE_DEAD;
    default:  // ???
      FXL_LOG(WARNING) << "Imported unknown thread state from ktrace: "
                       << value;
      return INT32_MAX;
  }
}

}  // namespace

#define MAKE_STRING(literal) \
  trace_context_make_registered_string_literal(context_, literal)

Importer::Importer(trace_context_t* context)
    : context_(context),
      tags_(GetTags()),
      kernel_string_ref_(MAKE_STRING("kernel")),
      unknown_category_ref_(MAKE_STRING("kernel:unknown")),
      arch_category_ref_(MAKE_STRING("kernel:arch")),
      meta_category_ref_(MAKE_STRING("kernel:meta")),
      lifecycle_category_ref_(MAKE_STRING("kernel:lifecycle")),
      tasks_category_ref_(MAKE_STRING("kernel:tasks")),
      ipc_category_ref_(MAKE_STRING("kernel:ipc")),
      irq_category_ref_(MAKE_STRING("kernel:irq")),
      probe_category_ref_(MAKE_STRING("kernel:probe")),
      sched_category_ref_(MAKE_STRING("kernel:sched")),
      syscall_category_ref_(MAKE_STRING("kernel:syscall")),
      channel_category_ref_(MAKE_STRING("kernel:channel")),
      vcpu_category_ref_(MAKE_STRING("kernel:vcpu")),
      channel_read_name_ref_(MAKE_STRING("read")),
      channel_write_name_ref_(MAKE_STRING("write")),
      num_bytes_name_ref_(MAKE_STRING("num_bytes")),
      num_handles_name_ref_(MAKE_STRING("num_handles")),
      page_fault_name_ref_(MAKE_STRING("page_fault")),
      vaddr_name_ref_(MAKE_STRING("vaddr")),
      flags_name_ref_(MAKE_STRING("flags")),
      exit_address_name_ref_(MAKE_STRING("exit_address")),
      arg0_name_ref_(MAKE_STRING("arg0")),
      arg1_name_ref_(MAKE_STRING("arg1")),
      kUnknownThreadRef(trace_make_unknown_thread_ref()) {}

#undef MAKE_STRING

Importer::~Importer() = default;

bool Importer::Import(Reader& reader) {
  trace_context_write_process_info_record(context_, kNoProcess,
                                          &kernel_string_ref_);

  auto start = fxl::TimePoint::Now();

  while (true) {
    if (auto record = reader.ReadNextRecord()) {
      if (!ImportRecord(record, KTRACE_LEN(record->tag))) {
        FXL_VLOG(2) << "Skipped ktrace record, tag=" << record->tag;
      }
    } else {
      break;
    }
  }

  size_t nr_bytes_read = reader.number_bytes_read();
  size_t nr_records_read = reader.number_records_read();

  // This is an INFO and not VLOG() as we currently always want to see this.
  FXL_LOG(INFO) << "Import of " << nr_records_read << " ktrace records"
                << "(" << nr_bytes_read << " bytes) took: "
                << (fxl::TimePoint::Now() - start).ToMicroseconds() << "us";

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

  // TODO(eieio): Using this combination of bits and groups to select the record
  // type is a bit hacky due to how the kernel trace record is defined. Fixing
  // this requires a re-design or replacement with the same strategy used in the
  // rest of the system.
  const bool is_probe_group = KTRACE_GROUP(record->tag) & KTRACE_GRP_PROBE;
  const bool is_flow = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_FLOW;
  const bool is_begin = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_BEGIN;
  const bool is_end = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_END;
  const bool is_duration = !is_flow && (is_begin | is_end);

  if (is_probe_group)
    return ImportProbeRecord(record, record_size);
  else if (is_duration)
    return ImportDurationRecord(record, record_size);
  else if (is_flow)
    return ImportFlowRecord(record, record_size);

  return ImportUnknownRecord(record, record_size);
}

bool Importer::ImportBasicRecord(const ktrace_header_t* record,
                                 const TagInfo& tag_info) {
  FXL_VLOG(2) << "BASIC: tag=0x" << std::hex << record->tag << " ("
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
  FXL_VLOG(2) << "QUAD: tag=0x" << std::hex << record->tag << " ("
              << tag_info.name << "), tid=" << std::dec << record->tid
              << ", timestamp=" << record->ts << ", a=0x" << std::hex
              << record->a << ", b=0x" << record->b << ", c=0x" << record->c
              << ", d=0x" << record->d;

  switch (KTRACE_EVENT(record->tag)) {
    case KTRACE_EVENT(TAG_VERSION):
      version_ = record->a;
      return true;
    case KTRACE_EVENT(TAG_TICKS_PER_MS): {
      trace_ticks_t kernel_ticks_per_second =
          ToUInt64(record->a, record->b) * 1000u;
      trace_ticks_t user_ticks_per_second = zx_ticks_per_second();
      if (kernel_ticks_per_second != user_ticks_per_second) {
        FXL_LOG(WARNING) << "Kernel and userspace are using different tracing "
                            "timebases, "
                            "tracks may be misaligned: "
                         << "kernel_ticks_per_second="
                         << kernel_ticks_per_second
                         << "user_ticks_per_second=" << user_ticks_per_second;
      }
      return true;
    }
    case KTRACE_EVENT(TAG_PAGE_FAULT):
      return HandlePageFault(record->ts, record->d,
                             ToUInt64(record->a, record->b), record->c);
    case KTRACE_EVENT(TAG_CONTEXT_SWITCH): {
      trace_cpu_number_t cpu = record->b & 0xff;
      trace_thread_state_t outgoing_thread_state =
          ToTraceThreadState((record->b >> 8) & 0xff);
      trace_thread_priority_t outgoing_thread_priority =
          (record->b >> 16) & 0xff;
      trace_thread_priority_t incoming_thread_priority = record->b >> 24;
      return HandleContextSwitch(record->ts, cpu, outgoing_thread_state,
                                 outgoing_thread_priority,
                                 incoming_thread_priority, record->tid,
                                 record->c, record->a, record->d);
    }
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
    case KTRACE_EVENT(TAG_VCPU_ENTER):
      return HandleVcpuEnter(record->ts, record->tid);
    case KTRACE_EVENT(TAG_VCPU_EXIT):
      return HandleVcpuExit(record->ts, record->tid, record->a,
                            ToUInt64(record->b, record->c));
    case KTRACE_EVENT(TAG_VCPU_BLOCK):
      return HandleVcpuBlock(record->ts, record->tid, record->a);
    case KTRACE_EVENT(TAG_VCPU_UNBLOCK):
      return HandleVcpuUnblock(record->ts, record->tid, record->a);
    default:
      return false;
  }
}

bool Importer::ImportNameRecord(const ktrace_rec_name_t* record,
                                const TagInfo& tag_info) {
  fbl::StringPiece name(record->name,
                        strnlen(record->name, ZX_MAX_NAME_LEN - 1));
  FXL_VLOG(2) << "NAME: tag=0x" << std::hex << record->tag << " ("
              << tag_info.name << "), id=0x" << record->id << ", arg=0x"
              << record->arg << ", name='" << fbl::String(name).c_str() << "'";

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
    case KTRACE_EVENT(TAG_VCPU_META):
      return HandleVcpuMeta(record->id, name);
    case KTRACE_EVENT(TAG_VCPU_EXIT_META):
      return HandleVcpuExitMeta(record->id, name);
    default:
      return false;
  }
}

bool Importer::ImportProbeRecord(const ktrace_header_t* record,
                                 size_t record_size) {
  if (!(KTRACE_EVENT(record->tag) & KTRACE_NAMED_EVENT_BIT)) {
    return ImportUnknownRecord(record, record_size);
  }

  const uint32_t event_name_id = KTRACE_EVENT_NAME_ID(record->tag);
  const bool cpu_trace = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_CPU;

  if (record_size == 24) {
    const auto arg0 = reinterpret_cast<const uint32_t*>(record + 1)[0];
    const auto arg1 = reinterpret_cast<const uint32_t*>(record + 1)[1];
    FXL_VLOG(2) << "PROBE: tag=0x" << std::hex << record->tag
                << ", event_name_id=0x" << event_name_id << ", tid=" << std::dec
                << record->tid << ", ts=" << record->ts << ", arg0=0x"
                << std::hex << arg0 << ", arg1=0x" << arg1;
    return HandleProbe(record->ts, record->tid, event_name_id, cpu_trace, arg0,
                       arg1);
  } else if (record_size == 32) {
    const auto arg0 = reinterpret_cast<const uint64_t*>(record + 1)[0];
    const auto arg1 = reinterpret_cast<const uint64_t*>(record + 1)[1];
    FXL_VLOG(2) << "PROBE: tag=0x" << std::hex << record->tag
                << ", event_name_id=0x" << event_name_id << ", tid=" << std::dec
                << record->tid << ", ts=" << record->ts << ", arg0=0x"
                << std::hex << arg0 << ", arg1=0x" << arg1;
    return HandleProbe(record->ts, record->tid, event_name_id, cpu_trace, arg0,
                       arg1);
  }

  FXL_VLOG(2) << "PROBE: tag=0x" << std::hex << record->tag
              << ", event_name_id=0x" << event_name_id << ", tid=" << std::dec
              << record->tid << ", ts=" << record->ts;
  return HandleProbe(record->ts, record->tid, event_name_id, cpu_trace);
}

bool Importer::ImportDurationRecord(const ktrace_header_t* record,
                                    size_t record_size) {
  if (!(KTRACE_EVENT(record->tag) & KTRACE_NAMED_EVENT_BIT)) {
    return ImportUnknownRecord(record, record_size);
  }

  const uint32_t event_name_id = KTRACE_EVENT_NAME_ID(record->tag);
  const uint32_t group = KTRACE_GROUP(record->tag);
  const bool cpu_trace = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_CPU;
  const bool is_begin = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_BEGIN;
  const bool is_end = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_END;

  if (record_size == 32) {
    const auto arg0 = reinterpret_cast<const uint64_t*>(record + 1)[0];
    const auto arg1 = reinterpret_cast<const uint64_t*>(record + 1)[1];
    if (is_begin) {
      return HandleDurationBegin(record->ts, record->tid, event_name_id, group,
                                 cpu_trace, arg0, arg1);
    } else if (is_end) {
      return HandleDurationEnd(record->ts, record->tid, event_name_id, group,
                               cpu_trace, arg0, arg1);
    }
  } else {
    if (is_begin) {
      return HandleDurationBegin(record->ts, record->tid, event_name_id, group,
                                 cpu_trace);
    } else if (is_end) {
      return HandleDurationEnd(record->ts, record->tid, event_name_id, group,
                               cpu_trace);
    }
  }

  return false;
}

bool Importer::ImportFlowRecord(const ktrace_header_t* record,
                                size_t record_size) {
  FXL_DCHECK(KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_FLOW);

  if (!(KTRACE_EVENT(record->tag) & KTRACE_NAMED_EVENT_BIT)) {
    return ImportUnknownRecord(record, record_size);
  }

  const uint32_t event_name_id = KTRACE_EVENT_NAME_ID(record->tag);
  const uint32_t group = KTRACE_GROUP(record->tag);
  const bool cpu_trace = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_CPU;
  const bool is_begin = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_BEGIN;
  const bool is_end = KTRACE_FLAGS(record->tag) & KTRACE_FLAGS_END;

  if (record_size == 32) {
    const auto flow_id = reinterpret_cast<const uint64_t*>(record + 1)[0];
    if (is_begin) {
      return HandleFlowBegin(record->ts, record->tid, event_name_id, group,
                             cpu_trace, flow_id);
    } else if (is_end) {
      return HandleFlowEnd(record->ts, record->tid, event_name_id, group,
                           cpu_trace, flow_id);
    } else {
      return ImportUnknownRecord(record, record_size);
    }
  }

  return false;
}

bool Importer::ImportUnknownRecord(const ktrace_header_t* record,
                                   size_t record_size) {
  FXL_VLOG(2) << "UNKNOWN: tag=0x" << std::hex << record->tag
              << ", size=" << std::dec << record_size;
  return false;
}

bool Importer::HandleKernelThreadName(KernelThread kernel_thread,
                                      const fbl::StringPiece& name) {
  trace_string_ref name_ref =
      trace_make_inline_string_ref(name.data(), name.length());
  trace_context_write_thread_info_record(
      context_, kNoProcess, kKernelThreadFlag | kernel_thread, &name_ref);
  kernel_thread_refs_.emplace(
      kernel_thread,
      trace_context_make_registered_thread(context_, kNoProcess,
                                           kKernelThreadFlag | kernel_thread));
  return true;
}

bool Importer::HandleThreadName(zx_koid_t thread, zx_koid_t process,
                                const fbl::StringPiece& name) {
  trace_string_ref name_ref =
      trace_make_inline_string_ref(name.data(), name.length());
  trace_context_write_thread_info_record(context_, process, thread, &name_ref);
  thread_refs_.emplace(
      thread, trace_context_make_registered_thread(context_, process, thread));
  return true;
}

bool Importer::HandleProcessName(zx_koid_t process,
                                 const fbl::StringPiece& name) {
  trace_string_ref name_ref =
      trace_make_inline_string_ref(name.data(), name.length());
  trace_context_write_process_info_record(context_, process, &name_ref);
  return true;
}

bool Importer::HandleSyscallName(uint32_t syscall,
                                 const fbl::StringPiece& name) {
  syscall_names_.emplace(syscall, trace_context_make_registered_string_copy(
                                      context_, name.data(), name.length()));
  return true;
}

bool Importer::HandleIRQName(uint32_t irq, const fbl::StringPiece& name) {
  irq_names_.emplace(irq, trace_context_make_registered_string_copy(
                              context_, name.data(), name.length()));
  return true;
}

bool Importer::HandleProbeName(uint32_t event_name_id,
                               const fbl::StringPiece& name) {
  probe_names_.emplace(event_name_id,
                       trace_context_make_registered_string_copy(
                           context_, name.data(), name.length()));
  return true;
}

bool Importer::HandleVcpuMeta(uint32_t meta, const fbl::StringPiece& name) {
  vcpu_meta_.emplace(meta, trace_context_make_registered_string_copy(
                               context_, name.data(), name.length()));
  return true;
}

bool Importer::HandleVcpuExitMeta(uint32_t exit, const fbl::StringPiece& name) {
  vcpu_exit_meta_.emplace(exit, trace_context_make_registered_string_copy(
                                    context_, name.data(), name.length()));
  return true;
}

bool Importer::HandleIRQEnter(trace_ticks_t event_time,
                              trace_cpu_number_t cpu_number, uint32_t irq) {
  trace_thread_ref_t thread_ref = GetCpuPseudoThreadRef(cpu_number);
  if (!trace_is_unknown_thread_ref(&thread_ref)) {
    trace_string_ref_t name_ref = GetNameRef(irq_names_, "irq", irq);
    trace_context_write_duration_begin_event_record(
        context_, event_time, &thread_ref, &irq_category_ref_, &name_ref,
        nullptr, 0u);
  }
  return true;
}

bool Importer::HandleIRQExit(trace_ticks_t event_time,
                             trace_cpu_number_t cpu_number, uint32_t irq) {
  trace_thread_ref_t thread_ref = GetCpuPseudoThreadRef(cpu_number);
  if (!trace_is_unknown_thread_ref(&thread_ref)) {
    trace_string_ref_t name_ref = GetNameRef(irq_names_, "irq", irq);
    trace_context_write_duration_end_event_record(
        context_, event_time, &thread_ref, &irq_category_ref_, &name_ref,
        nullptr, 0u);
  }
  return true;
}

bool Importer::HandleSyscallEnter(trace_ticks_t event_time,
                                  trace_cpu_number_t cpu_number,
                                  uint32_t syscall) {
  zx_koid_t thread = GetCpuCurrentThread(cpu_number);
  if (thread != ZX_KOID_INVALID) {
    auto& duration = syscall_durations_[thread];
    if (duration.valid) {
      FXL_LOG(WARNING) << "Syscall duration for thread " << thread
                       << " already exists";
    }
    duration =
        SyscallDuration{.begin = event_time, .syscall = syscall, .valid = true};
  }
  return true;
}

bool Importer::HandleSyscallExit(trace_ticks_t event_time,
                                 trace_cpu_number_t cpu_number,
                                 uint32_t syscall) {
  zx_koid_t thread = GetCpuCurrentThread(cpu_number);
  if (thread != ZX_KOID_INVALID) {
    auto& duration = syscall_durations_[thread];
    if (!duration.valid) {
      // This is common as syscalls that start before tracing starts will not
      // have a corresponding SyscallEnter call and should be ignored.
      return false;
    }
    if (duration.syscall != syscall) {
      FXL_LOG(WARNING) << "Syscall end type on thread " << thread
                       << " does not match the begin type";
      return false;
    }
    trace_thread_ref_t thread_ref = GetThreadRef(thread);
    trace_string_ref_t name_ref =
        GetNameRef(syscall_names_, "syscall", syscall);
    trace_context_write_duration_event_record(
        context_, duration.begin, event_time, &thread_ref,
        &syscall_category_ref_, &name_ref, nullptr, 0u);
    duration.valid = false;
  }
  return true;
}

bool Importer::HandlePageFault(trace_ticks_t event_time,
                               trace_cpu_number_t cpu_number,
                               uint64_t virtual_address, uint32_t flags) {
  trace_thread_ref_t thread_ref = GetCpuCurrentThreadRef(cpu_number);
  if (!trace_is_unknown_thread_ref(&thread_ref)) {
    trace_arg_t args[] = {
        trace_make_arg(vaddr_name_ref_,
                       trace_make_pointer_arg_value(virtual_address)),
        trace_make_arg(flags_name_ref_, trace_make_uint32_arg_value(flags))};
    trace_context_write_instant_event_record(
        context_, event_time, &thread_ref, &irq_category_ref_,
        &page_fault_name_ref_, TRACE_SCOPE_THREAD, args, fbl::count_of(args));
  }
  return true;
}

bool Importer::HandleContextSwitch(
    trace_ticks_t event_time, trace_cpu_number_t cpu_number,
    trace_thread_state_t outgoing_thread_state,
    trace_thread_priority_t outgoing_thread_priority,
    trace_thread_priority_t incoming_thread_priority, zx_koid_t outgoing_thread,
    KernelThread outgoing_kernel_thread, zx_koid_t incoming_thread,
    KernelThread incoming_kernel_thread) {
  trace_thread_ref_t outgoing_thread_ref = GetCpuCurrentThreadRef(cpu_number);
  trace_thread_ref_t incoming_thread_ref =
      incoming_thread
          ? SwitchCpuToThread(cpu_number, incoming_thread)
          : SwitchCpuToKernelThread(cpu_number, incoming_kernel_thread);
  if (!trace_is_unknown_thread_ref(&outgoing_thread_ref) ||
      !trace_is_unknown_thread_ref(&incoming_thread_ref)) {
    trace_context_write_context_switch_record(
        context_, event_time, cpu_number, outgoing_thread_state,
        &outgoing_thread_ref, &incoming_thread_ref, outgoing_thread_priority,
        incoming_thread_priority);
  }
  return true;
}

bool Importer::HandleObjectDelete(trace_ticks_t event_time, zx_koid_t thread,
                                  zx_koid_t object) {
  auto it = channels_.ids_.find(object);
  if (it != channels_.ids_.end()) {
    channels_.message_counters_.erase(it->second);
  }

  return true;
}

bool Importer::HandleThreadCreate(trace_ticks_t event_time, zx_koid_t thread,
                                  zx_koid_t affected_thread,
                                  zx_koid_t affected_process) {
  return false;
}

bool Importer::HandleThreadStart(trace_ticks_t event_time, zx_koid_t thread,
                                 zx_koid_t affected_thread) {
  return false;
}

bool Importer::HandleThreadExit(trace_ticks_t event_time, zx_koid_t thread) {
  return false;
}

bool Importer::HandleProcessCreate(trace_ticks_t event_time, zx_koid_t thread,
                                   zx_koid_t affected_process) {
  return false;
}

bool Importer::HandleProcessStart(trace_ticks_t event_time, zx_koid_t thread,
                                  zx_koid_t affected_thread,
                                  zx_koid_t affected_process) {
  return false;
}

bool Importer::HandleProcessExit(trace_ticks_t event_time, zx_koid_t thread,
                                 zx_koid_t affected_process) {
  return false;
}

bool Importer::HandleChannelCreate(trace_ticks_t event_time, zx_koid_t thread,
                                   zx_koid_t channel0, zx_koid_t channel1,
                                   uint32_t flags) {
  if (channels_.ids_.count(channel0) != 0 ||
      channels_.ids_.count(channel1) != 0) {
    FXL_LOG(WARNING)
        << "Channel creation for an already known channel was requested, "
        << "ignoring the request.";
    return false;
  }

  channels_.ids_[channel0] = channels_.ids_[channel1] = channels_.next_id_++;
  return true;
}

bool Importer::HandleChannelWrite(trace_ticks_t event_time, zx_koid_t thread,
                                  zx_koid_t channel, uint32_t num_bytes,
                                  uint32_t num_handles) {
  auto it = channels_.ids_.find(channel);
  if (it == channels_.ids_.end())
    return false;

  auto counter = std::get<Channels::kWriteCounterIndex>(
      channels_.message_counters_[it->second])++;

  trace_thread_ref_t thread_ref = GetThreadRef(thread);
  trace_arg_t args[2] = {
      trace_make_arg(num_bytes_name_ref_,
                     trace_make_uint32_arg_value(num_bytes)),
      trace_make_arg(num_handles_name_ref_,
                     trace_make_uint32_arg_value(num_handles))};
  trace_context_write_flow_begin_event_record(
      context_, event_time, &thread_ref, &channel_category_ref_,
      &channel_write_name_ref_, counter, args, fbl::count_of(args));
  return true;
}

bool Importer::HandleChannelRead(trace_ticks_t event_time, zx_koid_t thread,
                                 zx_koid_t channel, uint32_t num_bytes,
                                 uint32_t num_handles) {
  auto it = channels_.ids_.find(channel);
  if (it == channels_.ids_.end())
    return false;

  auto counter = std::get<Channels::kReadCounterIndex>(
      channels_.message_counters_[it->second])++;

  trace_thread_ref_t thread_ref = GetThreadRef(thread);
  trace_arg_t args[2] = {
      trace_make_arg(num_bytes_name_ref_,
                     trace_make_uint32_arg_value(num_bytes)),
      trace_make_arg(num_handles_name_ref_,
                     trace_make_uint32_arg_value(num_handles))};
  trace_context_write_flow_end_event_record(
      context_, event_time, &thread_ref, &channel_category_ref_,
      &channel_read_name_ref_, counter, args, fbl::count_of(args));
  return true;
}

bool Importer::HandlePortWait(trace_ticks_t event_time, zx_koid_t thread,
                              zx_koid_t port) {
  return false;
}

bool Importer::HandlePortWaitDone(trace_ticks_t event_time, zx_koid_t thread,
                                  zx_koid_t port, uint32_t status) {
  return false;
}

bool Importer::HandlePortCreate(trace_ticks_t event_time, zx_koid_t thread,
                                zx_koid_t port) {
  return false;
}

bool Importer::HandlePortQueue(trace_ticks_t event_time, zx_koid_t thread,
                               zx_koid_t port, uint32_t num_bytes) {
  return false;
}

bool Importer::HandleWaitOne(trace_ticks_t event_time, zx_koid_t thread,
                             zx_koid_t object, uint32_t signals,
                             zx_time_t timeout) {
  return false;
}

bool Importer::HandleWaitOneDone(trace_ticks_t event_time, zx_koid_t thread,
                                 zx_koid_t object, uint32_t status,
                                 uint32_t pending) {
  return false;
}

bool Importer::HandleProbe(trace_ticks_t event_time, zx_koid_t thread,
                           uint32_t event_name_id, bool cpu_trace) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(thread) : GetThreadRef(thread);
  trace_string_ref_t name_ref =
      GetNameRef(probe_names_, "probe", event_name_id);
  trace_context_write_instant_event_record(context_, event_time, &thread_ref,
                                           &probe_category_ref_, &name_ref,
                                           TRACE_SCOPE_THREAD, nullptr, 0u);
  return true;
}

bool Importer::HandleProbe(trace_ticks_t event_time, zx_koid_t thread,
                           uint32_t event_name_id, bool cpu_trace,
                           uint32_t arg0, uint32_t arg1) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(thread) : GetThreadRef(thread);
  trace_string_ref_t name_ref =
      GetNameRef(probe_names_, "probe", event_name_id);
  trace_arg_t args[] = {
      trace_make_arg(arg0_name_ref_, trace_make_uint32_arg_value(arg0)),
      trace_make_arg(arg1_name_ref_, trace_make_uint32_arg_value(arg1))};
  trace_context_write_instant_event_record(
      context_, event_time, &thread_ref, &probe_category_ref_, &name_ref,
      TRACE_SCOPE_THREAD, args, fbl::count_of(args));
  return true;
}

bool Importer::HandleProbe(trace_ticks_t event_time, zx_koid_t thread,
                           uint32_t event_name_id, bool cpu_trace,
                           uint64_t arg0, uint64_t arg1) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(thread) : GetThreadRef(thread);
  trace_string_ref_t name_ref =
      GetNameRef(probe_names_, "probe", event_name_id);
  trace_arg_t args[] = {
      trace_make_arg(arg0_name_ref_, trace_make_uint64_arg_value(arg0)),
      trace_make_arg(arg1_name_ref_, trace_make_uint64_arg_value(arg1))};
  trace_context_write_instant_event_record(
      context_, event_time, &thread_ref, &probe_category_ref_, &name_ref,
      TRACE_SCOPE_THREAD, args, fbl::count_of(args));
  return true;
}

bool Importer::HandleVcpuEnter(trace_ticks_t event_time, zx_koid_t thread) {
  auto& duration = vcpu_durations_[thread];
  if (duration.valid) {
    FXL_LOG(WARNING) << "VCPU duration for thread " << thread
                     << " already exists";
    return false;
  }
  duration = VcpuDuration{.begin = event_time, .valid = true};
  return true;
}

bool Importer::HandleVcpuExit(trace_ticks_t event_time, zx_koid_t thread,
                              uint32_t exit, uint64_t exit_addr) {
  auto& duration = vcpu_durations_[thread];
  trace_arg_t args[] = {
      trace_make_arg(exit_address_name_ref_,
                     trace_make_pointer_arg_value(exit_addr)),
  };
  if (!duration.valid) {
    FXL_LOG(WARNING) << "VCPU duration for thread " << thread
                     << " does not have a beginning";
    return false;
  }

  trace_thread_ref_t thread_ref = GetThreadRef(thread);
  trace_string_ref_t name_ref = GetNameRef(vcpu_exit_meta_, "exit", exit);
  trace_context_write_duration_event_record(
      context_, duration.begin, event_time, &thread_ref, &vcpu_category_ref_,
      &name_ref, args, fbl::count_of(args));

  duration.valid = false;
  return true;
}

bool Importer::HandleVcpuBlock(trace_ticks_t event_time, zx_koid_t thread,
                               uint32_t meta) {
  trace_thread_ref_t thread_ref = GetThreadRef(thread);
  trace_string_ref_t name_ref = GetNameRef(vcpu_meta_, "meta", meta);
  trace_context_write_duration_begin_event_record(
      context_, event_time, &thread_ref, &vcpu_category_ref_, &name_ref,
      nullptr, 0u);
  return true;
}

bool Importer::HandleVcpuUnblock(trace_ticks_t event_time, zx_koid_t thread,
                                 uint32_t meta) {
  trace_thread_ref_t thread_ref = GetThreadRef(thread);
  trace_string_ref_t name_ref = GetNameRef(vcpu_meta_, "meta", meta);
  trace_context_write_duration_end_event_record(
      context_, event_time, &thread_ref, &vcpu_category_ref_, &name_ref,
      nullptr, 0u);
  return true;
}

bool Importer::HandleDurationBegin(trace_ticks_t event_time, zx_koid_t thread,
                                   uint32_t event_name_id, uint32_t group,
                                   bool cpu_trace) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(thread) : GetThreadRef(thread);
  trace_string_ref_t name_ref =
      GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_duration_begin_event_record(
      context_, event_time, &thread_ref, &category_ref, &name_ref, nullptr, 0u);

  return true;
}

bool Importer::HandleDurationBegin(trace_ticks_t event_time, zx_koid_t thread,
                                   uint32_t event_name_id, uint32_t group,
                                   bool cpu_trace, uint64_t arg0,
                                   uint64_t arg1) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(thread) : GetThreadRef(thread);
  trace_arg_t args[] = {
      trace_make_arg(arg0_name_ref_, trace_make_uint64_arg_value(arg0)),
      trace_make_arg(arg1_name_ref_, trace_make_uint64_arg_value(arg1))};
  trace_string_ref_t name_ref =
      GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_duration_begin_event_record(
      context_, event_time, &thread_ref, &category_ref, &name_ref, args,
      fbl::count_of(args));

  return true;
}

bool Importer::HandleDurationEnd(trace_ticks_t event_time, zx_koid_t thread,
                                 uint32_t event_name_id, uint32_t group,
                                 bool cpu_trace) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(thread) : GetThreadRef(thread);
  trace_string_ref_t name_ref =
      GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_duration_end_event_record(
      context_, event_time, &thread_ref, &category_ref, &name_ref, nullptr, 0u);

  return true;
}

bool Importer::HandleDurationEnd(trace_ticks_t event_time, zx_koid_t thread,
                                 uint32_t event_name_id, uint32_t group,
                                 bool cpu_trace, uint64_t arg0, uint64_t arg1) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(thread) : GetThreadRef(thread);
  trace_arg_t args[] = {
      trace_make_arg(arg0_name_ref_, trace_make_uint64_arg_value(arg0)),
      trace_make_arg(arg1_name_ref_, trace_make_uint64_arg_value(arg1))};
  trace_string_ref_t name_ref =
      GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_duration_end_event_record(
      context_, event_time, &thread_ref, &category_ref, &name_ref, args,
      fbl::count_of(args));

  return true;
}

bool Importer::HandleFlowBegin(trace_ticks_t event_time, zx_koid_t thread,
                               uint32_t event_name_id, uint32_t group,
                               bool cpu_trace, trace_flow_id_t flow_id) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(thread) : GetThreadRef(thread);
  trace_string_ref_t name_ref =
      GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_flow_begin_event_record(context_, event_time, &thread_ref,
                                              &category_ref, &name_ref, flow_id,
                                              nullptr, 0u);

  return true;
}

bool Importer::HandleFlowEnd(trace_ticks_t event_time, zx_koid_t thread,
                             uint32_t event_name_id, uint32_t group,
                             bool cpu_trace, trace_flow_id_t flow_id) {
  trace_thread_ref_t thread_ref =
      cpu_trace ? GetCpuPseudoThreadRef(thread) : GetThreadRef(thread);
  trace_string_ref_t name_ref =
      GetNameRef(probe_names_, "probe", event_name_id);
  trace_string_ref_t category_ref = GetCategoryForGroup(group);
  trace_context_write_flow_end_event_record(context_, event_time, &thread_ref,
                                            &category_ref, &name_ref, flow_id,
                                            nullptr, 0u);

  return true;
}

trace_thread_ref_t Importer::GetCpuCurrentThreadRef(
    trace_cpu_number_t cpu_number) {
  if (cpu_number >= cpu_infos_.size())
    return kUnknownThreadRef;
  return cpu_infos_[cpu_number].current_thread_ref;
}

zx_koid_t Importer::GetCpuCurrentThread(trace_cpu_number_t cpu_number) {
  if (cpu_number >= cpu_infos_.size())
    return ZX_KOID_INVALID;
  return cpu_infos_[cpu_number].current_thread;
}

trace_thread_ref_t Importer::SwitchCpuToThread(trace_cpu_number_t cpu_number,
                                               zx_koid_t thread) {
  if (cpu_number >= cpu_infos_.size())
    cpu_infos_.resize(cpu_number + 1u);
  cpu_infos_[cpu_number].current_thread = thread;
  return cpu_infos_[cpu_number].current_thread_ref = GetThreadRef(thread);
}

trace_thread_ref_t Importer::SwitchCpuToKernelThread(
    trace_cpu_number_t cpu_number, KernelThread kernel_thread) {
  if (cpu_number >= cpu_infos_.size())
    cpu_infos_.resize(cpu_number + 1u);
  cpu_infos_[cpu_number].current_thread = kernel_thread;
  return cpu_infos_[cpu_number].current_thread_ref =
             GetKernelThreadRef(kernel_thread);
}

const trace_string_ref_t& Importer::GetNameRef(
    std::unordered_map<uint32_t, trace_string_ref_t>& table, const char* kind,
    uint32_t id) {
  auto it = table.find(id);
  if (it == table.end()) {
    fbl::String name = fbl::StringPrintf("%s %#x", kind, id);
    std::tie(it, std::ignore) =
        table.emplace(id, trace_context_make_registered_string_copy(
                              context_, name.data(), name.length()));
  }
  return it->second;
}

const trace_thread_ref_t& Importer::GetThreadRef(zx_koid_t thread) {
  // |trace_make_inline_thread_ref()| requires a valid thread id (given that
  // we're using ZX_KOID_INVALID for the process for unknown threads).
  if (thread == ZX_KOID_INVALID) {
    return kUnknownThreadRef;
  }
  auto it = thread_refs_.find(thread);
  if (it == thread_refs_.end()) {
    std::tie(it, std::ignore) = thread_refs_.emplace(
        thread, trace_make_inline_thread_ref(kNoProcess, thread));
  }
  return it->second;
}

// TODO(TO-106): Revisit using pseudo thread references to support per-CPU
// events.
const trace_thread_ref_t& Importer::GetCpuPseudoThreadRef(
    trace_cpu_number_t cpu) {
  const zx_koid_t thread = kKernelPseudoCpuBase + cpu;
  auto it = thread_refs_.find(thread);
  if (it == thread_refs_.end()) {
    fbl::String label = fbl::StringPrintf("cpu-%d", cpu);

    trace_string_ref name_ref =
        trace_make_inline_string_ref(label.data(), label.length());
    trace_context_write_thread_info_record(context_, kNoProcess, thread,
                                           &name_ref);
    std::tie(it, std::ignore) = thread_refs_.emplace(
        thread,
        trace_context_make_registered_thread(context_, kNoProcess, thread));
  }
  return it->second;
}

const trace_thread_ref_t& Importer::GetKernelThreadRef(
    KernelThread kernel_thread) {
  auto it = kernel_thread_refs_.find(kernel_thread);
  if (it == kernel_thread_refs_.end()) {
    std::tie(it, std::ignore) = kernel_thread_refs_.emplace(
        kernel_thread, trace_make_inline_thread_ref(
                           kNoProcess, kKernelThreadFlag | kernel_thread));
  }
  return it->second;
}

const trace_string_ref_t& Importer::GetCategoryForGroup(uint32_t group) {
  switch (group) {
    case KTRACE_GRP_META:
      return meta_category_ref_;
    case KTRACE_GRP_LIFECYCLE:
      return lifecycle_category_ref_;
    case KTRACE_GRP_SCHEDULER:
      return sched_category_ref_;
    case KTRACE_GRP_TASKS:
      return tasks_category_ref_;
    case KTRACE_GRP_IPC:
      return ipc_category_ref_;
    case KTRACE_GRP_IRQ:
      return irq_category_ref_;
    case KTRACE_GRP_PROBE:
      return probe_category_ref_;
    case KTRACE_GRP_ARCH:
      return arch_category_ref_;
    default:
      return unknown_category_ref_;
  }
}

}  // namespace ktrace_provider
