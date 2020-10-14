// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_KTRACE_PROVIDER_IMPORTER_H_
#define GARNET_BIN_KTRACE_PROVIDER_IMPORTER_H_

#include <lib/trace-engine/context.h>
#include <stddef.h>
#include <stdint.h>

#include <array>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <fbl/string.h>
#include <fbl/string_piece.h>

#include "garnet/bin/ktrace_provider/reader.h"
#include "garnet/bin/ktrace_provider/tags.h"

namespace ktrace_provider {

class Reader;

class Importer {
 public:
  static constexpr zx_koid_t kNoProcess = 0u;
  static constexpr zx_koid_t kKernelThreadFlag = 0x100000000;

  static constexpr zx_koid_t kKernelPseudoKoidBase = 0x00000000'70000000u;
  static constexpr zx_koid_t kKernelPseudoCpuBase = kKernelPseudoKoidBase + 0x00000000'01000000u;

  Importer(trace_context* context);
  ~Importer();

  bool Import(Reader& reader);

 private:
  using KernelThread = uint32_t;

  bool ImportRecord(const ktrace_header_t* record, size_t record_size);
  bool ImportBasicRecord(const ktrace_header_t* record, const TagInfo& tag_info);
  bool ImportQuadRecord(const ktrace_rec_32b_t* record, const TagInfo& tag_info);
  bool ImportNameRecord(const ktrace_rec_name_t* record, const TagInfo& tag_info);
  bool ImportProbeRecord(const ktrace_header_t* record, size_t record_size);
  bool ImportDurationRecord(const ktrace_header_t* record, size_t record_size);
  bool ImportFlowRecord(const ktrace_header_t* record, size_t record_size);
  bool ImportCounterRecord(const ktrace_header_t* record, size_t record_size);
  bool ImportUnknownRecord(const ktrace_header_t* record, size_t record_size);

  bool HandleKernelThreadName(KernelThread kernel_thread, const fbl::StringPiece& name);
  bool HandleThreadName(zx_koid_t thread, zx_koid_t process, const fbl::StringPiece& name);
  bool HandleProcessName(zx_koid_t process, const fbl::StringPiece& name);
  bool HandleSyscallName(uint32_t syscall, const fbl::StringPiece& name);
  bool HandleIRQName(uint32_t irq, const fbl::StringPiece& name);
  bool HandleProbeName(uint32_t probe, const fbl::StringPiece& name);
  bool HandleVcpuMeta(uint32_t meta, const fbl::StringPiece& name);
  bool HandleVcpuExitMeta(uint32_t exit, const fbl::StringPiece& name);

  bool HandleIRQEnter(trace_ticks_t event_time, trace_cpu_number_t cpu_number, uint32_t irq);
  bool HandleIRQExit(trace_ticks_t event_time, trace_cpu_number_t cpu_number, uint32_t irq);
  bool HandleSyscallEnter(trace_ticks_t event_time, trace_cpu_number_t cpu_number,
                          uint32_t syscall);
  bool HandleSyscallExit(trace_ticks_t event_time, trace_cpu_number_t cpu_number, uint32_t syscall);
  bool HandlePageFaultEnter(trace_ticks_t event_time, trace_cpu_number_t cpu_number,
                            uint64_t virtual_address, uint32_t flags);
  bool HandlePageFaultExit(trace_ticks_t event_time, trace_cpu_number_t cpu_number,
                           uint64_t virtual_address, uint32_t flags);
  bool HandleContextSwitch(trace_ticks_t event_time, trace_cpu_number_t cpu_number,
                           trace_thread_state_t outgoing_thread_state,
                           trace_thread_priority_t outgoing_thread_priority,
                           trace_thread_priority_t incoming_thread_priority,
                           zx_koid_t outgoing_thread, KernelThread outgoing_kernel_thread,
                           zx_koid_t incoming_thread, KernelThread incoming_kernel_thread);
  bool HandleInheritPriorityStart(trace_ticks_t event_time, uint32_t id,
                                  trace_cpu_number_t cpu_number);
  bool HandleInheritPriority(trace_ticks_t event_time, uint32_t id, uint32_t tid, uint32_t flags,
                             int old_inherited_prio, int new_inherited_prio, int old_effective_prio,
                             int new_effective_prio);
  bool HandleFutexWait(trace_ticks_t event_time, uint64_t futex_id, uint32_t new_owner_tid,
                       trace_cpu_number_t cpu_number);
  bool HandleFutexWoke(trace_ticks_t event_time, uint64_t futex_id, zx_status_t wait_result,
                       trace_cpu_number_t cpu_number);
  bool HandleFutexWake(trace_ticks_t event_time, uint64_t futex_id, uint32_t new_owner_tid,
                       uint32_t count, uint32_t flags, trace_cpu_number_t cpu_number);
  bool HandleFutexRequeue(trace_ticks_t event_time, uint64_t futex_id, uint32_t new_owner_tid,
                          uint32_t count, uint32_t flags, trace_cpu_number_t cpu_number);
  bool HandleKernelMutexEvent(trace_ticks_t event_time, uint32_t which_event, uint32_t mutex_id,
                              uint32_t tid, uint32_t waiter_count, uint32_t flags,
                              trace_cpu_number_t cpu_number);
  bool HandleObjectDelete(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t object);
  bool HandleThreadCreate(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t affected_thread,
                          zx_koid_t affected_process);
  bool HandleThreadStart(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t affected_thread);
  bool HandleThreadExit(trace_ticks_t event_time, zx_koid_t thread);
  bool HandleProcessCreate(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t affected_process);
  bool HandleProcessStart(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t affected_thread,
                          zx_koid_t affected_process);
  bool HandleProcessExit(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t affected_process);
  bool HandleChannelCreate(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t channel0,
                           zx_koid_t channel1, uint32_t flags);
  bool HandleChannelWrite(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t channel,
                          uint32_t num_bytes, uint32_t num_handles);
  bool HandleChannelRead(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t channel,
                         uint32_t num_bytes, uint32_t num_handles);
  bool HandlePortWait(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t port);
  bool HandlePortWaitDone(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t port,
                          uint32_t status);
  bool HandlePortCreate(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t port);
  bool HandlePortQueue(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t port,
                       uint32_t num_bytes);
  bool HandleWaitOne(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t object, uint32_t signals,
                     zx_time_t timeout);
  bool HandleWaitOneDone(trace_ticks_t event_time, zx_koid_t thread, zx_koid_t object,
                         uint32_t status, uint32_t pending);
  bool HandleProbe(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                   bool cpu_trace);
  bool HandleProbe(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                   bool cpu_trace, uint32_t arg0, uint32_t arg1);
  bool HandleProbe(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                   bool cpu_trace, uint64_t arg0, uint64_t arg1);
  bool HandleDurationBegin(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                           uint32_t group, bool cpu_trace);
  bool HandleDurationBegin(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                           uint32_t group, bool cpu_trace, uint64_t arg0, uint64_t arg1);
  bool HandleDurationEnd(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                         uint32_t group, bool cpu_trace);
  bool HandleDurationEnd(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                         uint32_t group, bool cpu_trace, uint64_t arg0, uint64_t arg1);
  bool HandleFlowBegin(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                       uint32_t group, bool cpu_trace, trace_flow_id_t flow_id);
  bool HandleFlowEnd(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                     uint32_t group, bool cpu_trace, trace_flow_id_t flow_id);
  bool HandleFlowStep(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                      uint32_t group, bool cpu_trace, trace_flow_id_t flow_id);
  bool HandleCounter(trace_ticks_t event_time, zx_koid_t thread, uint32_t event_name_id,
                     uint32_t group, bool cpu_trace, trace_counter_id_t counter_id, int64_t value);
  bool HandleVcpuEnter(trace_ticks_t event_time, zx_koid_t thread);
  bool HandleVcpuExit(trace_ticks_t event_time, zx_koid_t thread, uint32_t exit,
                      uint64_t exit_address);
  bool HandleVcpuBlock(trace_ticks_t event_time, zx_koid_t thread, uint32_t meta);
  bool HandleVcpuUnblock(trace_ticks_t event_time, zx_koid_t thread, uint32_t meta);

  struct CpuInfo {
    zx_koid_t current_thread = ZX_KOID_INVALID;
    trace_thread_ref_t current_thread_ref = trace_make_unknown_thread_ref();
  };

  trace_thread_ref_t GetCpuCurrentThreadRef(trace_cpu_number_t cpu_number);
  zx_koid_t GetCpuCurrentThread(trace_cpu_number_t cpu_number);
  trace_thread_ref_t SwitchCpuToThread(trace_cpu_number_t cpu_number, zx_koid_t thread);
  trace_thread_ref_t SwitchCpuToKernelThread(trace_cpu_number_t cpu_number,
                                             KernelThread kernel_thread);

  const trace_string_ref_t& GetNameRef(std::unordered_map<uint32_t, trace_string_ref_t>& table,
                                       const char* kind, uint32_t id);
  const trace_thread_ref_t& GetThreadRef(zx_koid_t thread);
  const trace_thread_ref_t& GetKernelThreadRef(KernelThread kernel_thread);
  const trace_thread_ref_t& GetCpuPseudoThreadRef(trace_cpu_number_t cpu);

  const trace_string_ref_t& GetCategoryForGroup(uint32_t group);

  trace_context_t* const context_;
  const TagMap& tags_;

  trace_string_ref_t const kernel_string_ref_;
  trace_string_ref_t const unknown_category_ref_;
  trace_string_ref_t const arch_category_ref_;
  trace_string_ref_t const meta_category_ref_;
  trace_string_ref_t const lifecycle_category_ref_;
  trace_string_ref_t const tasks_category_ref_;
  trace_string_ref_t const ipc_category_ref_;
  trace_string_ref_t const irq_category_ref_;
  trace_string_ref_t const probe_category_ref_;
  trace_string_ref_t const sched_category_ref_;
  trace_string_ref_t const syscall_category_ref_;
  trace_string_ref_t const channel_category_ref_;
  trace_string_ref_t const vcpu_category_ref_;
  trace_string_ref_t const vm_category_ref_;
  trace_string_ref_t const channel_read_name_ref_;
  trace_string_ref_t const channel_write_name_ref_;
  trace_string_ref_t const num_bytes_name_ref_;
  trace_string_ref_t const num_handles_name_ref_;
  trace_string_ref_t const page_fault_name_ref_;
  trace_string_ref_t const vaddr_name_ref_;
  trace_string_ref_t const flags_name_ref_;
  trace_string_ref_t const exit_address_name_ref_;
  trace_string_ref_t const arg0_name_ref_;
  trace_string_ref_t const arg1_name_ref_;
  trace_string_ref_t const inherit_prio_name_ref_;
  trace_string_ref_t const inherit_prio_old_ip_name_ref_;
  trace_string_ref_t const inherit_prio_new_ip_name_ref_;
  trace_string_ref_t const inherit_prio_old_ep_name_ref_;
  trace_string_ref_t const inherit_prio_new_ep_name_ref_;
  trace_string_ref_t const futex_wait_name_ref_;
  trace_string_ref_t const futex_woke_name_ref_;
  trace_string_ref_t const futex_wake_name_ref_;
  trace_string_ref_t const futex_requeue_name_ref_;
  trace_string_ref_t const futex_id_name_ref_;
  trace_string_ref_t const futex_owner_name_ref_;
  trace_string_ref_t const futex_wait_res_name_ref_;
  trace_string_ref_t const futex_count_name_ref_;
  trace_string_ref_t const futex_was_requeue_name_ref_;
  trace_string_ref_t const futex_was_active_name_ref_;
  trace_string_ref_t const kernel_mutex_acquire_name_ref_;
  trace_string_ref_t const kernel_mutex_block_name_ref_;
  trace_string_ref_t const kernel_mutex_release_name_ref_;
  trace_string_ref_t const kernel_mutex_mutex_id_name_ref_;
  trace_string_ref_t const kernel_mutex_tid_name_ref_;
  trace_string_ref_t const kernel_mutex_tid_type_ref_;
  trace_string_ref_t const kernel_mutex_tid_type_user_ref_;
  trace_string_ref_t const kernel_mutex_tid_type_kernel_ref_;
  trace_string_ref_t const kernel_mutex_tid_type_none_ref_;
  trace_string_ref_t const kernel_mutex_waiter_count_name_ref_;
  trace_string_ref_t const misc_unknown_name_ref_;

  uint32_t version_ = 0u;

  std::vector<CpuInfo> cpu_infos_;

  std::unordered_map<KernelThread, trace_thread_ref_t> kernel_thread_refs_;
  std::unordered_map<zx_koid_t, trace_thread_ref_t> thread_refs_;

  const trace_thread_ref_t kUnknownThreadRef;

  struct VcpuDuration {
    trace_ticks_t begin;
    bool valid = false;
  };
  std::unordered_map<zx_koid_t, VcpuDuration> vcpu_durations_;

  struct SyscallDuration {
    trace_ticks_t begin;
    uint32_t syscall;
    bool valid = false;
  };
  std::unordered_map<zx_koid_t, SyscallDuration> syscall_durations_;

  std::unordered_map<uint32_t, trace_string_ref_t> irq_names_;
  std::unordered_map<uint32_t, trace_string_ref_t> probe_names_;
  std::unordered_map<uint32_t, trace_string_ref_t> syscall_names_;
  std::unordered_map<uint32_t, trace_string_ref_t> vcpu_meta_;
  std::unordered_map<uint32_t, trace_string_ref_t> vcpu_exit_meta_;

  struct Channels {
    using ChannelId = uint64_t;
    using MessageCounter = uint64_t;

    static constexpr size_t kReadCounterIndex = 0;
    static constexpr size_t kWriteCounterIndex = 1;

    ChannelId next_id_ = 0;
    std::unordered_map<zx_koid_t, ChannelId> ids_;
    std::unordered_map<ChannelId, std::tuple<MessageCounter, MessageCounter>> message_counters_;
  } channels_;

  Importer(const Importer&) = delete;
  Importer(Importer&&) = delete;
  Importer& operator=(const Importer&) = delete;
  Importer& operator=(Importer&&) = delete;
};

}  // namespace ktrace_provider

#endif  // GARNET_BIN_KTRACE_PROVIDER_IMPORTER_H_
