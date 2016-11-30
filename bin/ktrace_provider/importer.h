// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_KTRACE_PROVIDER_IMPORTER_H_
#define APPS_TRACING_SRC_KTRACE_PROVIDER_IMPORTER_H_

#include "stddef.h"
#include "stdint.h"

#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "apps/tracing/lib/trace/writer.h"
#include "apps/tracing/src/ktrace_provider/tags.h"
#include "lib/ftl/macros.h"

namespace ktrace_provider {

class Importer {
 public:
  using TraceWriter = tracing::writer::TraceWriter;
  using StringRef = tracing::writer::StringRef;
  using ThreadRef = tracing::writer::ThreadRef;
  using ThreadState = tracing::ThreadState;
  using Ticks = tracing::Ticks;
  using CpuNumber = tracing::CpuNumber;
  using KernelThread = uint32_t;

  Importer(TraceWriter& writer);
  ~Importer();

  bool Import(const uint8_t* buffer, size_t size);

 private:
  bool ImportRecord(const ktrace_header_t* record, size_t record_size);
  bool ImportBasicRecord(const ktrace_header_t* record,
                         const TagInfo& tag_info);
  bool ImportQuadRecord(const ktrace_rec_32b_t* record,
                        const TagInfo& tag_info);
  bool ImportNameRecord(const ktrace_rec_name_t* record,
                        const TagInfo& tag_info);
  bool ImportProbeRecord(const ktrace_header_t* record, size_t record_size);
  bool ImportUnknownRecord(const ktrace_header_t* record, size_t record_size);

  bool HandleKernelThreadName(KernelThread kernel_thread, std::string name);
  bool HandleThreadName(mx_koid_t thread, mx_koid_t process, std::string name);
  bool HandleProcessName(mx_koid_t process, std::string name);
  bool HandleSyscallName(uint32_t syscall, std::string name);
  bool HandleIRQName(uint32_t irq, std::string name);
  bool HandleProbeName(uint32_t probe, std::string name);

  bool HandleIRQEnter(Ticks event_time, CpuNumber cpu_number, uint32_t irq);
  bool HandleIRQExit(Ticks event_time, CpuNumber cpu_number, uint32_t irq);
  bool HandleSyscallEnter(Ticks event_time,
                          CpuNumber cpu_number,
                          uint32_t syscall);
  bool HandleSyscallExit(Ticks event_time,
                         CpuNumber cpu_number,
                         uint32_t syscall);
  bool HandlePageFault(Ticks event_time,
                       CpuNumber cpu_number,
                       uint64_t virtual_address,
                       uint32_t flags);
  bool HandleContextSwitch(Ticks event_time,
                           CpuNumber cpu_number,
                           ThreadState outgoing_thread_state,
                           mx_koid_t outgoing_thread,
                           KernelThread outgoing_kernel_thread,
                           mx_koid_t incoming_thread,
                           KernelThread incoming_kernel_thread);
  bool HandleObjectDelete(Ticks event_time, mx_koid_t thread, mx_koid_t object);
  bool HandleThreadCreate(Ticks event_time,
                          mx_koid_t thread,
                          mx_koid_t affected_thread,
                          mx_koid_t affected_process);
  bool HandleThreadStart(Ticks event_time,
                         mx_koid_t thread,
                         mx_koid_t affected_thread);
  bool HandleThreadExit(Ticks event_time, mx_koid_t thread);
  bool HandleProcessCreate(Ticks event_time,
                           mx_koid_t thread,
                           mx_koid_t affected_process);
  bool HandleProcessStart(Ticks event_time,
                          mx_koid_t thread,
                          mx_koid_t affected_thread,
                          mx_koid_t affected_process);
  bool HandleProcessExit(Ticks event_time,
                         mx_koid_t thread,
                         mx_koid_t affected_process);
  bool HandleChannelCreate(Ticks event_time,
                           mx_koid_t thread,
                           mx_koid_t channel0,
                           mx_koid_t channel1,
                           uint32_t flags);
  bool HandleChannelWrite(Ticks event_time,
                          mx_koid_t thread,
                          mx_koid_t channel,
                          uint32_t num_bytes,
                          uint32_t num_handles);
  bool HandleChannelRead(Ticks event_time,
                         mx_koid_t thread,
                         mx_koid_t channel,
                         uint32_t num_bytes,
                         uint32_t num_handles);
  bool HandlePortWait(Ticks event_time, mx_koid_t thread, mx_koid_t port);
  bool HandlePortWaitDone(Ticks event_time,
                          mx_koid_t thread,
                          mx_koid_t port,
                          uint32_t status);
  bool HandlePortCreate(Ticks event_time, mx_koid_t thread, mx_koid_t port);
  bool HandlePortQueue(Ticks event_time,
                       mx_koid_t thread,
                       mx_koid_t port,
                       uint32_t num_bytes);
  bool HandleWaitOne(Ticks event_time,
                     mx_koid_t thread,
                     mx_koid_t object,
                     uint32_t signals,
                     mx_time_t timeout);
  bool HandleWaitOneDone(Ticks event_time,
                         mx_koid_t thread,
                         mx_koid_t object,
                         uint32_t status,
                         uint32_t pending);
  bool HandleProbe(Ticks event_time, mx_koid_t thread, uint32_t probe);
  bool HandleProbe(Ticks event_time,
                   mx_koid_t thread,
                   uint32_t probe,
                   uint32_t arg0,
                   uint32_t arg1);

  struct CpuInfo {
    ThreadRef current_thread_ref = ThreadRef::MakeUnknown();
  };

  ThreadRef GetCpuCurrentThread(CpuNumber cpu_number);
  ThreadRef SwitchCpuToThread(CpuNumber cpu_number, mx_koid_t thread);
  ThreadRef SwitchCpuToKernelThread(CpuNumber cpu_number,
                                    KernelThread kernel_thread);

  const StringRef& GetNameRef(std::unordered_map<uint32_t, StringRef>& table,
                              const char* kind,
                              uint32_t id);
  const ThreadRef& GetThreadRef(mx_koid_t thread);
  const ThreadRef& GetKernelThreadRef(KernelThread kernel_thread);

  TraceWriter& writer_;
  const TagMap& tags_;

  StringRef const ipc_category_ref_;
  StringRef const irq_category_ref_;
  StringRef const probe_category_ref_;
  StringRef const syscall_category_ref_;
  StringRef const channel_category_ref_;
  StringRef const channel_read_name_ref_;
  StringRef const channel_write_name_ref_;
  StringRef const num_bytes_name_ref_;
  StringRef const num_handles_name_ref_;
  StringRef const page_fault_name_ref_;
  StringRef const vaddr_name_ref_;
  StringRef const flags_name_ref_;
  StringRef const arg0_name_ref_;
  StringRef const arg1_name_ref_;

  uint32_t version_ = 0u;

  std::vector<CpuInfo> cpu_infos_;

  std::unordered_map<KernelThread, ThreadRef> kernel_thread_refs_;
  std::unordered_map<mx_koid_t, ThreadRef> thread_refs_;

  std::unordered_map<uint32_t, StringRef> irq_names_;
  std::unordered_map<uint32_t, StringRef> probe_names_;
  std::unordered_map<uint32_t, StringRef> syscall_names_;

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

  FTL_DISALLOW_COPY_AND_ASSIGN(Importer);
};

}  // namespace ktrace_provider

#endif  // APPS_TRACING_SRC_KTRACE_PROVIDER_IMPORTER_H_
