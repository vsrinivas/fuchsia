// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_KTRACE_PROVIDER_IMPORTER_H_
#define APPS_TRACING_SRC_KTRACE_PROVIDER_IMPORTER_H_

#include "stddef.h"
#include "stdint.h"

#include <tuple>
#include <unordered_map>
#include <vector>

#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <trace-engine/context.h>

#include "apps/tracing/src/ktrace_provider/tags.h"
#include "lib/fxl/macros.h"

namespace ktrace_provider {

class Reader;

class Importer {
 public:
  Importer(trace_context* context);
  ~Importer();

  bool Import(Reader& reader);

 private:
  using KernelThread = uint32_t;

  bool ImportRecord(const ktrace_header_t* record, size_t record_size);
  bool ImportBasicRecord(const ktrace_header_t* record,
                         const TagInfo& tag_info);
  bool ImportQuadRecord(const ktrace_rec_32b_t* record,
                        const TagInfo& tag_info);
  bool ImportNameRecord(const ktrace_rec_name_t* record,
                        const TagInfo& tag_info);
  bool ImportProbeRecord(const ktrace_header_t* record, size_t record_size);
  bool ImportUnknownRecord(const ktrace_header_t* record, size_t record_size);

  bool HandleKernelThreadName(KernelThread kernel_thread,
                              const fbl::StringPiece& name);
  bool HandleThreadName(mx_koid_t thread,
                        mx_koid_t process,
                        const fbl::StringPiece& name);
  bool HandleProcessName(mx_koid_t process, const fbl::StringPiece& name);
  bool HandleSyscallName(uint32_t syscall, const fbl::StringPiece& name);
  bool HandleIRQName(uint32_t irq, const fbl::StringPiece& name);
  bool HandleProbeName(uint32_t probe, const fbl::StringPiece& name);

  bool HandleIRQEnter(trace_ticks_t event_time,
                      trace_cpu_number_t cpu_number,
                      uint32_t irq);
  bool HandleIRQExit(trace_ticks_t event_time,
                     trace_cpu_number_t cpu_number,
                     uint32_t irq);
  bool HandleSyscallEnter(trace_ticks_t event_time,
                          trace_cpu_number_t cpu_number,
                          uint32_t syscall);
  bool HandleSyscallExit(trace_ticks_t event_time,
                         trace_cpu_number_t cpu_number,
                         uint32_t syscall);
  bool HandlePageFault(trace_ticks_t event_time,
                       trace_cpu_number_t cpu_number,
                       uint64_t virtual_address,
                       uint32_t flags);
  bool HandleContextSwitch(trace_ticks_t event_time,
                           trace_cpu_number_t cpu_number,
                           trace_thread_state_t outgoing_thread_state,
                           mx_koid_t outgoing_thread,
                           KernelThread outgoing_kernel_thread,
                           mx_koid_t incoming_thread,
                           KernelThread incoming_kernel_thread);
  bool HandleObjectDelete(trace_ticks_t event_time,
                          mx_koid_t thread,
                          mx_koid_t object);
  bool HandleThreadCreate(trace_ticks_t event_time,
                          mx_koid_t thread,
                          mx_koid_t affected_thread,
                          mx_koid_t affected_process);
  bool HandleThreadStart(trace_ticks_t event_time,
                         mx_koid_t thread,
                         mx_koid_t affected_thread);
  bool HandleThreadExit(trace_ticks_t event_time, mx_koid_t thread);
  bool HandleProcessCreate(trace_ticks_t event_time,
                           mx_koid_t thread,
                           mx_koid_t affected_process);
  bool HandleProcessStart(trace_ticks_t event_time,
                          mx_koid_t thread,
                          mx_koid_t affected_thread,
                          mx_koid_t affected_process);
  bool HandleProcessExit(trace_ticks_t event_time,
                         mx_koid_t thread,
                         mx_koid_t affected_process);
  bool HandleChannelCreate(trace_ticks_t event_time,
                           mx_koid_t thread,
                           mx_koid_t channel0,
                           mx_koid_t channel1,
                           uint32_t flags);
  bool HandleChannelWrite(trace_ticks_t event_time,
                          mx_koid_t thread,
                          mx_koid_t channel,
                          uint32_t num_bytes,
                          uint32_t num_handles);
  bool HandleChannelRead(trace_ticks_t event_time,
                         mx_koid_t thread,
                         mx_koid_t channel,
                         uint32_t num_bytes,
                         uint32_t num_handles);
  bool HandlePortWait(trace_ticks_t event_time,
                      mx_koid_t thread,
                      mx_koid_t port);
  bool HandlePortWaitDone(trace_ticks_t event_time,
                          mx_koid_t thread,
                          mx_koid_t port,
                          uint32_t status);
  bool HandlePortCreate(trace_ticks_t event_time,
                        mx_koid_t thread,
                        mx_koid_t port);
  bool HandlePortQueue(trace_ticks_t event_time,
                       mx_koid_t thread,
                       mx_koid_t port,
                       uint32_t num_bytes);
  bool HandleWaitOne(trace_ticks_t event_time,
                     mx_koid_t thread,
                     mx_koid_t object,
                     uint32_t signals,
                     mx_time_t timeout);
  bool HandleWaitOneDone(trace_ticks_t event_time,
                         mx_koid_t thread,
                         mx_koid_t object,
                         uint32_t status,
                         uint32_t pending);
  bool HandleProbe(trace_ticks_t event_time, mx_koid_t thread, uint32_t probe);
  bool HandleProbe(trace_ticks_t event_time,
                   mx_koid_t thread,
                   uint32_t probe,
                   uint32_t arg0,
                   uint32_t arg1);

  struct CpuInfo {
    trace_thread_ref_t current_thread_ref = trace_make_unknown_thread_ref();
  };

  trace_thread_ref_t GetCpuCurrentThread(trace_cpu_number_t cpu_number);
  trace_thread_ref_t SwitchCpuToThread(trace_cpu_number_t cpu_number,
                                       mx_koid_t thread);
  trace_thread_ref_t SwitchCpuToKernelThread(trace_cpu_number_t cpu_number,
                                             KernelThread kernel_thread);

  const trace_string_ref_t& GetNameRef(
      std::unordered_map<uint32_t, trace_string_ref_t>& table,
      const char* kind,
      uint32_t id);
  const trace_thread_ref_t& GetThreadRef(mx_koid_t thread);
  const trace_thread_ref_t& GetKernelThreadRef(KernelThread kernel_thread);

  trace_context_t* const context_;
  const TagMap& tags_;

  trace_string_ref_t const kernel_string_ref_;
  trace_string_ref_t const ipc_category_ref_;
  trace_string_ref_t const irq_category_ref_;
  trace_string_ref_t const probe_category_ref_;
  trace_string_ref_t const syscall_category_ref_;
  trace_string_ref_t const channel_category_ref_;
  trace_string_ref_t const channel_read_name_ref_;
  trace_string_ref_t const channel_write_name_ref_;
  trace_string_ref_t const num_bytes_name_ref_;
  trace_string_ref_t const num_handles_name_ref_;
  trace_string_ref_t const page_fault_name_ref_;
  trace_string_ref_t const vaddr_name_ref_;
  trace_string_ref_t const flags_name_ref_;
  trace_string_ref_t const arg0_name_ref_;
  trace_string_ref_t const arg1_name_ref_;

  uint32_t version_ = 0u;

  std::vector<CpuInfo> cpu_infos_;

  std::unordered_map<KernelThread, trace_thread_ref_t> kernel_thread_refs_;
  std::unordered_map<mx_koid_t, trace_thread_ref_t> thread_refs_;

  std::unordered_map<uint32_t, trace_string_ref_t> irq_names_;
  std::unordered_map<uint32_t, trace_string_ref_t> probe_names_;
  std::unordered_map<uint32_t, trace_string_ref_t> syscall_names_;

  struct Channels {
    using ChannelId = uint64_t;
    using MessageCounter = uint64_t;

    static constexpr size_t kReadCounterIndex = 0;
    static constexpr size_t kWriteCounterIndex = 1;

    ChannelId next_id_ = 0;
    std::unordered_map<mx_koid_t, ChannelId> ids_;
    std::unordered_map<ChannelId, std::tuple<MessageCounter, MessageCounter>>
        message_counters_;
  } channels_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Importer);
};

}  // namespace ktrace_provider

#endif  // APPS_TRACING_SRC_KTRACE_PROVIDER_IMPORTER_H_
