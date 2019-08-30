// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/system/public/zircon/errors.h>
#include <zircon/system/public/zircon/syscalls/exception.h>
#include <zircon/system/public/zircon/syscalls/port.h>

#include <cstdint>
#include <memory>

#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

class ZxChannelCallArgs {
 public:
  static const uint8_t* wr_bytes(const zx_channel_call_args_t* from) {
    return reinterpret_cast<const uint8_t*>(from->wr_bytes);
  }
  static const zx_handle_t* wr_handles(const zx_channel_call_args_t* from) {
    return from->wr_handles;
  }
  static const uint8_t* rd_bytes(const zx_channel_call_args_t* from) {
    return reinterpret_cast<const uint8_t*>(from->rd_bytes);
  }
  static const zx_handle_t* rd_handles(const zx_channel_call_args_t* from) {
    return from->rd_handles;
  }
  static uint32_t wr_num_bytes(const zx_channel_call_args_t* from) { return from->wr_num_bytes; }
  static uint32_t wr_num_handles(const zx_channel_call_args_t* from) {
    return from->wr_num_handles;
  }
  static uint32_t rd_num_bytes(const zx_channel_call_args_t* from) { return from->rd_num_bytes; }
  static uint32_t rd_num_handles(const zx_channel_call_args_t* from) {
    return from->rd_num_handles;
  }
};

class ZxX86_64ExcData : public Class<zx_x86_64_exc_data_t> {
 public:
  static const ZxX86_64ExcData* GetClass();

  static uint64_t vector(const zx_x86_64_exc_data_t* from) { return from->vector; }
  static uint64_t err_code(const zx_x86_64_exc_data_t* from) { return from->err_code; }
  static uint64_t cr2(const zx_x86_64_exc_data_t* from) { return from->cr2; }

 private:
  ZxX86_64ExcData() : Class("zx_x86_64_exc_data_t") {
    AddField(std::make_unique<ClassField<zx_x86_64_exc_data_t, uint64_t>>(
        "vector", SyscallType::kUint64, vector));
    AddField(std::make_unique<ClassField<zx_x86_64_exc_data_t, uint64_t>>(
        "err_code", SyscallType::kUint64, err_code));
    AddField(std::make_unique<ClassField<zx_x86_64_exc_data_t, uint64_t>>(
        "cr2", SyscallType::kUint64, cr2));
  }
  ZxX86_64ExcData(const ZxX86_64ExcData&) = delete;
  ZxX86_64ExcData& operator=(const ZxX86_64ExcData&) = delete;
  static ZxX86_64ExcData* instance_;
};

ZxX86_64ExcData* ZxX86_64ExcData::instance_ = nullptr;

const ZxX86_64ExcData* ZxX86_64ExcData::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxX86_64ExcData;
  }
  return instance_;
}

class ZxArm64ExcData : public Class<zx_arm64_exc_data_t> {
 public:
  static const ZxArm64ExcData* GetClass();

  static uint32_t esr(const zx_arm64_exc_data_t* from) { return from->esr; }
  static uint64_t far(const zx_arm64_exc_data_t* from) { return from->far; }

 private:
  ZxArm64ExcData() : Class("zx_arm64_exc_data_t") {
    AddField(std::make_unique<ClassField<zx_arm64_exc_data_t, uint32_t>>(
        "esr", SyscallType::kUint32, esr));
    AddField(std::make_unique<ClassField<zx_arm64_exc_data_t, uint64_t>>(
        "far", SyscallType::kUint64, far));
  }
  ZxArm64ExcData(const ZxArm64ExcData&) = delete;
  ZxArm64ExcData& operator=(const ZxArm64ExcData&) = delete;
  static ZxArm64ExcData* instance_;
};

ZxArm64ExcData* ZxArm64ExcData::instance_ = nullptr;

const ZxArm64ExcData* ZxArm64ExcData::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxArm64ExcData;
  }
  return instance_;
}

class ZxExceptionContext : public Class<zx_exception_context_t> {
 public:
  static const ZxExceptionContext* GetClass();

  static const zx_x86_64_exc_data_t* x86_64(const zx_exception_context_t* from) {
    return &from->arch.u.x86_64;
  }
  static const zx_arm64_exc_data_t* arm_64(const zx_exception_context_t* from) {
    return &from->arch.u.arm_64;
  }

 private:
  ZxExceptionContext() : Class("zx_exception_context_t") {
    AddField(std::make_unique<ClassClassField<zx_exception_context_t, zx_x86_64_exc_data_t>>(
        "arch.x86_64", x86_64, ZxX86_64ExcData::GetClass()));
    AddField(std::make_unique<ClassClassField<zx_exception_context_t, zx_arm64_exc_data_t>>(
        "arch.arm_64", arm_64, ZxArm64ExcData::GetClass()));
  }
  ZxExceptionContext(const ZxExceptionContext&) = delete;
  ZxExceptionContext& operator=(const ZxExceptionContext&) = delete;
  static ZxExceptionContext* instance_;
};

ZxExceptionContext* ZxExceptionContext::instance_ = nullptr;

const ZxExceptionContext* ZxExceptionContext::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxExceptionContext;
  }
  return instance_;
}

class ZxExceptionHeader : public Class<zx_exception_header_t> {
 public:
  static const ZxExceptionHeader* GetClass();

  static uint32_t size(const zx_exception_header_t* from) { return from->size; }
  static zx_excp_type_t type(const zx_exception_header_t* from) { return from->type; }

 private:
  ZxExceptionHeader() : Class("zx_exception_header_t") {
    AddField(std::make_unique<ClassField<zx_exception_header_t, uint32_t>>(
        "size", SyscallType::kUint32, size));
    AddField(std::make_unique<ClassField<zx_exception_header_t, zx_excp_type_t>>(
        "type", SyscallType::kUint32, type));
  }
  ZxExceptionHeader(const ZxExceptionHeader&) = delete;
  ZxExceptionHeader& operator=(const ZxExceptionHeader&) = delete;
  static ZxExceptionHeader* instance_;
};

ZxExceptionHeader* ZxExceptionHeader::instance_ = nullptr;

const ZxExceptionHeader* ZxExceptionHeader::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxExceptionHeader;
  }
  return instance_;
}

class ZxExceptionReport : public Class<zx_exception_report_t> {
 public:
  static const ZxExceptionReport* GetClass();

  static const zx_exception_header_t* header(const zx_exception_report_t* from) {
    return &from->header;
  }
  static const zx_exception_context_t* context(const zx_exception_report_t* from) {
    return &from->context;
  }

 private:
  ZxExceptionReport() : Class("zx_exception_report_t") {
    AddField(std::make_unique<ClassClassField<zx_exception_report_t, zx_exception_header_t>>(
        "header", header, ZxExceptionHeader::GetClass()));
    AddField(std::make_unique<ClassClassField<zx_exception_report_t, zx_exception_context_t>>(
        "context", context, ZxExceptionContext::GetClass()));
  }
  ZxExceptionReport(const ZxExceptionReport&) = delete;
  ZxExceptionReport& operator=(const ZxExceptionReport&) = delete;
  static ZxExceptionReport* instance_;
};

ZxExceptionReport* ZxExceptionReport::instance_ = nullptr;

const ZxExceptionReport* ZxExceptionReport::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxExceptionReport;
  }
  return instance_;
}

class ZxInfoBti : public Class<zx_info_bti_t> {
 public:
  static const ZxInfoBti* GetClass();

  static uint64_t minimum_contiguity(const zx_info_bti_t* from) { return from->minimum_contiguity; }
  static uint64_t aspace_size(const zx_info_bti_t* from) { return from->aspace_size; }

 private:
  ZxInfoBti() : Class("zx_info_bti_t") {
    AddField(std::make_unique<ClassField<zx_info_bti_t, uint64_t>>(
        "minimum_contiguity", SyscallType::kUint64, minimum_contiguity));
    AddField(std::make_unique<ClassField<zx_info_bti_t, uint64_t>>(
        "aspace_size", SyscallType::kUint64, aspace_size));
  }
  ZxInfoBti(const ZxInfoBti&) = delete;
  ZxInfoBti& operator=(const ZxInfoBti&) = delete;
  static ZxInfoBti* instance_;
};

ZxInfoBti* ZxInfoBti::instance_ = nullptr;

const ZxInfoBti* ZxInfoBti::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoBti;
  }
  return instance_;
}

class ZxInfoCpuStats : public Class<zx_info_cpu_stats_t> {
 public:
  static const ZxInfoCpuStats* GetClass();

  static uint32_t cpu_number(const zx_info_cpu_stats_t* from) { return from->cpu_number; }
  static uint32_t flags(const zx_info_cpu_stats_t* from) { return from->flags; }
  static zx_duration_t idle_time(const zx_info_cpu_stats_t* from) { return from->idle_time; }
  static uint64_t reschedules(const zx_info_cpu_stats_t* from) { return from->reschedules; }
  static uint64_t context_switches(const zx_info_cpu_stats_t* from) {
    return from->context_switches;
  }
  static uint64_t irq_preempts(const zx_info_cpu_stats_t* from) { return from->irq_preempts; }
  static uint64_t preempts(const zx_info_cpu_stats_t* from) { return from->preempts; }
  static uint64_t yields(const zx_info_cpu_stats_t* from) { return from->yields; }
  static uint64_t ints(const zx_info_cpu_stats_t* from) { return from->ints; }
  static uint64_t timer_ints(const zx_info_cpu_stats_t* from) { return from->timer_ints; }
  static uint64_t timers(const zx_info_cpu_stats_t* from) { return from->timers; }
  static uint64_t syscalls(const zx_info_cpu_stats_t* from) { return from->syscalls; }
  static uint64_t reschedule_ipis(const zx_info_cpu_stats_t* from) { return from->reschedule_ipis; }
  static uint64_t generic_ipis(const zx_info_cpu_stats_t* from) { return from->generic_ipis; }

 private:
  ZxInfoCpuStats() : Class("zx_info_cpu_stats_t") {
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint32_t>>(
        "cpu_number", SyscallType::kUint32, cpu_number));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint32_t>>(
        "flags", SyscallType::kUint32, flags));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, zx_duration_t>>(
        "idle_time", SyscallType::kDuration, idle_time));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint64_t>>(
        "reschedules", SyscallType::kUint64, reschedules));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint64_t>>(
        "context_switches", SyscallType::kUint64, context_switches));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint64_t>>(
        "irq_preempts", SyscallType::kUint64, irq_preempts));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint64_t>>(
        "preempts", SyscallType::kUint64, preempts));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint64_t>>(
        "yields", SyscallType::kUint64, yields));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint64_t>>(
        "ints", SyscallType::kUint64, ints));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint64_t>>(
        "timer_ints", SyscallType::kUint64, timer_ints));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint64_t>>(
        "timers", SyscallType::kUint64, timers));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint64_t>>(
        "syscalls", SyscallType::kUint64, syscalls));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint64_t>>(
        "reschedule_ipis", SyscallType::kUint64, reschedule_ipis));
    AddField(std::make_unique<ClassField<zx_info_cpu_stats_t, uint64_t>>(
        "generic_ipis", SyscallType::kUint64, generic_ipis));
  }
  ZxInfoCpuStats(const ZxInfoCpuStats&) = delete;
  ZxInfoCpuStats& operator=(const ZxInfoCpuStats&) = delete;
  static ZxInfoCpuStats* instance_;
};

ZxInfoCpuStats* ZxInfoCpuStats::instance_ = nullptr;

const ZxInfoCpuStats* ZxInfoCpuStats::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoCpuStats;
  }
  return instance_;
}

class ZxInfoHandleBasic : public Class<zx_info_handle_basic_t> {
 public:
  static const ZxInfoHandleBasic* GetClass();

  static zx_koid_t koid(const zx_info_handle_basic_t* from) { return from->koid; }
  static zx_rights_t rights(const zx_info_handle_basic_t* from) { return from->rights; }
  static zx_obj_type_t type(const zx_info_handle_basic_t* from) { return from->type; }
  static zx_koid_t related_koid(const zx_info_handle_basic_t* from) { return from->related_koid; }
  static zx_obj_props_t props(const zx_info_handle_basic_t* from) { return from->props; }

 private:
  ZxInfoHandleBasic() : Class("zx_info_handle_basic_t") {
    AddField(std::make_unique<ClassField<zx_info_handle_basic_t, zx_koid_t>>(
        "koid", SyscallType::kKoid, koid));
    AddField(std::make_unique<ClassField<zx_info_handle_basic_t, zx_rights_t>>(
        "rights", SyscallType::kRights, rights));
    AddField(std::make_unique<ClassField<zx_info_handle_basic_t, zx_obj_type_t>>(
        "type", SyscallType::kObjType, type));
    AddField(std::make_unique<ClassField<zx_info_handle_basic_t, zx_koid_t>>(
        "related_koid", SyscallType::kKoid, related_koid));
    AddField(std::make_unique<ClassField<zx_info_handle_basic_t, zx_obj_props_t>>(
        "props", SyscallType::kObjProps, props));
  }
  ZxInfoHandleBasic(const ZxInfoHandleBasic&) = delete;
  ZxInfoHandleBasic& operator=(const ZxInfoHandleBasic&) = delete;
  static ZxInfoHandleBasic* instance_;
};

ZxInfoHandleBasic* ZxInfoHandleBasic::instance_ = nullptr;

const ZxInfoHandleBasic* ZxInfoHandleBasic::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoHandleBasic;
  }
  return instance_;
}

class ZxInfoHandleCount : public Class<zx_info_handle_count_t> {
 public:
  static const ZxInfoHandleCount* GetClass();

  static uint32_t handle_count(const zx_info_handle_count_t* from) { return from->handle_count; }

 private:
  ZxInfoHandleCount() : Class("zx_info_handle_count_t") {
    AddField(std::make_unique<ClassField<zx_info_handle_count_t, uint32_t>>(
        "handle_count", SyscallType::kUint32, handle_count));
  }
  ZxInfoHandleCount(const ZxInfoHandleCount&) = delete;
  ZxInfoHandleCount& operator=(const ZxInfoHandleCount&) = delete;
  static ZxInfoHandleCount* instance_;
};

ZxInfoHandleCount* ZxInfoHandleCount::instance_ = nullptr;

const ZxInfoHandleCount* ZxInfoHandleCount::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoHandleCount;
  }
  return instance_;
}

class ZxInfoJob : public Class<zx_info_job_t> {
 public:
  static const ZxInfoJob* GetClass();

  static int64_t return_code(const zx_info_job_t* from) { return from->return_code; }
  static bool exited(const zx_info_job_t* from) { return from->exited; }
  static bool kill_on_oom(const zx_info_job_t* from) { return from->kill_on_oom; }
  static bool debugger_attached(const zx_info_job_t* from) { return from->debugger_attached; }

 private:
  ZxInfoJob() : Class("zx_info_job_t") {
    AddField(std::make_unique<ClassField<zx_info_job_t, int64_t>>(
        "return_code", SyscallType::kInt64, return_code));
    AddField(
        std::make_unique<ClassField<zx_info_job_t, bool>>("exited", SyscallType::kBool, exited));
    AddField(std::make_unique<ClassField<zx_info_job_t, bool>>("kill_on_oom", SyscallType::kBool,
                                                               kill_on_oom));
    AddField(std::make_unique<ClassField<zx_info_job_t, bool>>(
        "debugger_attached", SyscallType::kBool, debugger_attached));
  }
  ZxInfoJob(const ZxInfoJob&) = delete;
  ZxInfoJob& operator=(const ZxInfoJob&) = delete;
  static ZxInfoJob* instance_;
};

ZxInfoJob* ZxInfoJob::instance_ = nullptr;

const ZxInfoJob* ZxInfoJob::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoJob;
  }
  return instance_;
}

class ZxInfoKmemStats : public Class<zx_info_kmem_stats_t> {
 public:
  static const ZxInfoKmemStats* GetClass();

  static size_t total_bytes(const zx_info_kmem_stats_t* from) { return from->total_bytes; }
  static size_t free_bytes(const zx_info_kmem_stats_t* from) { return from->free_bytes; }
  static size_t wired_bytes(const zx_info_kmem_stats_t* from) { return from->wired_bytes; }
  static size_t total_heap_bytes(const zx_info_kmem_stats_t* from) {
    return from->total_heap_bytes;
  }
  static size_t free_heap_bytes(const zx_info_kmem_stats_t* from) { return from->free_heap_bytes; }
  static size_t vmo_bytes(const zx_info_kmem_stats_t* from) { return from->vmo_bytes; }
  static size_t mmu_overhead_bytes(const zx_info_kmem_stats_t* from) {
    return from->mmu_overhead_bytes;
  }
  static size_t other_bytes(const zx_info_kmem_stats_t* from) { return from->other_bytes; }

 private:
  ZxInfoKmemStats() : Class("zx_info_kmem_stats_t") {
    AddField(std::make_unique<ClassField<zx_info_kmem_stats_t, size_t>>(
        "total_bytes", SyscallType::kSize, total_bytes));
    AddField(std::make_unique<ClassField<zx_info_kmem_stats_t, size_t>>(
        "free_bytes", SyscallType::kSize, free_bytes));
    AddField(std::make_unique<ClassField<zx_info_kmem_stats_t, size_t>>(
        "wired_bytes", SyscallType::kSize, wired_bytes));
    AddField(std::make_unique<ClassField<zx_info_kmem_stats_t, size_t>>(
        "total_heap_bytes", SyscallType::kSize, total_heap_bytes));
    AddField(std::make_unique<ClassField<zx_info_kmem_stats_t, size_t>>(
        "free_heap_bytes", SyscallType::kSize, free_heap_bytes));
    AddField(std::make_unique<ClassField<zx_info_kmem_stats_t, size_t>>(
        "vmo_bytes", SyscallType::kSize, vmo_bytes));
    AddField(std::make_unique<ClassField<zx_info_kmem_stats_t, size_t>>(
        "mmu_overhead_bytes", SyscallType::kSize, mmu_overhead_bytes));
    AddField(std::make_unique<ClassField<zx_info_kmem_stats_t, size_t>>(
        "other_bytes", SyscallType::kSize, other_bytes));
  }
  ZxInfoKmemStats(const ZxInfoKmemStats&) = delete;
  ZxInfoKmemStats& operator=(const ZxInfoKmemStats&) = delete;
  static ZxInfoKmemStats* instance_;
};

ZxInfoKmemStats* ZxInfoKmemStats::instance_ = nullptr;

const ZxInfoKmemStats* ZxInfoKmemStats::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoKmemStats;
  }
  return instance_;
}

class ZxInfoProcess : public Class<zx_info_process_t> {
 public:
  static const ZxInfoProcess* GetClass();

  static int64_t return_code(const zx_info_process_t* from) { return from->return_code; }
  static bool started(const zx_info_process_t* from) { return from->started; }
  static bool exited(const zx_info_process_t* from) { return from->exited; }
  static bool debugger_attached(const zx_info_process_t* from) { return from->debugger_attached; }

 private:
  ZxInfoProcess() : Class("zx_info_process_t") {
    AddField(std::make_unique<ClassField<zx_info_process_t, int64_t>>(
        "return_code", SyscallType::kInt64, return_code));
    AddField(std::make_unique<ClassField<zx_info_process_t, bool>>("started", SyscallType::kBool,
                                                                   started));
    AddField(std::make_unique<ClassField<zx_info_process_t, bool>>("exited", SyscallType::kBool,
                                                                   exited));
    AddField(std::make_unique<ClassField<zx_info_process_t, bool>>(
        "debugger_attached", SyscallType::kBool, debugger_attached));
  }
  ZxInfoProcess(const ZxInfoProcess&) = delete;
  ZxInfoProcess& operator=(const ZxInfoProcess&) = delete;
  static ZxInfoProcess* instance_;
};

ZxInfoProcess* ZxInfoProcess::instance_ = nullptr;

const ZxInfoProcess* ZxInfoProcess::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoProcess;
  }
  return instance_;
}

class ZxInfoProcessHandleStats : public Class<zx_info_process_handle_stats_t> {
 public:
  static const ZxInfoProcessHandleStats* GetClass();

  static std::pair<const uint32_t*, int> handle_count(const zx_info_process_handle_stats_t* from) {
    return std::make_pair(reinterpret_cast<const uint32_t*>(from->handle_count),
                          sizeof(from->handle_count) / sizeof(uint32_t));
  }

 private:
  ZxInfoProcessHandleStats() : Class("zx_info_process_handle_stats_t") {
    AddField(std::make_unique<
             ClassField<zx_info_process_handle_stats_t, std::pair<const uint32_t*, int>>>(
        "handle_count", SyscallType::kUint32ArrayDecimal, handle_count));
  }
  ZxInfoProcessHandleStats(const ZxInfoProcessHandleStats&) = delete;
  ZxInfoProcessHandleStats& operator=(const ZxInfoProcessHandleStats&) = delete;
  static ZxInfoProcessHandleStats* instance_;
};

ZxInfoProcessHandleStats* ZxInfoProcessHandleStats::instance_ = nullptr;

const ZxInfoProcessHandleStats* ZxInfoProcessHandleStats::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoProcessHandleStats;
  }
  return instance_;
}

class ZxInfoResource : public Class<zx_info_resource_t> {
 public:
  static const ZxInfoResource* GetClass();

  static zx_rsrc_kind_t kind(const zx_info_resource_t* from) { return from->kind; }
  static uint32_t flags(const zx_info_resource_t* from) { return from->flags; }
  static uint64_t base(const zx_info_resource_t* from) { return from->base; }
  static size_t size(const zx_info_resource_t* from) { return from->size; }
  static std::pair<const char*, size_t> name(const zx_info_resource_t* from) {
    return std::make_pair(reinterpret_cast<const char*>(from->name), sizeof(from->name));
  }

 private:
  ZxInfoResource() : Class("zx_info_resource_t") {
    AddField(std::make_unique<ClassField<zx_info_resource_t, zx_rsrc_kind_t>>(
        "kind", SyscallType::kRsrcKind, kind));
    AddField(std::make_unique<ClassField<zx_info_resource_t, uint32_t>>(
        "flags", SyscallType::kUint32, flags));
    AddField(std::make_unique<ClassField<zx_info_resource_t, uint64_t>>(
        "base", SyscallType::kUint64, base));
    AddField(
        std::make_unique<ClassField<zx_info_resource_t, size_t>>("size", SyscallType::kSize, size));
    AddField(std::make_unique<ClassField<zx_info_resource_t, std::pair<const char*, size_t>>>(
        "name", SyscallType::kCharArray, name));
  }
  ZxInfoResource(const ZxInfoResource&) = delete;
  ZxInfoResource& operator=(const ZxInfoResource&) = delete;
  static ZxInfoResource* instance_;
};

ZxInfoResource* ZxInfoResource::instance_ = nullptr;

const ZxInfoResource* ZxInfoResource::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoResource;
  }
  return instance_;
}

class ZxInfoSocket : public Class<zx_info_socket_t> {
 public:
  static const ZxInfoSocket* GetClass();

  static uint32_t options(const zx_info_socket_t* from) { return from->options; }
  static size_t rx_buf_max(const zx_info_socket_t* from) { return from->rx_buf_max; }
  static size_t rx_buf_size(const zx_info_socket_t* from) { return from->rx_buf_size; }
  static size_t rx_buf_available(const zx_info_socket_t* from) { return from->rx_buf_available; }
  static size_t tx_buf_max(const zx_info_socket_t* from) { return from->tx_buf_max; }
  static size_t tx_buf_size(const zx_info_socket_t* from) { return from->tx_buf_size; }

 private:
  ZxInfoSocket() : Class("zx_info_socket_t") {
    AddField(std::make_unique<ClassField<zx_info_socket_t, uint32_t>>(
        "options", SyscallType::kUint32, options));
    AddField(std::make_unique<ClassField<zx_info_socket_t, size_t>>(
        "rx_buf_max", SyscallType::kSize, rx_buf_max));
    AddField(std::make_unique<ClassField<zx_info_socket_t, size_t>>(
        "rx_buf_size", SyscallType::kSize, rx_buf_size));
    AddField(std::make_unique<ClassField<zx_info_socket_t, size_t>>(
        "rx_buf_available", SyscallType::kSize, rx_buf_available));
    AddField(std::make_unique<ClassField<zx_info_socket_t, size_t>>(
        "tx_buf_max", SyscallType::kSize, tx_buf_max));
    AddField(std::make_unique<ClassField<zx_info_socket_t, size_t>>(
        "tx_buf_size", SyscallType::kSize, tx_buf_size));
  }
  ZxInfoSocket(const ZxInfoSocket&) = delete;
  ZxInfoSocket& operator=(const ZxInfoSocket&) = delete;
  static ZxInfoSocket* instance_;
};

ZxInfoSocket* ZxInfoSocket::instance_ = nullptr;

const ZxInfoSocket* ZxInfoSocket::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoSocket;
  }
  return instance_;
}

class ZxInfoTaskStats : public Class<zx_info_task_stats_t> {
 public:
  static const ZxInfoTaskStats* GetClass();

  static size_t mem_mapped_bytes(const zx_info_task_stats_t* from) {
    return from->mem_mapped_bytes;
  }
  static size_t mem_private_bytes(const zx_info_task_stats_t* from) {
    return from->mem_private_bytes;
  }
  static size_t mem_shared_bytes(const zx_info_task_stats_t* from) {
    return from->mem_shared_bytes;
  }
  static size_t mem_scaled_shared_bytes(const zx_info_task_stats_t* from) {
    return from->mem_scaled_shared_bytes;
  }

 private:
  ZxInfoTaskStats() : Class("zx_info_task_stats_t") {
    AddField(std::make_unique<ClassField<zx_info_task_stats_t, size_t>>(
        "mem_mapped_bytes", SyscallType::kSize, mem_mapped_bytes));
    AddField(std::make_unique<ClassField<zx_info_task_stats_t, size_t>>(
        "mem_private_bytes", SyscallType::kSize, mem_private_bytes));
    AddField(std::make_unique<ClassField<zx_info_task_stats_t, size_t>>(
        "mem_shared_bytes", SyscallType::kSize, mem_shared_bytes));
    AddField(std::make_unique<ClassField<zx_info_task_stats_t, size_t>>(
        "mem_scaled_shared_bytes", SyscallType::kSize, mem_scaled_shared_bytes));
  }
  ZxInfoTaskStats(const ZxInfoTaskStats&) = delete;
  ZxInfoTaskStats& operator=(const ZxInfoTaskStats&) = delete;
  static ZxInfoTaskStats* instance_;
};

ZxInfoTaskStats* ZxInfoTaskStats::instance_ = nullptr;

const ZxInfoTaskStats* ZxInfoTaskStats::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoTaskStats;
  }
  return instance_;
}

class ZxCpuSet : public Class<zx_cpu_set_t> {
 public:
  static const ZxCpuSet* GetClass();

  static std::pair<const uint64_t*, int> mask(const zx_cpu_set_t* from) {
    return std::make_pair(reinterpret_cast<const uint64_t*>(from->mask),
                          sizeof(from->mask) / sizeof(uint64_t));
  }

 private:
  ZxCpuSet() : Class("zx_cpu_set_t") {
    AddField(std::make_unique<ClassField<zx_cpu_set_t, std::pair<const uint64_t*, int>>>(
        "mask", SyscallType::kUint64ArrayHexa, mask));
  }
  ZxCpuSet(const ZxCpuSet&) = delete;
  ZxCpuSet& operator=(const ZxCpuSet&) = delete;
  static ZxCpuSet* instance_;
};

ZxCpuSet* ZxCpuSet::instance_ = nullptr;

const ZxCpuSet* ZxCpuSet::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxCpuSet;
  }
  return instance_;
}

class ZxInfoThread : public Class<zx_info_thread_t> {
 public:
  static const ZxInfoThread* GetClass();

  static uint32_t state(const zx_info_thread_t* from) { return from->state; }
  static uint32_t wait_exception_channel_type(const zx_info_thread_t* from) {
    return from->wait_exception_channel_type;
  }
  static const zx_cpu_set_t* cpu_affinity_mask(const zx_info_thread_t* from) {
    return &from->cpu_affinity_mask;
  }

 private:
  ZxInfoThread() : Class("zx_info_thread_t") {
    AddField(std::make_unique<ClassField<zx_info_thread_t, uint32_t>>(
        "state", SyscallType::kThreadState, state));
    AddField(std::make_unique<ClassField<zx_info_thread_t, uint32_t>>(
        "wait_exception_channel_type", SyscallType::kExceptionChannelType,
        wait_exception_channel_type));
    AddField(std::make_unique<ClassClassField<zx_info_thread_t, zx_cpu_set_t>>(
        "cpu_affinity_mask", cpu_affinity_mask, ZxCpuSet::GetClass()));
  }
  ZxInfoThread(const ZxInfoThread&) = delete;
  ZxInfoThread& operator=(const ZxInfoThread&) = delete;
  static ZxInfoThread* instance_;
};

ZxInfoThread* ZxInfoThread::instance_ = nullptr;

const ZxInfoThread* ZxInfoThread::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoThread;
  }
  return instance_;
}

class ZxInfoThreadStats : public Class<zx_info_thread_stats_t> {
 public:
  static const ZxInfoThreadStats* GetClass();

  static zx_duration_t total_runtime(const zx_info_thread_stats_t* from) {
    return from->total_runtime;
  }
  static uint32_t last_scheduled_cpu(const zx_info_thread_stats_t* from) {
    return from->last_scheduled_cpu;
  }

 private:
  ZxInfoThreadStats() : Class("zx_info_thread_stats_t") {
    AddField(std::make_unique<ClassField<zx_info_thread_stats_t, zx_duration_t>>(
        "total_runtime", SyscallType::kDuration, total_runtime));
    AddField(std::make_unique<ClassField<zx_info_thread_stats_t, uint32_t>>(
        "last_scheduled_cpu", SyscallType::kUint32, last_scheduled_cpu));
  }
  ZxInfoThreadStats(const ZxInfoThreadStats&) = delete;
  ZxInfoThreadStats& operator=(const ZxInfoThreadStats&) = delete;
  static ZxInfoThreadStats* instance_;
};

ZxInfoThreadStats* ZxInfoThreadStats::instance_ = nullptr;

const ZxInfoThreadStats* ZxInfoThreadStats::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoThreadStats;
  }
  return instance_;
}

class ZxInfoTimer : public Class<zx_info_timer_t> {
 public:
  static const ZxInfoTimer* GetClass();

  static uint32_t options(const zx_info_timer_t* from) { return from->options; }
  static zx_time_t deadline(const zx_info_timer_t* from) { return from->deadline; }
  static zx_duration_t slack(const zx_info_timer_t* from) { return from->slack; }

 private:
  ZxInfoTimer() : Class("zx_info_timer_t") {
    AddField(std::make_unique<ClassField<zx_info_timer_t, uint32_t>>(
        "options", SyscallType::kUint32, options));
    AddField(std::make_unique<ClassField<zx_info_timer_t, zx_time_t>>(
        "deadline", SyscallType::kMonotonicTime, deadline));
    AddField(std::make_unique<ClassField<zx_info_timer_t, zx_duration_t>>(
        "slack", SyscallType::kDuration, slack));
  }
  ZxInfoTimer(const ZxInfoTimer&) = delete;
  ZxInfoTimer& operator=(const ZxInfoTimer&) = delete;
  static ZxInfoTimer* instance_;
};

ZxInfoTimer* ZxInfoTimer::instance_ = nullptr;

const ZxInfoTimer* ZxInfoTimer::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoTimer;
  }
  return instance_;
}

class ZxInfoVmar : public Class<zx_info_vmar_t> {
 public:
  static const ZxInfoVmar* GetClass();

  static uintptr_t base(const zx_info_vmar_t* from) { return from->base; }
  static size_t len(const zx_info_vmar_t* from) { return from->len; }

 private:
  ZxInfoVmar() : Class("zx_info_vmar_t") {
    AddField(std::make_unique<ClassField<zx_info_vmar_t, uintptr_t>>("base", SyscallType::kUintptr,
                                                                     base));
    AddField(std::make_unique<ClassField<zx_info_vmar_t, size_t>>("len", SyscallType::kSize, len));
  }
  ZxInfoVmar(const ZxInfoVmar&) = delete;
  ZxInfoVmar& operator=(const ZxInfoVmar&) = delete;
  static ZxInfoVmar* instance_;
};

ZxInfoVmar* ZxInfoVmar::instance_ = nullptr;

const ZxInfoVmar* ZxInfoVmar::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoVmar;
  }
  return instance_;
}

class ZxInfoVmo : public Class<zx_info_vmo_t> {
 public:
  static const ZxInfoVmo* GetClass();

  static zx_koid_t koid(const zx_info_vmo_t* from) { return from->koid; }
  static std::pair<const char*, size_t> name(const zx_info_vmo_t* from) {
    return std::make_pair(reinterpret_cast<const char*>(from->name), sizeof(from->name));
  }
  static uint64_t size_bytes(const zx_info_vmo_t* from) { return from->size_bytes; }
  static zx_koid_t parent_koid(const zx_info_vmo_t* from) { return from->parent_koid; }
  static size_t num_children(const zx_info_vmo_t* from) { return from->num_children; }
  static size_t num_mappings(const zx_info_vmo_t* from) { return from->num_mappings; }
  static size_t share_count(const zx_info_vmo_t* from) { return from->share_count; }
  static uint32_t flags(const zx_info_vmo_t* from) { return from->flags; }
  static uint64_t committed_bytes(const zx_info_vmo_t* from) { return from->committed_bytes; }
  static zx_rights_t handle_rights(const zx_info_vmo_t* from) { return from->handle_rights; }
  static uint32_t cache_policy(const zx_info_vmo_t* from) { return from->cache_policy; }

 private:
  ZxInfoVmo() : Class("zx_info_vmo_t") {
    AddField(
        std::make_unique<ClassField<zx_info_vmo_t, zx_koid_t>>("koid", SyscallType::kKoid, koid));
    AddField(std::make_unique<ClassField<zx_info_vmo_t, std::pair<const char*, size_t>>>(
        "name", SyscallType::kCharArray, name));
    AddField(std::make_unique<ClassField<zx_info_vmo_t, uint64_t>>(
        "size_bytes", SyscallType::kUint64, size_bytes));
    AddField(std::make_unique<ClassField<zx_info_vmo_t, zx_koid_t>>(
        "parent_koid", SyscallType::kKoid, parent_koid));
    AddField(std::make_unique<ClassField<zx_info_vmo_t, size_t>>("num_children", SyscallType::kSize,
                                                                 num_children));
    AddField(std::make_unique<ClassField<zx_info_vmo_t, size_t>>("num_mappings", SyscallType::kSize,
                                                                 num_mappings));
    AddField(std::make_unique<ClassField<zx_info_vmo_t, size_t>>("share_count", SyscallType::kSize,
                                                                 share_count));
    AddField(std::make_unique<ClassField<zx_info_vmo_t, uint32_t>>("flags", SyscallType::kVmoType,
                                                                   flags));
    AddField(std::make_unique<ClassField<zx_info_vmo_t, uint64_t>>(
        "committed_bytes", SyscallType::kUint64, committed_bytes));
    AddField(std::make_unique<ClassField<zx_info_vmo_t, zx_rights_t>>(
        "handle_rights", SyscallType::kRights, handle_rights));
    AddField(std::make_unique<ClassField<zx_info_vmo_t, uint32_t>>(
        "cache_policy", SyscallType::kCachePolicy, cache_policy));
  }
  ZxInfoVmo(const ZxInfoVmo&) = delete;
  ZxInfoVmo& operator=(const ZxInfoVmo&) = delete;
  static ZxInfoVmo* instance_;
};

ZxInfoVmo* ZxInfoVmo::instance_ = nullptr;

const ZxInfoVmo* ZxInfoVmo::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoVmo;
  }
  return instance_;
}

class ZxPacketUser : public Class<zx_packet_user_t> {
 public:
  static const ZxPacketUser* GetClass();

  static std::pair<const uint64_t*, int> u64(const zx_packet_user_t* from) {
    return std::make_pair(reinterpret_cast<const uint64_t*>(from->u64),
                          sizeof(from->u64) / sizeof(uint64_t));
  }
  static std::pair<const uint32_t*, int> u32(const zx_packet_user_t* from) {
    return std::make_pair(reinterpret_cast<const uint32_t*>(from->u32),
                          sizeof(from->u32) / sizeof(uint32_t));
  }
  static std::pair<const uint16_t*, int> u16(const zx_packet_user_t* from) {
    return std::make_pair(reinterpret_cast<const uint16_t*>(from->u16),
                          sizeof(from->u16) / sizeof(uint16_t));
  }
  static std::pair<const uint8_t*, int> c8(const zx_packet_user_t* from) {
    return std::make_pair(reinterpret_cast<const uint8_t*>(from->c8),
                          sizeof(from->c8) / sizeof(uint8_t));
  }

 private:
  ZxPacketUser() : Class("zx_packet_user_t") {
    AddField(std::make_unique<ClassField<zx_packet_user_t, std::pair<const uint64_t*, int>>>(
        "u64", SyscallType::kUint64ArrayHexa, u64));
    AddField(std::make_unique<ClassField<zx_packet_user_t, std::pair<const uint32_t*, int>>>(
        "u32", SyscallType::kUint32ArrayHexa, u32));
    AddField(std::make_unique<ClassField<zx_packet_user_t, std::pair<const uint16_t*, int>>>(
        "u16", SyscallType::kUint16ArrayHexa, u16));
    AddField(std::make_unique<ClassField<zx_packet_user_t, std::pair<const uint8_t*, int>>>(
        "u8", SyscallType::kUint8ArrayHexa, c8));
  }
  ZxPacketUser(const ZxPacketUser&) = delete;
  ZxPacketUser& operator=(const ZxPacketUser&) = delete;
  static ZxPacketUser* instance_;
};

ZxPacketUser* ZxPacketUser::instance_ = nullptr;

const ZxPacketUser* ZxPacketUser::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketUser;
  }
  return instance_;
}

class ZxPacketSignal : public Class<zx_packet_signal_t> {
 public:
  static const ZxPacketSignal* GetClass();

  static zx_signals_t trigger(const zx_packet_signal_t* from) { return from->trigger; }
  static zx_signals_t observed(const zx_packet_signal_t* from) { return from->observed; }
  static uint64_t count(const zx_packet_signal_t* from) { return from->count; }
  static uint64_t timestamp(const zx_packet_signal_t* from) { return from->timestamp; }
  static uint64_t reserved1(const zx_packet_signal_t* from) { return from->reserved1; }

 private:
  ZxPacketSignal() : Class("zx_packet_signal_t") {
    AddField(std::make_unique<ClassField<zx_packet_signal_t, zx_signals_t>>(
        "trigger", SyscallType::kSignals, trigger));
    AddField(std::make_unique<ClassField<zx_packet_signal_t, zx_signals_t>>(
        "observed", SyscallType::kSignals, observed));
    AddField(std::make_unique<ClassField<zx_packet_signal_t, uint64_t>>(
        "count", SyscallType::kUint64, count));
    AddField(std::make_unique<ClassField<zx_packet_signal_t, uint64_t>>(
        "timestamp", SyscallType::kTime, timestamp));
    AddField(std::make_unique<ClassField<zx_packet_signal_t, uint64_t>>(
        "reserved1", SyscallType::kUint64, reserved1));
  }
  ZxPacketSignal(const ZxPacketSignal&) = delete;
  ZxPacketSignal& operator=(const ZxPacketSignal&) = delete;
  static ZxPacketSignal* instance_;
};

ZxPacketSignal* ZxPacketSignal::instance_ = nullptr;

const ZxPacketSignal* ZxPacketSignal::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketSignal;
  }
  return instance_;
}

class ZxPacketException : public Class<zx_packet_exception_t> {
 public:
  static const ZxPacketException* GetClass();

  static uint64_t pid(const zx_packet_exception_t* from) { return from->pid; }
  static uint64_t tid(const zx_packet_exception_t* from) { return from->tid; }
  static uint64_t reserved0(const zx_packet_exception_t* from) { return from->reserved0; }
  static uint64_t reserved1(const zx_packet_exception_t* from) { return from->reserved1; }

 private:
  ZxPacketException() : Class("zx_packet_exception_t") {
    AddField(std::make_unique<ClassField<zx_packet_exception_t, uint64_t>>(
        "pid", SyscallType::kUint64, pid));
    AddField(std::make_unique<ClassField<zx_packet_exception_t, uint64_t>>(
        "tid", SyscallType::kUint64, tid));
    AddField(std::make_unique<ClassField<zx_packet_exception_t, uint64_t>>(
        "reserved0", SyscallType::kUint64, reserved0));
    AddField(std::make_unique<ClassField<zx_packet_exception_t, uint64_t>>(
        "reserved1", SyscallType::kUint64, reserved1));
  }
  ZxPacketException(const ZxPacketException&) = delete;
  ZxPacketException& operator=(const ZxPacketException&) = delete;
  static ZxPacketException* instance_;
};

ZxPacketException* ZxPacketException::instance_ = nullptr;

const ZxPacketException* ZxPacketException::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketException;
  }
  return instance_;
}

class ZxPacketGuestBell : public Class<zx_packet_guest_bell_t> {
 public:
  static const ZxPacketGuestBell* GetClass();

  static zx_gpaddr_t addr(const zx_packet_guest_bell_t* from) { return from->addr; }
  static uint64_t reserved0(const zx_packet_guest_bell_t* from) { return from->reserved0; }
  static uint64_t reserved1(const zx_packet_guest_bell_t* from) { return from->reserved1; }
  static uint64_t reserved2(const zx_packet_guest_bell_t* from) { return from->reserved2; }

 private:
  ZxPacketGuestBell() : Class("zx_packet_guest_bell_t") {
    AddField(std::make_unique<ClassField<zx_packet_guest_bell_t, zx_gpaddr_t>>(
        "addr", SyscallType::kGpAddr, addr));
    AddField(std::make_unique<ClassField<zx_packet_guest_bell_t, uint64_t>>(
        "reserved0", SyscallType::kUint64, reserved0));
    AddField(std::make_unique<ClassField<zx_packet_guest_bell_t, uint64_t>>(
        "reserved1", SyscallType::kUint64, reserved1));
    AddField(std::make_unique<ClassField<zx_packet_guest_bell_t, uint64_t>>(
        "reserved2", SyscallType::kUint64, reserved2));
  }
  ZxPacketGuestBell(const ZxPacketGuestBell&) = delete;
  ZxPacketGuestBell& operator=(const ZxPacketGuestBell&) = delete;
  static ZxPacketGuestBell* instance_;
};

ZxPacketGuestBell* ZxPacketGuestBell::instance_ = nullptr;

const ZxPacketGuestBell* ZxPacketGuestBell::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestBell;
  }
  return instance_;
}

class ZxPacketGuestMemAArch64 : public Class<zx_packet_guest_mem_aarch64_t> {
 public:
  static const ZxPacketGuestMemAArch64* GetClass();

  static zx_gpaddr_t addr(const zx_packet_guest_mem_aarch64_t* from) { return from->addr; }
  static uint8_t access_size(const zx_packet_guest_mem_aarch64_t* from) {
    return from->access_size;
  }
  static bool sign_extend(const zx_packet_guest_mem_aarch64_t* from) { return from->sign_extend; }
  static uint8_t xt(const zx_packet_guest_mem_aarch64_t* from) { return from->xt; }
  static bool read(const zx_packet_guest_mem_aarch64_t* from) { return from->read; }
  static uint64_t data(const zx_packet_guest_mem_aarch64_t* from) { return from->data; }
  static uint64_t reserved(const zx_packet_guest_mem_aarch64_t* from) { return from->reserved; }

 private:
  ZxPacketGuestMemAArch64() : Class("zx_packet_guest_mem_aarch64_t") {
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, zx_gpaddr_t>>(
        "addr", SyscallType::kGpAddr, addr));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, uint8_t>>(
        "access_size", SyscallType::kUint8, access_size));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, bool>>(
        "sign_extend", SyscallType::kBool, sign_extend));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, uint8_t>>(
        "xt", SyscallType::kUint8, xt));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, bool>>(
        "read", SyscallType::kBool, read));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, uint64_t>>(
        "data", SyscallType::kUint64, data));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_aarch64_t, uint64_t>>(
        "reserved", SyscallType::kUint64, reserved));
  }
  ZxPacketGuestMemAArch64(const ZxPacketGuestMemAArch64&) = delete;
  ZxPacketGuestMemAArch64& operator=(const ZxPacketGuestMemAArch64&) = delete;
  static ZxPacketGuestMemAArch64* instance_;
};

ZxPacketGuestMemAArch64* ZxPacketGuestMemAArch64::instance_ = nullptr;

const ZxPacketGuestMemAArch64* ZxPacketGuestMemAArch64::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestMemAArch64;
  }
  return instance_;
}

class ZxPacketGuestMemX86 : public Class<zx_packet_guest_mem_x86_t> {
 public:
  static const ZxPacketGuestMemX86* GetClass();

  static zx_gpaddr_t addr(const zx_packet_guest_mem_x86_t* from) { return from->addr; }
  static uint8_t inst_len(const zx_packet_guest_mem_x86_t* from) { return from->inst_len; }
  static std::pair<const uint8_t*, int> inst_buf(const zx_packet_guest_mem_x86_t* from) {
    return std::make_pair(reinterpret_cast<const uint8_t*>(from->inst_buf),
                          sizeof(from->inst_buf) / sizeof(uint8_t));
  }
  static uint8_t default_operand_size(const zx_packet_guest_mem_x86_t* from) {
    return from->default_operand_size;
  }
  static std::pair<const uint8_t*, int> reserved(const zx_packet_guest_mem_x86_t* from) {
    return std::make_pair(reinterpret_cast<const uint8_t*>(from->reserved),
                          sizeof(from->reserved) / sizeof(uint8_t));
  }

 private:
  ZxPacketGuestMemX86() : Class("zx_packet_guest_mem_x86_t") {
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_x86_t, zx_gpaddr_t>>(
        "addr", SyscallType::kGpAddr, addr));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_x86_t, uint8_t>>(
        "inst_len", SyscallType::kUint8, inst_len));
    AddField(
        std::make_unique<ClassField<zx_packet_guest_mem_x86_t, std::pair<const uint8_t*, int>>>(
            "inst_buf", SyscallType::kUint8ArrayHexa, inst_buf));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_x86_t, uint8_t>>(
        "default_operand_size", SyscallType::kUint8, default_operand_size));
    AddField(
        std::make_unique<ClassField<zx_packet_guest_mem_x86_t, std::pair<const uint8_t*, int>>>(
            "reserved", SyscallType::kUint8ArrayHexa, reserved));
  }
  ZxPacketGuestMemX86(const ZxPacketGuestMemX86&) = delete;
  ZxPacketGuestMemX86& operator=(const ZxPacketGuestMemX86&) = delete;
  static ZxPacketGuestMemX86* instance_;
};

ZxPacketGuestMemX86* ZxPacketGuestMemX86::instance_ = nullptr;

const ZxPacketGuestMemX86* ZxPacketGuestMemX86::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestMemX86;
  }
  return instance_;
}

class ZxPacketGuestIo : public Class<zx_packet_guest_io_t> {
 public:
  static const ZxPacketGuestIo* GetClass();

  static uint16_t port(const zx_packet_guest_io_t* from) { return from->port; }
  static uint8_t access_size(const zx_packet_guest_io_t* from) { return from->access_size; }
  static bool input(const zx_packet_guest_io_t* from) { return from->input; }
  static uint8_t u8(const zx_packet_guest_io_t* from) { return from->u8; }
  static uint16_t u16(const zx_packet_guest_io_t* from) { return from->u16; }
  static uint32_t u32(const zx_packet_guest_io_t* from) { return from->u32; }
  static std::pair<const uint8_t*, int> data(const zx_packet_guest_io_t* from) {
    return std::make_pair(reinterpret_cast<const uint8_t*>(from->data),
                          sizeof(from->data) / sizeof(uint8_t));
  }
  static uint64_t reserved0(const zx_packet_guest_io_t* from) { return from->reserved0; }
  static uint64_t reserved1(const zx_packet_guest_io_t* from) { return from->reserved1; }
  static uint64_t reserved2(const zx_packet_guest_io_t* from) { return from->reserved2; }

 private:
  ZxPacketGuestIo() : Class("zx_packet_guest_io_t") {
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint16_t>>(
        "port", SyscallType::kUint16, port));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint8_t>>(
        "access_size", SyscallType::kUint8, access_size));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, bool>>("input", SyscallType::kBool,
                                                                      input));
    AddField(
        std::make_unique<ClassField<zx_packet_guest_io_t, uint8_t>>("u8", SyscallType::kUint8, u8));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint16_t>>(
        "u16", SyscallType::kUint16, u16));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint32_t>>(
        "u32", SyscallType::kUint32, u32));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, std::pair<const uint8_t*, int>>>(
        "data", SyscallType::kUint8ArrayHexa, data));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint64_t>>(
        "reserved0", SyscallType::kUint64, reserved0));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint64_t>>(
        "reserved1", SyscallType::kUint64, reserved1));
    AddField(std::make_unique<ClassField<zx_packet_guest_io_t, uint64_t>>(
        "reserved2", SyscallType::kUint64, reserved2));
  }
  ZxPacketGuestIo(const ZxPacketGuestIo&) = delete;
  ZxPacketGuestIo& operator=(const ZxPacketGuestIo&) = delete;
  static ZxPacketGuestIo* instance_;
};

ZxPacketGuestIo* ZxPacketGuestIo::instance_ = nullptr;

const ZxPacketGuestIo* ZxPacketGuestIo::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestIo;
  }
  return instance_;
}

// This is extracted from zx_packet_vcpu from zircon/system/public/zircon/syscalls/port.h
struct zx_packet_guest_vcpu_interrupt {
  uint64_t mask;
  uint8_t vector;
};
using zx_packet_guest_vcpu_interrupt_t = struct zx_packet_guest_vcpu_interrupt;

class ZxPacketGuestVcpuInterrupt : public Class<zx_packet_guest_vcpu_interrupt_t> {
 public:
  static const ZxPacketGuestVcpuInterrupt* GetClass();

  static uint64_t mask(const zx_packet_guest_vcpu_interrupt_t* from) { return from->mask; }
  static uint8_t vector(const zx_packet_guest_vcpu_interrupt_t* from) { return from->vector; }

 private:
  ZxPacketGuestVcpuInterrupt() : Class("zx_packet_guest_vcpu_interrupt_t") {
    AddField(std::make_unique<ClassField<zx_packet_guest_vcpu_interrupt_t, uint64_t>>(
        "mask", SyscallType::kUint64, mask));
    AddField(std::make_unique<ClassField<zx_packet_guest_vcpu_interrupt_t, uint8_t>>(
        "vector", SyscallType::kUint8, vector));
  }
  ZxPacketGuestVcpuInterrupt(const ZxPacketGuestVcpuInterrupt&) = delete;
  ZxPacketGuestVcpuInterrupt& operator=(const ZxPacketGuestVcpuInterrupt&) = delete;
  static ZxPacketGuestVcpuInterrupt* instance_;
};

ZxPacketGuestVcpuInterrupt* ZxPacketGuestVcpuInterrupt::instance_ = nullptr;

const ZxPacketGuestVcpuInterrupt* ZxPacketGuestVcpuInterrupt::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestVcpuInterrupt;
  }
  return instance_;
}

// This is extracted from zx_packet_vcpu from zircon/system/public/zircon/syscalls/port.h
struct zx_packet_guest_vcpu_startup {
  uint64_t id;
  zx_gpaddr_t entry;
};
using zx_packet_guest_vcpu_startup_t = struct zx_packet_guest_vcpu_startup;

class ZxPacketGuestVcpuStartup : public Class<zx_packet_guest_vcpu_startup_t> {
 public:
  static const ZxPacketGuestVcpuStartup* GetClass();

  static uint64_t id(const zx_packet_guest_vcpu_startup_t* from) { return from->id; }
  static zx_gpaddr_t entry(const zx_packet_guest_vcpu_startup_t* from) { return from->entry; }

 private:
  ZxPacketGuestVcpuStartup() : Class("zx_packet_guest_vcpu_startup_t") {
    AddField(std::make_unique<ClassField<zx_packet_guest_vcpu_startup_t, uint64_t>>(
        "id", SyscallType::kUint64, id));
    AddField(std::make_unique<ClassField<zx_packet_guest_vcpu_startup_t, zx_gpaddr_t>>(
        "entry", SyscallType::kGpAddr, entry));
  }
  ZxPacketGuestVcpuStartup(const ZxPacketGuestVcpuStartup&) = delete;
  ZxPacketGuestVcpuStartup& operator=(const ZxPacketGuestVcpuStartup&) = delete;
  static ZxPacketGuestVcpuStartup* instance_;
};

ZxPacketGuestVcpuStartup* ZxPacketGuestVcpuStartup::instance_ = nullptr;

const ZxPacketGuestVcpuStartup* ZxPacketGuestVcpuStartup::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestVcpuStartup;
  }
  return instance_;
}

class ZxPacketGuestVcpu : public Class<zx_packet_guest_vcpu_t> {
 public:
  static const ZxPacketGuestVcpu* GetClass();

  static const zx_packet_guest_vcpu_interrupt_t* interrupt(const zx_packet_guest_vcpu_t* from) {
    return reinterpret_cast<const zx_packet_guest_vcpu_interrupt_t*>(&from->interrupt);
  }
  static const zx_packet_guest_vcpu_startup_t* startup(const zx_packet_guest_vcpu_t* from) {
    return reinterpret_cast<const zx_packet_guest_vcpu_startup_t*>(&from->startup);
  }
  static uint8_t type(const zx_packet_guest_vcpu_t* from) { return from->type; }
  static uint64_t reserved(const zx_packet_guest_vcpu_t* from) { return from->reserved; }

 private:
  ZxPacketGuestVcpu() : Class("zx_packet_guest_vcpu_t") {
    auto type_field = AddField(std::make_unique<ClassField<zx_packet_guest_vcpu_t, uint8_t>>(
        "type", SyscallType::kPacketGuestVcpuType, type));
    AddField(
        std::make_unique<ClassClassField<zx_packet_guest_vcpu_t, zx_packet_guest_vcpu_interrupt_t>>(
            "interrupt", interrupt, ZxPacketGuestVcpuInterrupt::GetClass()))
        ->DisplayIfEqual(type_field, uint8_t(ZX_PKT_GUEST_VCPU_INTERRUPT));
    AddField(
        std::make_unique<ClassClassField<zx_packet_guest_vcpu_t, zx_packet_guest_vcpu_startup_t>>(
            "startup", startup, ZxPacketGuestVcpuStartup::GetClass()))
        ->DisplayIfEqual(type_field, uint8_t(ZX_PKT_GUEST_VCPU_STARTUP));
    AddField(std::make_unique<ClassField<zx_packet_guest_vcpu_t, uint64_t>>(
        "reserved", SyscallType::kUint64, reserved));
  }
  ZxPacketGuestVcpu(const ZxPacketGuestVcpu&) = delete;
  ZxPacketGuestVcpu& operator=(const ZxPacketGuestVcpu&) = delete;
  static ZxPacketGuestVcpu* instance_;
};

ZxPacketGuestVcpu* ZxPacketGuestVcpu::instance_ = nullptr;

const ZxPacketGuestVcpu* ZxPacketGuestVcpu::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketGuestVcpu;
  }
  return instance_;
}

class ZxPacketInterrupt : public Class<zx_packet_interrupt_t> {
 public:
  static const ZxPacketInterrupt* GetClass();

  static zx_time_t timestamp(const zx_packet_interrupt_t* from) { return from->timestamp; }
  static uint64_t reserved0(const zx_packet_interrupt_t* from) { return from->reserved0; }
  static uint64_t reserved1(const zx_packet_interrupt_t* from) { return from->reserved1; }
  static uint64_t reserved2(const zx_packet_interrupt_t* from) { return from->reserved2; }

 private:
  ZxPacketInterrupt() : Class("zx_packet_interrupt_t") {
    AddField(std::make_unique<ClassField<zx_packet_interrupt_t, zx_time_t>>(
        "timestamp", SyscallType::kTime, timestamp));
    AddField(std::make_unique<ClassField<zx_packet_interrupt_t, uint64_t>>(
        "reserved0", SyscallType::kUint64, reserved0));
    AddField(std::make_unique<ClassField<zx_packet_interrupt_t, uint64_t>>(
        "reserved1", SyscallType::kUint64, reserved1));
    AddField(std::make_unique<ClassField<zx_packet_interrupt_t, uint64_t>>(
        "reserved2", SyscallType::kUint64, reserved2));
  }
  ZxPacketInterrupt(const ZxPacketInterrupt&) = delete;
  ZxPacketInterrupt& operator=(const ZxPacketInterrupt&) = delete;
  static ZxPacketInterrupt* instance_;
};

ZxPacketInterrupt* ZxPacketInterrupt::instance_ = nullptr;

const ZxPacketInterrupt* ZxPacketInterrupt::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketInterrupt;
  }
  return instance_;
}

class ZxPacketPageRequest : public Class<zx_packet_page_request_t> {
 public:
  static const ZxPacketPageRequest* GetClass();

  static uint16_t command(const zx_packet_page_request_t* from) { return from->command; }
  static uint16_t flags(const zx_packet_page_request_t* from) { return from->flags; }
  static uint32_t reserved0(const zx_packet_page_request_t* from) { return from->reserved0; }
  static uint64_t offset(const zx_packet_page_request_t* from) { return from->offset; }
  static uint64_t length(const zx_packet_page_request_t* from) { return from->length; }
  static uint64_t reserved1(const zx_packet_page_request_t* from) { return from->reserved1; }

 private:
  ZxPacketPageRequest() : Class("zx_packet_page_request_t") {
    AddField(std::make_unique<ClassField<zx_packet_page_request_t, uint16_t>>(
        "command", SyscallType::kPacketPageRequestCommand, command));
    AddField(std::make_unique<ClassField<zx_packet_page_request_t, uint16_t>>(
        "flags", SyscallType::kUint16, flags));
    AddField(std::make_unique<ClassField<zx_packet_page_request_t, uint32_t>>(
        "reserved0", SyscallType::kUint32, reserved0));
    AddField(std::make_unique<ClassField<zx_packet_page_request_t, uint64_t>>(
        "offset", SyscallType::kUint64, offset));
    AddField(std::make_unique<ClassField<zx_packet_page_request_t, uint64_t>>(
        "length", SyscallType::kUint64, length));
    AddField(std::make_unique<ClassField<zx_packet_page_request_t, uint64_t>>(
        "reserved1", SyscallType::kUint64, reserved1));
  }
  ZxPacketPageRequest(const ZxPacketPageRequest&) = delete;
  ZxPacketPageRequest& operator=(const ZxPacketPageRequest&) = delete;
  static ZxPacketPageRequest* instance_;
};

ZxPacketPageRequest* ZxPacketPageRequest::instance_ = nullptr;

const ZxPacketPageRequest* ZxPacketPageRequest::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPacketPageRequest;
  }
  return instance_;
}

class ZxPortPacket : public Class<zx_port_packet_t> {
 public:
  static const ZxPortPacket* GetClass();

  static uint64_t key(const zx_port_packet_t* from) { return from->key; }
  static uint32_t type(const zx_port_packet_t* from) { return from->type; }
  static zx_status_t status(const zx_port_packet_t* from) { return from->status; }
  static const zx_packet_user_t* user(const zx_port_packet_t* from) { return &from->user; }
  static const zx_packet_signal_t* signal(const zx_port_packet_t* from) { return &from->signal; }
  static const zx_packet_exception_t* exception(const zx_port_packet_t* from) {
    return &from->exception;
  }
  static const zx_packet_guest_bell_t* guest_bell(const zx_port_packet_t* from) {
    return &from->guest_bell;
  }
  static const zx_packet_guest_mem_aarch64_t* guest_mem_aarch64(const zx_port_packet_t* from) {
    return reinterpret_cast<const zx_packet_guest_mem_aarch64_t*>(&from->guest_mem);
  }
  static const zx_packet_guest_mem_x86_t* guest_mem_x86(const zx_port_packet_t* from) {
    return reinterpret_cast<const zx_packet_guest_mem_x86_t*>(&from->guest_mem);
  }
  static const zx_packet_guest_io_t* guest_io(const zx_port_packet_t* from) {
    return &from->guest_io;
  }
  static const zx_packet_guest_vcpu_t* guest_vcpu(const zx_port_packet_t* from) {
    return &from->guest_vcpu;
  }
  static const zx_packet_interrupt_t* interrupt(const zx_port_packet_t* from) {
    return &from->interrupt;
  }
  static const zx_packet_page_request_t* page_request(const zx_port_packet_t* from) {
    return &from->page_request;
  }

  static constexpr uint32_t kExceptionMask = 0xff;

 private:
  ZxPortPacket() : Class("zx_port_packet_t") {
    AddField(
        std::make_unique<ClassField<zx_port_packet_t, uint64_t>>("key", SyscallType::kUint64, key));
    auto type_field = AddField(std::make_unique<ClassField<zx_port_packet_t, uint32_t>>(
        "type", SyscallType::kPortPacketType, type));
    AddField(std::make_unique<ClassField<zx_port_packet_t, zx_status_t>>(
        "status", SyscallType::kStatus, status));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_user_t>>(
                 "user", user, ZxPacketUser::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_USER));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_signal_t>>(
                 "signal", signal, ZxPacketSignal::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_SIGNAL_ONE));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_signal_t>>(
                 "signal", signal, ZxPacketSignal::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_SIGNAL_REP));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_exception_t>>(
                 "exception", exception, ZxPacketException::GetClass()))
        ->DisplayIfMaskedEqual(type_field, kExceptionMask, uint32_t(ZX_PKT_TYPE_EXCEPTION(0)));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_bell_t>>(
                 "guest_bell", guest_bell, ZxPacketGuestBell::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_GUEST_BELL));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_mem_aarch64_t>>(
                 "guest_mem", guest_mem_aarch64, ZxPacketGuestMemAArch64::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_GUEST_MEM))
        ->DisplayIfArch(debug_ipc::Arch::kArm64);
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_mem_x86_t>>(
                 "guest_mem", guest_mem_x86, ZxPacketGuestMemX86::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_GUEST_MEM))
        ->DisplayIfArch(debug_ipc::Arch::kX64);
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_io_t>>(
                 "guest_io", guest_io, ZxPacketGuestIo::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_GUEST_IO));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_vcpu_t>>(
                 "guest_vcpu", guest_vcpu, ZxPacketGuestVcpu::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_GUEST_VCPU));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_interrupt_t>>(
                 "interrupt", interrupt, ZxPacketInterrupt::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_INTERRUPT));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_page_request_t>>(
                 "page_request", page_request, ZxPacketPageRequest::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_PAGE_REQUEST));
  }
  ZxPortPacket(const ZxPortPacket&) = delete;
  ZxPortPacket& operator=(const ZxPortPacket&) = delete;
  static ZxPortPacket* instance_;
};

ZxPortPacket* ZxPortPacket::instance_ = nullptr;

const ZxPortPacket* ZxPortPacket::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPortPacket;
  }
  return instance_;
}

class ZxWaitItem : public Class<zx_wait_item_t> {
 public:
  static const ZxWaitItem* GetClass();

  static zx_handle_t handle(const zx_wait_item_t* from) { return from->handle; }
  static zx_signals_t waitfor(const zx_wait_item_t* from) { return from->waitfor; }
  static zx_signals_t pending(const zx_wait_item_t* from) { return from->pending; }

 private:
  ZxWaitItem() : Class("zx_wait_item_t") {
    AddField(std::make_unique<ClassField<zx_wait_item_t, zx_handle_t>>(
        "handle", SyscallType::kHandle, handle));
    AddField(std::make_unique<ClassField<zx_wait_item_t, zx_signals_t>>(
        "waitfor", SyscallType::kSignals, waitfor));
    AddField(std::make_unique<ClassField<zx_wait_item_t, zx_signals_t>>(
        "pending", SyscallType::kSignals, pending));
  }
  ZxWaitItem(const ZxWaitItem&) = delete;
  ZxWaitItem& operator=(const ZxWaitItem&) = delete;
  static ZxWaitItem* instance_;
};

ZxWaitItem* ZxWaitItem::instance_ = nullptr;

const ZxWaitItem* ZxWaitItem::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxWaitItem;
  }
  return instance_;
}

void SyscallDecoderDispatcher::Populate() {
  {
    Syscall* zx_clock_get = Add("zx_clock_get", SyscallReturnType::kStatus);
    // Arguments
    auto clock_id = zx_clock_get->Argument<zx_clock_t>(SyscallType::kClock);
    auto out = zx_clock_get->PointerArgument<zx_time_t>(SyscallType::kTime);
    // Inputs
    zx_clock_get->Input<zx_clock_t>("clock_id",
                                    std::make_unique<ArgumentAccess<zx_clock_t>>(clock_id));
    // Outputs
    zx_clock_get->Output<zx_time_t>(ZX_OK, "out", std::make_unique<ArgumentAccess<zx_time_t>>(out));
  }

  { Add("zx_clock_get_monotonic", SyscallReturnType::kTime); }

  {
    Syscall* zx_nanosleep = Add("zx_nanosleep", SyscallReturnType::kStatus);
    // Arguments
    auto deadline = zx_nanosleep->Argument<zx_time_t>(SyscallType::kTime);
    // Inputs
    zx_nanosleep->Input<zx_time_t>("deadline",
                                   std::make_unique<ArgumentAccess<zx_time_t>>(deadline));
  }

  { Add("zx_ticks_get", SyscallReturnType::kTicks); }

  { Add("zx_ticks_per_second", SyscallReturnType::kTicks); }

  {
    Syscall* zx_deadline_after = Add("zx_deadline_after", SyscallReturnType::kTime);
    // Arguments
    auto nanoseconds = zx_deadline_after->Argument<zx_duration_t>(SyscallType::kDuration);
    // Inputs
    zx_deadline_after->Input<zx_duration_t>(
        "nanoseconds", std::make_unique<ArgumentAccess<zx_duration_t>>(nanoseconds));
  }

  {
    Syscall* zx_clock_adjust = Add("zx_clock_adjust", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_clock_adjust->Argument<zx_handle_t>(SyscallType::kHandle);
    auto clock_id = zx_clock_adjust->Argument<zx_clock_t>(SyscallType::kClock);
    auto offset = zx_clock_adjust->Argument<int64_t>(SyscallType::kInt64);
    // Inputs
    zx_clock_adjust->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_clock_adjust->Input<zx_clock_t>("clock_id",
                                       std::make_unique<ArgumentAccess<zx_clock_t>>(clock_id));
    zx_clock_adjust->Input<int64_t>("offset", std::make_unique<ArgumentAccess<int64_t>>(offset));
  }

  {
    Syscall* zx_object_wait_one = Add("zx_object_wait_one", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_object_wait_one->Argument<zx_handle_t>(SyscallType::kHandle);
    auto signals = zx_object_wait_one->Argument<zx_signals_t>(SyscallType::kSignals);
    auto deadline = zx_object_wait_one->Argument<zx_time_t>(SyscallType::kTime);
    auto observed = zx_object_wait_one->PointerArgument<zx_signals_t>(SyscallType::kSignals);
    // Inputs
    zx_object_wait_one->Input<zx_handle_t>("handle",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_object_wait_one->Input<zx_signals_t>(
        "signals", std::make_unique<ArgumentAccess<zx_signals_t>>(signals));
    zx_object_wait_one->Input<zx_time_t>("deadline",
                                         std::make_unique<ArgumentAccess<zx_time_t>>(deadline));
    // Outputs
    zx_object_wait_one->Output<zx_signals_t>(
        ZX_OK, "observed", std::make_unique<ArgumentAccess<zx_signals_t>>(observed));
  }

  {
    Syscall* zx_object_wait_many = Add("zx_object_wait_many", SyscallReturnType::kStatus);
    // Arguments
    auto items = zx_object_wait_many->PointerArgument<zx_wait_item_t>(SyscallType::kStruct);
    auto count = zx_object_wait_many->Argument<size_t>(SyscallType::kSize);
    auto deadline = zx_object_wait_many->Argument<zx_time_t>(SyscallType::kTime);
    // Inputs
    zx_object_wait_many->InputObjectArray<zx_wait_item_t>(
        "items", std::make_unique<ArgumentAccess<zx_wait_item_t>>(items),
        std::make_unique<ArgumentAccess<size_t>>(count), ZxWaitItem::GetClass());
    zx_object_wait_many->Input<zx_time_t>("deadline",
                                          std::make_unique<ArgumentAccess<zx_time_t>>(deadline));
    // Outputs
    zx_object_wait_many->OutputObjectArray<zx_wait_item_t>(
        ZX_OK, "items", std::make_unique<ArgumentAccess<zx_wait_item_t>>(items),
        std::make_unique<ArgumentAccess<size_t>>(count), ZxWaitItem::GetClass());
    zx_object_wait_many->OutputObjectArray<zx_wait_item_t>(
        ZX_ERR_CANCELED, "items", std::make_unique<ArgumentAccess<zx_wait_item_t>>(items),
        std::make_unique<ArgumentAccess<size_t>>(count), ZxWaitItem::GetClass());
  }

  {
    Syscall* zx_object_wait_async = Add("zx_object_wait_async", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_object_wait_async->Argument<zx_handle_t>(SyscallType::kHandle);
    auto port = zx_object_wait_async->Argument<zx_handle_t>(SyscallType::kHandle);
    auto key = zx_object_wait_async->Argument<uint64_t>(SyscallType::kUint64);
    auto signals = zx_object_wait_async->Argument<zx_signals_t>(SyscallType::kSignals);
    auto options = zx_object_wait_async->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_object_wait_async->Input<zx_handle_t>("handle",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_object_wait_async->Input<zx_handle_t>("port",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(port));
    zx_object_wait_async->Input<uint64_t>("key", std::make_unique<ArgumentAccess<uint64_t>>(key));
    zx_object_wait_async->Input<zx_signals_t>(
        "signals", std::make_unique<ArgumentAccess<zx_signals_t>>(signals));
    zx_object_wait_async->Input<uint32_t>("options",
                                          std::make_unique<ArgumentAccess<uint32_t>>(options));
  }

  {
    Syscall* zx_object_signal = Add("zx_object_signal", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_object_signal->Argument<zx_handle_t>(SyscallType::kHandle);
    auto clear_mask = zx_object_signal->Argument<uint32_t>(SyscallType::kSignals);
    auto set_mask = zx_object_signal->Argument<uint32_t>(SyscallType::kSignals);
    // Inputs
    zx_object_signal->Input<zx_handle_t>("handle",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_object_signal->Input<uint32_t>("clear_mask",
                                      std::make_unique<ArgumentAccess<uint32_t>>(clear_mask));
    zx_object_signal->Input<uint32_t>("set_mask",
                                      std::make_unique<ArgumentAccess<uint32_t>>(set_mask));
  }

  {
    Syscall* zx_object_signal_peer = Add("zx_object_signal_peer", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_object_signal_peer->Argument<zx_handle_t>(SyscallType::kHandle);
    auto clear_mask = zx_object_signal_peer->Argument<uint32_t>(SyscallType::kSignals);
    auto set_mask = zx_object_signal_peer->Argument<uint32_t>(SyscallType::kSignals);
    // Inputs
    zx_object_signal_peer->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_object_signal_peer->Input<uint32_t>("clear_mask",
                                           std::make_unique<ArgumentAccess<uint32_t>>(clear_mask));
    zx_object_signal_peer->Input<uint32_t>("set_mask",
                                           std::make_unique<ArgumentAccess<uint32_t>>(set_mask));
  }

  {
    Syscall* zx_object_get_info = Add("zx_object_get_info", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_object_get_info->Argument<zx_handle_t>(SyscallType::kHandle);
    auto topic =
        zx_object_get_info->Argument<zx_object_info_topic_t>(SyscallType::kObjectInfoTopic);
    auto buffer = zx_object_get_info->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto buffer_size = zx_object_get_info->Argument<size_t>(SyscallType::kSize);
    zx_object_get_info->PointerArgument<size_t>(SyscallType::kSize);
    zx_object_get_info->PointerArgument<size_t>(SyscallType::kSize);
    // Inputs
    zx_object_get_info->Input<zx_handle_t>("handle",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_object_get_info->Input<uint32_t>("topic", std::make_unique<ArgumentAccess<uint32_t>>(topic));
    zx_object_get_info->Input<size_t>("buffer_size",
                                      std::make_unique<ArgumentAccess<size_t>>(buffer_size));
    // Outputs
    zx_object_get_info
        ->OutputObject<zx_info_handle_basic_t>(ZX_OK, "info",
                                               std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                               ZxInfoHandleBasic::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_HANDLE_BASIC);
    zx_object_get_info
        ->OutputObject<zx_info_handle_count_t>(ZX_OK, "info",
                                               std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                               ZxInfoHandleCount::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_HANDLE_COUNT);
    zx_object_get_info
        ->OutputObject<zx_info_process_handle_stats_t>(
            ZX_OK, "info", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxInfoProcessHandleStats::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_PROCESS_HANDLE_STATS);
    zx_object_get_info
        ->OutputObject<zx_info_job_t>(
            ZX_OK, "info", std::make_unique<ArgumentAccess<uint8_t>>(buffer), ZxInfoJob::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic), ZX_INFO_JOB);
    zx_object_get_info
        ->OutputObject<zx_info_process_t>(ZX_OK, "info",
                                          std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                          ZxInfoProcess::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_PROCESS);
    zx_object_get_info
        ->OutputObject<zx_info_thread_t>(ZX_OK, "info",
                                         std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                         ZxInfoThread::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_THREAD);
    zx_object_get_info
        ->OutputObject<zx_exception_report_t>(ZX_OK, "info",
                                              std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                              ZxExceptionReport::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_THREAD_EXCEPTION_REPORT);
    zx_object_get_info
        ->OutputObject<zx_info_thread_stats_t>(ZX_OK, "info",
                                               std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                               ZxInfoThreadStats::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_THREAD_STATS);
    zx_object_get_info
        ->OutputObject<zx_info_cpu_stats_t>(ZX_OK, "info",
                                            std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                            ZxInfoCpuStats::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_CPU_STATS);
    zx_object_get_info
        ->OutputObject<zx_info_vmar_t>(ZX_OK, "info",
                                       std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                       ZxInfoVmar::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic), ZX_INFO_VMAR);
    zx_object_get_info
        ->OutputObject<zx_info_vmo_t>(
            ZX_OK, "info", std::make_unique<ArgumentAccess<uint8_t>>(buffer), ZxInfoVmo::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic), ZX_INFO_VMO);
    zx_object_get_info
        ->OutputObject<zx_info_socket_t>(ZX_OK, "info",
                                         std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                         ZxInfoSocket::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_SOCKET);
    zx_object_get_info
        ->OutputObject<zx_info_timer_t>(ZX_OK, "info",
                                        std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                        ZxInfoTimer::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_TIMER);
    zx_object_get_info
        ->OutputObject<zx_info_task_stats_t>(ZX_OK, "info",
                                             std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                             ZxInfoTaskStats::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_TASK_STATS);
    zx_object_get_info
        ->OutputObject<zx_info_kmem_stats_t>(ZX_OK, "info",
                                             std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                             ZxInfoKmemStats::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_KMEM_STATS);
    zx_object_get_info
        ->OutputObject<zx_info_resource_t>(ZX_OK, "info",
                                           std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                           ZxInfoResource::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_RESOURCE);
    zx_object_get_info
        ->OutputObject<zx_info_bti_t>(
            ZX_OK, "info", std::make_unique<ArgumentAccess<uint8_t>>(buffer), ZxInfoBti::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic), ZX_INFO_BTI);
  }

  {
    Syscall* zx_object_get_child = Add("zx_object_get_child", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_object_get_child->Argument<zx_handle_t>(SyscallType::kHandle);
    auto koid = zx_object_get_child->Argument<uint64_t>(SyscallType::kUint64);
    auto rights = zx_object_get_child->Argument<zx_rights_t>(SyscallType::kRights);
    auto out = zx_object_get_child->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_object_get_child->Input<zx_handle_t>("handle",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_object_get_child->Input<uint64_t>("koid", std::make_unique<ArgumentAccess<uint64_t>>(koid));
    zx_object_get_child->Input<zx_rights_t>("rights",
                                            std::make_unique<ArgumentAccess<zx_rights_t>>(rights));
    // Outputs
    zx_object_get_child->Output<zx_handle_t>(ZX_OK, "out",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(out));
  }

  {
    Syscall* zx_channel_create = Add("zx_channel_create", SyscallReturnType::kStatus);
    // Arguments
    auto options = zx_channel_create->Argument<uint32_t>(SyscallType::kUint32);
    auto out0 = zx_channel_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto out1 = zx_channel_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_channel_create->Input<uint32_t>("options",
                                       std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_channel_create->Output<zx_handle_t>(ZX_OK, "out0",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(out0));
    zx_channel_create->Output<zx_handle_t>(ZX_OK, "out1",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(out1));
  }

  {
    Syscall* zx_channel_read = Add("zx_channel_read", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_channel_read->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_channel_read->Argument<uint32_t>(SyscallType::kUint32);
    auto bytes = zx_channel_read->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto handles = zx_channel_read->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto num_bytes = zx_channel_read->Argument<uint32_t>(SyscallType::kUint32);
    auto num_handles = zx_channel_read->Argument<uint32_t>(SyscallType::kUint32);
    auto actual_bytes = zx_channel_read->PointerArgument<uint32_t>(SyscallType::kUint32);
    auto actual_handles = zx_channel_read->PointerArgument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_channel_read->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_channel_read->Input<uint32_t>("options",
                                     std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_channel_read->Input<uint32_t>("num_bytes",
                                     std::make_unique<ArgumentAccess<uint32_t>>(num_bytes));
    zx_channel_read->Input<uint32_t>("num_handles",
                                     std::make_unique<ArgumentAccess<uint32_t>>(num_handles));
    // Outputs
    zx_channel_read->OutputFidlMessageHandle(
        ZX_OK, "", SyscallFidlType::kInputMessage,
        std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
        std::make_unique<ArgumentAccess<uint8_t>>(bytes),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes),
        std::make_unique<ArgumentAccess<zx_handle_t>>(handles),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
    zx_channel_read->Output<uint32_t>(ZX_ERR_BUFFER_TOO_SMALL, "actual_bytes",
                                      std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes));
    zx_channel_read->Output<uint32_t>(ZX_ERR_BUFFER_TOO_SMALL, "actual_handles",
                                      std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
  }

  {
    Syscall* zx_channel_read_etc = Add("zx_channel_read_etc", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_channel_read_etc->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_channel_read_etc->Argument<uint32_t>(SyscallType::kUint32);
    auto bytes = zx_channel_read_etc->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto handles = zx_channel_read_etc->PointerArgument<zx_handle_info_t>(SyscallType::kHandle);
    auto num_bytes = zx_channel_read_etc->Argument<uint32_t>(SyscallType::kUint32);
    auto num_handles = zx_channel_read_etc->Argument<uint32_t>(SyscallType::kUint32);
    auto actual_bytes = zx_channel_read_etc->PointerArgument<uint32_t>(SyscallType::kUint32);
    auto actual_handles = zx_channel_read_etc->PointerArgument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_channel_read_etc->Input<zx_handle_t>("handle",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_channel_read_etc->Input<uint32_t>("options",
                                         std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_channel_read_etc->Input<uint32_t>("num_bytes",
                                         std::make_unique<ArgumentAccess<uint32_t>>(num_bytes));
    zx_channel_read_etc->Input<uint32_t>("num_handles",
                                         std::make_unique<ArgumentAccess<uint32_t>>(num_handles));
    // Outputs
    zx_channel_read_etc->OutputFidlMessageHandleInfo(
        ZX_OK, "", SyscallFidlType::kInputMessage,
        std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
        std::make_unique<ArgumentAccess<uint8_t>>(bytes),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes),
        std::make_unique<ArgumentAccess<zx_handle_info_t>>(handles),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
    zx_channel_read_etc->Output<uint32_t>(ZX_ERR_BUFFER_TOO_SMALL, "actual_bytes",
                                          std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes));
    zx_channel_read_etc->Output<uint32_t>(
        ZX_ERR_BUFFER_TOO_SMALL, "actual_handles",
        std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
  }

  {
    Syscall* zx_channel_write = Add("zx_channel_write", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_channel_write->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_channel_write->Argument<uint32_t>(SyscallType::kUint32);
    auto bytes = zx_channel_write->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto num_bytes = zx_channel_write->Argument<uint32_t>(SyscallType::kUint32);
    auto handles = zx_channel_write->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto num_handles = zx_channel_write->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_channel_write->Input<zx_handle_t>("handle",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_channel_write->Input<uint32_t>("options",
                                      std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_channel_write->InputFidlMessage("", SyscallFidlType::kOutputMessage,
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
                                       std::make_unique<ArgumentAccess<uint8_t>>(bytes),
                                       std::make_unique<ArgumentAccess<uint32_t>>(num_bytes),
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(handles),
                                       std::make_unique<ArgumentAccess<uint32_t>>(num_handles));
  }
  {
    Syscall* zx_channel_call = Add("zx_channel_call", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_channel_call->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_channel_call->Argument<uint32_t>(SyscallType::kUint32);
    auto deadline = zx_channel_call->Argument<zx_time_t>(SyscallType::kTime);
    auto args = zx_channel_call->PointerArgument<zx_channel_call_args_t>(SyscallType::kStruct);
    auto actual_bytes = zx_channel_call->PointerArgument<uint32_t>(SyscallType::kUint32);
    auto actual_handles = zx_channel_call->PointerArgument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_channel_call->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_channel_call->Input<uint32_t>("options",
                                     std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_channel_call->Input<zx_time_t>("deadline",
                                      std::make_unique<ArgumentAccess<zx_time_t>>(deadline));
    zx_channel_call->Input<uint32_t>(
        "rd_num_bytes", std::make_unique<FieldAccess<zx_channel_call_args, uint32_t>>(
                            args, ZxChannelCallArgs::rd_num_bytes, SyscallType::kUint32));
    zx_channel_call->Input<uint32_t>(
        "rd_num_handles", std::make_unique<FieldAccess<zx_channel_call_args, uint32_t>>(
                              args, ZxChannelCallArgs::rd_num_handles, SyscallType::kUint32));
    zx_channel_call->InputFidlMessage(
        "", SyscallFidlType::kOutputRequest, std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
        std::make_unique<PointerFieldAccess<zx_channel_call_args, uint8_t>>(
            args, ZxChannelCallArgs::wr_bytes, SyscallType::kUint8),
        std::make_unique<FieldAccess<zx_channel_call_args, uint32_t>>(
            args, ZxChannelCallArgs::wr_num_bytes, SyscallType::kUint32),
        std::make_unique<PointerFieldAccess<zx_channel_call_args, zx_handle_t>>(
            args, ZxChannelCallArgs::wr_handles, SyscallType::kHandle),
        std::make_unique<FieldAccess<zx_channel_call_args, uint32_t>>(
            args, ZxChannelCallArgs::wr_num_handles, SyscallType::kUint32));
    // Outputs
    zx_channel_call->OutputFidlMessageHandle(
        ZX_OK, "", SyscallFidlType::kInputResponse,
        std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
        std::make_unique<PointerFieldAccess<zx_channel_call_args, uint8_t>>(
            args, ZxChannelCallArgs::rd_bytes, SyscallType::kUint8),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes),
        std::make_unique<PointerFieldAccess<zx_channel_call_args, zx_handle_t>>(
            args, ZxChannelCallArgs::rd_handles, SyscallType::kHandle),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
    zx_channel_call->Output<uint32_t>(ZX_ERR_BUFFER_TOO_SMALL, "actual_bytes",
                                      std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes));
    zx_channel_call->Output<uint32_t>(ZX_ERR_BUFFER_TOO_SMALL, "actual_handles",
                                      std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
  }

  {
    Syscall* zx_port_create = Add("zx_port_create", SyscallReturnType::kStatus);
    // Arguments
    auto options = zx_port_create->Argument<uint32_t>(SyscallType::kUint32);
    auto out = zx_port_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_port_create->Input<uint32_t>("options", std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_port_create->Output<zx_handle_t>(ZX_OK, "out",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(out));
  }

  {
    Syscall* zx_port_queue = Add("zx_port_queue", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_port_queue->Argument<zx_handle_t>(SyscallType::kHandle);
    auto packet = zx_port_queue->PointerArgument<zx_port_packet_t>(SyscallType::kStruct);
    // Inputs
    zx_port_queue->Input<zx_handle_t>("handle",
                                      std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_port_queue->InputObject<zx_port_packet_t>(
        "packet", std::make_unique<ArgumentAccess<zx_port_packet_t>>(packet),
        ZxPortPacket::GetClass());
  }

  {
    Syscall* zx_port_wait = Add("zx_port_wait", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_port_wait->Argument<zx_handle_t>(SyscallType::kHandle);
    auto deadline = zx_port_wait->Argument<zx_time_t>(SyscallType::kTime);
    auto packet = zx_port_wait->PointerArgument<zx_port_packet_t>(SyscallType::kStruct);
    // Inputs
    zx_port_wait->Input<zx_handle_t>("handle",
                                     std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_port_wait->Input<zx_time_t>("deadline",
                                   std::make_unique<ArgumentAccess<zx_time_t>>(deadline));
    // Outputs
    zx_port_wait->OutputObject<zx_port_packet_t>(
        ZX_OK, "packet", std::make_unique<ArgumentAccess<zx_port_packet_t>>(packet),
        ZxPortPacket::GetClass());
  }

  {
    Syscall* zx_port_cancel = Add("zx_port_cancel", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_port_cancel->Argument<zx_handle_t>(SyscallType::kHandle);
    auto source = zx_port_cancel->Argument<zx_handle_t>(SyscallType::kHandle);
    auto key = zx_port_cancel->Argument<uint64_t>(SyscallType::kUint64);
    // Inputs
    zx_port_cancel->Input<zx_handle_t>("handle",
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_port_cancel->Input<zx_handle_t>("source",
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(source));
    zx_port_cancel->Input<uint64_t>("key", std::make_unique<ArgumentAccess<uint64_t>>(key));
  }
}

}  // namespace fidlcat
