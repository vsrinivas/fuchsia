// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/iommu.h>
#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/policy.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/profile.h>
#include <zircon/syscalls/smc.h>
#include <zircon/syscalls/system.h>

#include <cstdint>
#include <memory>

#include "tools/fidlcat/lib/statistics.h"
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
  static uint64_t pmo_count(const zx_info_bti_t* from) { return from->pmo_count; }
  static uint64_t quarantine_count(const zx_info_bti_t* from) { return from->quarantine_count; }

 private:
  ZxInfoBti() : Class("zx_info_bti_t") {
    AddField(std::make_unique<ClassField<zx_info_bti_t, uint64_t>>(
        "minimum_contiguity", SyscallType::kUint64, minimum_contiguity));
    AddField(std::make_unique<ClassField<zx_info_bti_t, uint64_t>>(
        "aspace_size", SyscallType::kUint64, aspace_size));
    AddField(std::make_unique<ClassField<zx_info_bti_t, uint64_t>>(
        "pmo_count", SyscallType::kUint64, pmo_count));
    AddField(std::make_unique<ClassField<zx_info_bti_t, uint64_t>>(
        "quarantine_count", SyscallType::kUint64, quarantine_count));
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
  static uint32_t reserved(const zx_info_handle_basic_t* from) { return from->reserved; }

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

class ZxInfoMapsMapping : public Class<zx_info_maps_mapping_t> {
 public:
  static const ZxInfoMapsMapping* GetClass();

  static zx_vm_option_t mmu_flags(const zx_info_maps_mapping_t* from) { return from->mmu_flags; }
  static zx_koid_t vmo_koid(const zx_info_maps_mapping_t* from) { return from->vmo_koid; }
  static uint64_t vmo_offset(const zx_info_maps_mapping_t* from) { return from->vmo_offset; }
  static size_t committed_pages(const zx_info_maps_mapping_t* from) {
    return from->committed_pages;
  }

 private:
  ZxInfoMapsMapping() : Class("zx_info_maps_mapping_t") {
    AddField(std::make_unique<ClassField<zx_info_maps_mapping_t, zx_vm_option_t>>(
        "mmu_flags", SyscallType::kVmOption, mmu_flags));
    AddField(std::make_unique<ClassField<zx_info_maps_mapping_t, zx_koid_t>>(
        "vmo_koid", SyscallType::kKoid, vmo_koid));
    AddField(std::make_unique<ClassField<zx_info_maps_mapping_t, uint64_t>>(
        "vmo_offset", SyscallType::kUint64, vmo_offset));
    AddField(std::make_unique<ClassField<zx_info_maps_mapping_t, size_t>>(
        "committed_pages", SyscallType::kSize, committed_pages));
  }
  ZxInfoMapsMapping(const ZxInfoMapsMapping&) = delete;
  ZxInfoMapsMapping& operator=(const ZxInfoMapsMapping&) = delete;
  static ZxInfoMapsMapping* instance_;
};

ZxInfoMapsMapping* ZxInfoMapsMapping::instance_ = nullptr;

const ZxInfoMapsMapping* ZxInfoMapsMapping::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoMapsMapping;
  }
  return instance_;
}

class ZxInfoMaps : public Class<zx_info_maps_t> {
 public:
  static const ZxInfoMaps* GetClass();

  static std::pair<const char*, size_t> name(const zx_info_maps_t* from) {
    return std::make_pair(reinterpret_cast<const char*>(from->name), sizeof(from->name));
  }
  static zx_vaddr_t base(const zx_info_maps_t* from) { return from->base; }
  static size_t size(const zx_info_maps_t* from) { return from->size; }
  static size_t depth(const zx_info_maps_t* from) { return from->depth; }
  static zx_info_maps_type_t type(const zx_info_maps_t* from) { return from->type; }
  static const zx_info_maps_mapping_t* mapping(const zx_info_maps_t* from) {
    return reinterpret_cast<const zx_info_maps_mapping_t*>(&from->u.mapping);
  }

 private:
  ZxInfoMaps() : Class("zx_info_maps_t") {
    AddField(std::make_unique<DynamicArrayField<zx_info_maps_t, char, size_t>>(
        "name", SyscallType::kChar, name));
    AddField(std::make_unique<ClassField<zx_info_maps_t, zx_vaddr_t>>("base", SyscallType::kVaddr,
                                                                      base));
    AddField(
        std::make_unique<ClassField<zx_info_maps_t, size_t>>("size", SyscallType::kSize, size));
    AddField(
        std::make_unique<ClassField<zx_info_maps_t, size_t>>("depth", SyscallType::kSize, depth));
    auto type_field = AddField(std::make_unique<ClassField<zx_info_maps_t, zx_info_maps_type_t>>(
        "type", SyscallType::kInfoMapsType, type));
    AddField(std::make_unique<ClassClassField<zx_info_maps_t, zx_info_maps_mapping_t>>(
                 "mapping", mapping, ZxInfoMapsMapping::GetClass()))
        ->DisplayIfEqual(type_field, ZX_INFO_MAPS_TYPE_MAPPING);
  }
  ZxInfoMaps(const ZxInfoMaps&) = delete;
  ZxInfoMaps& operator=(const ZxInfoMaps&) = delete;
  static ZxInfoMaps* instance_;
};

ZxInfoMaps* ZxInfoMaps::instance_ = nullptr;

const ZxInfoMaps* ZxInfoMaps::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxInfoMaps;
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
    AddField(std::make_unique<DynamicArrayField<zx_info_process_handle_stats_t, uint32_t, int>>(
        "handle_count", SyscallType::kUint32, handle_count));
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
    AddField(std::make_unique<DynamicArrayField<zx_info_resource_t, char, size_t>>(
        "name", SyscallType::kChar, name));
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
    AddField(std::make_unique<DynamicArrayField<zx_cpu_set_t, uint64_t, int>>(
        "mask", SyscallType::kUint64Hexa, mask));
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
  static uint64_t metadata_bytes(const zx_info_vmo_t* from) { return from->metadata_bytes; }
  static uint64_t committed_change_events(const zx_info_vmo_t* from) {
    return from->committed_change_events;
  }

 private:
  ZxInfoVmo() : Class("zx_info_vmo_t") {
    AddField(
        std::make_unique<ClassField<zx_info_vmo_t, zx_koid_t>>("koid", SyscallType::kKoid, koid));
    AddField(std::make_unique<DynamicArrayField<zx_info_vmo_t, char, size_t>>(
        "name", SyscallType::kChar, name));
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
    AddField(std::make_unique<ClassField<zx_info_vmo_t, uint64_t>>(
        "metadata_bytes", SyscallType::kUint64, metadata_bytes));
    AddField(std::make_unique<ClassField<zx_info_vmo_t, uint64_t>>(
        "committed_change_events", SyscallType::kUint64, committed_change_events));
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

class ZxIommuDescIntel : public Class<zx_iommu_desc_intel_t> {
 public:
  static const ZxIommuDescIntel* GetClass();

  static uint64_t register_base(const zx_iommu_desc_intel_t* from) { return from->register_base; }
  static uint16_t pci_segment(const zx_iommu_desc_intel_t* from) { return from->pci_segment; }
  static bool whole_segment(const zx_iommu_desc_intel_t* from) { return from->whole_segment; }
  static uint8_t scope_bytes(const zx_iommu_desc_intel_t* from) { return from->scope_bytes; }
  static uint16_t reserved_memory_bytes(const zx_iommu_desc_intel_t* from) {
    return from->reserved_memory_bytes;
  }

 private:
  ZxIommuDescIntel() : Class("zx_iommu_desc_intel_t") {
    AddField(std::make_unique<ClassField<zx_iommu_desc_intel_t, uint64_t>>(
        "register_base", SyscallType::kPaddr, register_base));
    AddField(std::make_unique<ClassField<zx_iommu_desc_intel_t, uint16_t>>(
        "pci_segment", SyscallType::kUint16, pci_segment));
    AddField(std::make_unique<ClassField<zx_iommu_desc_intel_t, bool>>(
        "whole_segment", SyscallType::kBool, whole_segment));
    AddField(std::make_unique<ClassField<zx_iommu_desc_intel_t, uint8_t>>(
        "scope_bytes", SyscallType::kUint8, scope_bytes));
    AddField(std::make_unique<ClassField<zx_iommu_desc_intel_t, uint16_t>>(
        "reserved_memory_bytes", SyscallType::kUint16, reserved_memory_bytes));
  }
  ZxIommuDescIntel(const ZxIommuDescIntel&) = delete;
  ZxIommuDescIntel& operator=(const ZxIommuDescIntel&) = delete;
  static ZxIommuDescIntel* instance_;
};

ZxIommuDescIntel* ZxIommuDescIntel::instance_ = nullptr;

const ZxIommuDescIntel* ZxIommuDescIntel::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxIommuDescIntel;
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
    AddField(std::make_unique<DynamicArrayField<zx_packet_user_t, uint64_t, int>>(
        "u64", SyscallType::kUint64Hexa, u64));
    AddField(std::make_unique<DynamicArrayField<zx_packet_user_t, uint32_t, int>>(
        "u32", SyscallType::kUint32Hexa, u32));
    AddField(std::make_unique<DynamicArrayField<zx_packet_user_t, uint16_t, int>>(
        "u16", SyscallType::kUint16Hexa, u16));
    AddField(std::make_unique<DynamicArrayField<zx_packet_user_t, uint8_t, int>>(
        "u8", SyscallType::kUint8Hexa, c8));
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
    AddField(std::make_unique<DynamicArrayField<zx_packet_guest_mem_x86_t, uint8_t, int>>(
        "inst_buf", SyscallType::kUint8Hexa, inst_buf));
    AddField(std::make_unique<ClassField<zx_packet_guest_mem_x86_t, uint8_t>>(
        "default_operand_size", SyscallType::kUint8, default_operand_size));
    AddField(std::make_unique<DynamicArrayField<zx_packet_guest_mem_x86_t, uint8_t, int>>(
        "reserved", SyscallType::kUint8Hexa, reserved));
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
    AddField(std::make_unique<DynamicArrayField<zx_packet_guest_io_t, uint8_t, int>>(
        "data", SyscallType::kUint8Hexa, data));
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

class ZxPciBar : public Class<zx_pci_bar_t> {
 public:
  static const ZxPciBar* GetClass();

  static uint32_t id(const zx_pci_bar_t* from) { return from->id; }
  static uint32_t type(const zx_pci_bar_t* from) { return from->type; }
  static size_t size(const zx_pci_bar_t* from) { return from->size; }
  static uintptr_t addr(const zx_pci_bar_t* from) { return from->addr; }
  static zx_handle_t handle(const zx_pci_bar_t* from) { return from->handle; }

 private:
  ZxPciBar() : Class("zx_pci_bar_t") {
    AddField(std::make_unique<ClassField<zx_pci_bar_t, uint32_t>>("id", SyscallType::kUint32, id));
    auto type_field = AddField(std::make_unique<ClassField<zx_pci_bar_t, uint32_t>>(
        "type", SyscallType::kPciBarType, type));
    AddField(std::make_unique<ClassField<zx_pci_bar_t, size_t>>("size", SyscallType::kSize, size))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PCI_BAR_TYPE_PIO));
    AddField(
        std::make_unique<ClassField<zx_pci_bar_t, uintptr_t>>("addr", SyscallType::kUintptr, addr))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PCI_BAR_TYPE_PIO));
    AddField(std::make_unique<ClassField<zx_pci_bar_t, zx_handle_t>>("handle", SyscallType::kHandle,
                                                                     handle))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PCI_BAR_TYPE_MMIO));
  }
  ZxPciBar(const ZxPciBar&) = delete;
  ZxPciBar& operator=(const ZxPciBar&) = delete;
  static ZxPciBar* instance_;
};

ZxPciBar* ZxPciBar::instance_ = nullptr;

const ZxPciBar* ZxPciBar::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPciBar;
  }
  return instance_;
}

class ZxPcieDeviceInfo : public Class<zx_pcie_device_info_t> {
 public:
  static const ZxPcieDeviceInfo* GetClass();

  static uint16_t vendor_id(const zx_pcie_device_info_t* from) { return from->vendor_id; }
  static uint16_t device_id(const zx_pcie_device_info_t* from) { return from->device_id; }
  static uint8_t base_class(const zx_pcie_device_info_t* from) { return from->base_class; }
  static uint8_t sub_class(const zx_pcie_device_info_t* from) { return from->sub_class; }
  static uint8_t program_interface(const zx_pcie_device_info_t* from) {
    return from->program_interface;
  }
  static uint8_t revision_id(const zx_pcie_device_info_t* from) { return from->revision_id; }
  static uint8_t bus_id(const zx_pcie_device_info_t* from) { return from->bus_id; }
  static uint8_t dev_id(const zx_pcie_device_info_t* from) { return from->dev_id; }
  static uint8_t func_id(const zx_pcie_device_info_t* from) { return from->func_id; }

 private:
  ZxPcieDeviceInfo() : Class("zx_pcie_device_info_t") {
    AddField(std::make_unique<ClassField<zx_pcie_device_info_t, uint16_t>>(
        "vendor_id", SyscallType::kUint16, vendor_id));
    AddField(std::make_unique<ClassField<zx_pcie_device_info_t, uint16_t>>(
        "device_id", SyscallType::kUint16, device_id));
    AddField(std::make_unique<ClassField<zx_pcie_device_info_t, uint8_t>>(
        "base_class", SyscallType::kUint8, base_class));
    AddField(std::make_unique<ClassField<zx_pcie_device_info_t, uint8_t>>(
        "sub_class", SyscallType::kUint8, sub_class));
    AddField(std::make_unique<ClassField<zx_pcie_device_info_t, uint8_t>>(
        "program_interface", SyscallType::kUint8, program_interface));
    AddField(std::make_unique<ClassField<zx_pcie_device_info_t, uint8_t>>(
        "revision_id", SyscallType::kUint8, revision_id));
    AddField(std::make_unique<ClassField<zx_pcie_device_info_t, uint8_t>>(
        "bus_id", SyscallType::kUint8, bus_id));
    AddField(std::make_unique<ClassField<zx_pcie_device_info_t, uint8_t>>(
        "dev_id", SyscallType::kUint8, dev_id));
    AddField(std::make_unique<ClassField<zx_pcie_device_info_t, uint8_t>>(
        "func_id", SyscallType::kUint8, func_id));
  }
  ZxPcieDeviceInfo(const ZxPcieDeviceInfo&) = delete;
  ZxPcieDeviceInfo& operator=(const ZxPcieDeviceInfo&) = delete;
  static ZxPcieDeviceInfo* instance_;
};

ZxPcieDeviceInfo* ZxPcieDeviceInfo::instance_ = nullptr;

const ZxPcieDeviceInfo* ZxPcieDeviceInfo::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPcieDeviceInfo;
  }
  return instance_;
}

class ZxPciInitArgIrq : public Class<zx_pci_init_arg_irq_t> {
 public:
  static const ZxPciInitArgIrq* GetClass();

  static uint32_t global_irq(const zx_pci_init_arg_irq_t* from) { return from->global_irq; }
  static bool level_triggered(const zx_pci_init_arg_irq_t* from) { return from->level_triggered; }
  static bool active_high(const zx_pci_init_arg_irq_t* from) { return from->active_high; }

 private:
  ZxPciInitArgIrq() : Class("zx_pci_init_arg_irq_t") {
    AddField(std::make_unique<ClassField<zx_pci_init_arg_irq_t, uint32_t>>(
        "global_irq", SyscallType::kUint32, global_irq));
    AddField(std::make_unique<ClassField<zx_pci_init_arg_irq_t, bool>>(
        "level_triggered", SyscallType::kBool, level_triggered));
    AddField(std::make_unique<ClassField<zx_pci_init_arg_irq_t, bool>>(
        "active_high", SyscallType::kBool, active_high));
  }
  ZxPciInitArgIrq(const ZxPciInitArgIrq&) = delete;
  ZxPciInitArgIrq& operator=(const ZxPciInitArgIrq&) = delete;
  static ZxPciInitArgIrq* instance_;
};

ZxPciInitArgIrq* ZxPciInitArgIrq::instance_ = nullptr;

const ZxPciInitArgIrq* ZxPciInitArgIrq::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPciInitArgIrq;
  }
  return instance_;
}

class ZxPciInitArgAddrWindow : public Class<zx_pci_init_arg_addr_window_t> {
 public:
  static const ZxPciInitArgAddrWindow* GetClass();

  static uint64_t base(const zx_pci_init_arg_addr_window_t* from) { return from->base; }
  static size_t size(const zx_pci_init_arg_addr_window_t* from) { return from->size; }
  static uint8_t bus_start(const zx_pci_init_arg_addr_window_t* from) { return from->bus_start; }
  static uint8_t bus_end(const zx_pci_init_arg_addr_window_t* from) { return from->bus_end; }
  static uint8_t cfg_space_type(const zx_pci_init_arg_addr_window_t* from) {
    return from->cfg_space_type;
  }
  static bool has_ecam(const zx_pci_init_arg_addr_window_t* from) { return from->has_ecam; }

 private:
  ZxPciInitArgAddrWindow() : Class("zx_pci_init_arg_addr_window_t") {
    AddField(std::make_unique<ClassField<zx_pci_init_arg_addr_window_t, uint64_t>>(
        "base", SyscallType::kUint64, base));
    AddField(std::make_unique<ClassField<zx_pci_init_arg_addr_window_t, size_t>>(
        "size", SyscallType::kSize, size));
    AddField(std::make_unique<ClassField<zx_pci_init_arg_addr_window_t, uint8_t>>(
        "bus_start", SyscallType::kUint8, bus_start));
    AddField(std::make_unique<ClassField<zx_pci_init_arg_addr_window_t, uint8_t>>(
        "bus_end", SyscallType::kUint8, bus_end));
    AddField(std::make_unique<ClassField<zx_pci_init_arg_addr_window_t, uint8_t>>(
        "cfg_space_type", SyscallType::kUint8, cfg_space_type));
    AddField(std::make_unique<ClassField<zx_pci_init_arg_addr_window_t, bool>>(
        "has_ecam", SyscallType::kBool, has_ecam));
  }
  ZxPciInitArgAddrWindow(const ZxPciInitArgAddrWindow&) = delete;
  ZxPciInitArgAddrWindow& operator=(const ZxPciInitArgAddrWindow&) = delete;
  static ZxPciInitArgAddrWindow* instance_;
};

ZxPciInitArgAddrWindow* ZxPciInitArgAddrWindow::instance_ = nullptr;

const ZxPciInitArgAddrWindow* ZxPciInitArgAddrWindow::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPciInitArgAddrWindow;
  }
  return instance_;
}

class ZxPciInitArg : public Class<zx_pci_init_arg_t> {
 public:
  static const ZxPciInitArg* GetClass();

  static std::pair<const uint32_t*, int> dev_pin_to_global_irq(const zx_pci_init_arg_t* from) {
    return std::make_pair(reinterpret_cast<const uint32_t*>(from->dev_pin_to_global_irq),
                          sizeof(from->dev_pin_to_global_irq) / sizeof(uint32_t));
  }
  static uint32_t num_irqs(const zx_pci_init_arg_t* from) { return from->num_irqs; }
  static const zx_pci_init_arg_irq_t* irqs(const zx_pci_init_arg_t* from) {
    return reinterpret_cast<const zx_pci_init_arg_irq_t*>(from->irqs);
  }
  static uint32_t addr_window_count(const zx_pci_init_arg_t* from) {
    return from->addr_window_count;
  }
  static const zx_pci_init_arg_addr_window_t* addr_windows(const zx_pci_init_arg_t* from) {
    return reinterpret_cast<const zx_pci_init_arg_addr_window_t*>(from->addr_windows);
  }

 private:
  ZxPciInitArg() : Class("zx_pci_init_arg_t") {
    AddField(std::make_unique<ArrayField<zx_pci_init_arg_t, uint32_t>>(
        "dev_pin_to_global_irq", SyscallType::kUint32Hexa, dev_pin_to_global_irq));
    AddField(std::make_unique<ClassField<zx_pci_init_arg_t, uint32_t>>(
        "num_irqs", SyscallType::kUint32, num_irqs));
    AddField(std::make_unique<DynamicArrayClassField<zx_pci_init_arg_t, zx_pci_init_arg_irq_t>>(
        "irqs", irqs, num_irqs, ZxPciInitArgIrq::GetClass()));
    AddField(std::make_unique<ClassField<zx_pci_init_arg_t, uint32_t>>(
        "addr_window_count", SyscallType::kUint32, addr_window_count));
    AddField(
        std::make_unique<DynamicArrayClassField<zx_pci_init_arg_t, zx_pci_init_arg_addr_window_t>>(
            "addr_windows", addr_windows, addr_window_count, ZxPciInitArgAddrWindow::GetClass()));
  }
  ZxPciInitArg(const ZxPciInitArg&) = delete;
  ZxPciInitArg& operator=(const ZxPciInitArg&) = delete;
  static ZxPciInitArg* instance_;
};

ZxPciInitArg* ZxPciInitArg::instance_ = nullptr;

const ZxPciInitArg* ZxPciInitArg::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPciInitArg;
  }
  return instance_;
}

class ZxPolicyBasic : public Class<zx_policy_basic_t> {
 public:
  static const ZxPolicyBasic* GetClass();

  static uint32_t condition(const zx_policy_basic_t* from) { return from->condition; }
  static uint32_t policy(const zx_policy_basic_t* from) { return from->policy; }

 private:
  ZxPolicyBasic() : Class("zx_policy_basic_t") {
    AddField(std::make_unique<ClassField<zx_policy_basic_t, uint32_t>>(
        "condition", SyscallType::kPolicyCondition, condition));
    AddField(std::make_unique<ClassField<zx_policy_basic_t, uint32_t>>(
        "policy", SyscallType::kPolicyAction, policy));
  }
  ZxPolicyBasic(const ZxPolicyBasic&) = delete;
  ZxPolicyBasic& operator=(const ZxPolicyBasic&) = delete;
  static ZxPolicyBasic* instance_;
};

ZxPolicyBasic* ZxPolicyBasic::instance_ = nullptr;

const ZxPolicyBasic* ZxPolicyBasic::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPolicyBasic;
  }
  return instance_;
}

class ZxPolicyTimerSlack : public Class<zx_policy_timer_slack_t> {
 public:
  static const ZxPolicyTimerSlack* GetClass();

  static zx_duration_t min_slack(const zx_policy_timer_slack_t* from) { return from->min_slack; }
  static uint32_t default_mode(const zx_policy_timer_slack_t* from) { return from->default_mode; }

 private:
  ZxPolicyTimerSlack() : Class("zx_policy_timer_slack_t") {
    AddField(std::make_unique<ClassField<zx_policy_timer_slack_t, zx_duration_t>>(
        "min_slack", SyscallType::kDuration, min_slack));
    AddField(std::make_unique<ClassField<zx_policy_timer_slack_t, uint32_t>>(
        "default_mode", SyscallType::kTimerOption, default_mode));
  }
  ZxPolicyTimerSlack(const ZxPolicyTimerSlack&) = delete;
  ZxPolicyTimerSlack& operator=(const ZxPolicyTimerSlack&) = delete;
  static ZxPolicyTimerSlack* instance_;
};

ZxPolicyTimerSlack* ZxPolicyTimerSlack::instance_ = nullptr;

const ZxPolicyTimerSlack* ZxPolicyTimerSlack::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxPolicyTimerSlack;
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

 private:
  ZxPortPacket() : Class("zx_port_packet_t") {
    // Conditional field Ids
    const uint8_t kZxPortPacket_ZX_PKT_TYPE_GUEST_MEM_Arm64 = 1;
    const uint8_t kZxPortPacket_ZX_PKT_TYPE_GUEST_MEM_X64 = 2;
    // Fields
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
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_bell_t>>(
                 "guest_bell", guest_bell, ZxPacketGuestBell::GetClass()))
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_GUEST_BELL));
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_mem_aarch64_t>>(
                 "guest_mem", guest_mem_aarch64, ZxPacketGuestMemAArch64::GetClass()),
             kZxPortPacket_ZX_PKT_TYPE_GUEST_MEM_Arm64)
        ->DisplayIfEqual(type_field, uint32_t(ZX_PKT_TYPE_GUEST_MEM))
        ->DisplayIfArch(debug_ipc::Arch::kArm64);
    AddField(std::make_unique<ClassClassField<zx_port_packet_t, zx_packet_guest_mem_x86_t>>(
                 "guest_mem", guest_mem_x86, ZxPacketGuestMemX86::GetClass()),
             kZxPortPacket_ZX_PKT_TYPE_GUEST_MEM_X64)
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

class ZxProfileInfo : public Class<zx_profile_info_t> {
 public:
  static const ZxProfileInfo* GetClass();

  static uint32_t flags(const zx_profile_info_t* from) { return from->flags; }
  static int32_t priority(const zx_profile_info_t* from) { return from->priority; }
  static const zx_cpu_set_t* cpu_affinity_mask(const zx_profile_info_t* from) {
    return &from->cpu_affinity_mask;
  }

 private:
  ZxProfileInfo() : Class("zx_profile_info_t") {
    AddField(std::make_unique<ClassField<zx_profile_info_t, uint32_t>>(
        "flags", SyscallType::kProfileInfoFlags, flags));
    AddField(std::make_unique<ClassField<zx_profile_info_t, int32_t>>(
        "priority", SyscallType::kInt32, priority));
    AddField(std::make_unique<ClassClassField<zx_profile_info_t, zx_cpu_set_t>>(
        "cpu_affinity_mask", cpu_affinity_mask, ZxCpuSet::GetClass()));
  }
  ZxProfileInfo(const ZxProfileInfo&) = delete;
  ZxProfileInfo& operator=(const ZxProfileInfo&) = delete;
  static ZxProfileInfo* instance_;
};

ZxProfileInfo* ZxProfileInfo::instance_ = nullptr;

const ZxProfileInfo* ZxProfileInfo::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxProfileInfo;
  }
  return instance_;
}

class ZxSmcParameters : public Class<zx_smc_parameters_t> {
 public:
  static const ZxSmcParameters* GetClass();

  static uint32_t func_id(const zx_smc_parameters_t* from) { return from->func_id; }
  static uint64_t arg1(const zx_smc_parameters_t* from) { return from->arg1; }
  static uint64_t arg2(const zx_smc_parameters_t* from) { return from->arg2; }
  static uint64_t arg3(const zx_smc_parameters_t* from) { return from->arg3; }
  static uint64_t arg4(const zx_smc_parameters_t* from) { return from->arg4; }
  static uint64_t arg5(const zx_smc_parameters_t* from) { return from->arg5; }
  static uint64_t arg6(const zx_smc_parameters_t* from) { return from->arg6; }
  static uint16_t client_id(const zx_smc_parameters_t* from) { return from->client_id; }
  static uint16_t secure_os_id(const zx_smc_parameters_t* from) { return from->secure_os_id; }

 private:
  ZxSmcParameters() : Class("zx_smc_parameters_t") {
    AddField(std::make_unique<ClassField<zx_smc_parameters_t, uint32_t>>(
        "func_id", SyscallType::kUint32, func_id));
    AddField(std::make_unique<ClassField<zx_smc_parameters_t, uint64_t>>(
        "arg1", SyscallType::kUint64, arg1));
    AddField(std::make_unique<ClassField<zx_smc_parameters_t, uint64_t>>(
        "arg2", SyscallType::kUint64, arg2));
    AddField(std::make_unique<ClassField<zx_smc_parameters_t, uint64_t>>(
        "arg3", SyscallType::kUint64, arg3));
    AddField(std::make_unique<ClassField<zx_smc_parameters_t, uint64_t>>(
        "arg4", SyscallType::kUint64, arg4));
    AddField(std::make_unique<ClassField<zx_smc_parameters_t, uint64_t>>(
        "arg5", SyscallType::kUint64, arg5));
    AddField(std::make_unique<ClassField<zx_smc_parameters_t, uint64_t>>(
        "arg6", SyscallType::kUint64, arg6));
    AddField(std::make_unique<ClassField<zx_smc_parameters_t, uint16_t>>(
        "client_id", SyscallType::kUint16, client_id));
    AddField(std::make_unique<ClassField<zx_smc_parameters_t, uint16_t>>(
        "secure_os_id", SyscallType::kUint16, secure_os_id));
  }
  ZxSmcParameters(const ZxSmcParameters&) = delete;
  ZxSmcParameters& operator=(const ZxSmcParameters&) = delete;
  static ZxSmcParameters* instance_;
};

ZxSmcParameters* ZxSmcParameters::instance_ = nullptr;

const ZxSmcParameters* ZxSmcParameters::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxSmcParameters;
  }
  return instance_;
}

class ZxSmcResult : public Class<zx_smc_result_t> {
 public:
  static const ZxSmcResult* GetClass();

  static uint64_t arg0(const zx_smc_result_t* from) { return from->arg0; }
  static uint64_t arg1(const zx_smc_result_t* from) { return from->arg1; }
  static uint64_t arg2(const zx_smc_result_t* from) { return from->arg2; }
  static uint64_t arg3(const zx_smc_result_t* from) { return from->arg3; }
  static uint64_t arg6(const zx_smc_result_t* from) { return from->arg6; }

 private:
  ZxSmcResult() : Class("zx_smc_result_t") {
    AddField(std::make_unique<ClassField<zx_smc_result_t, uint64_t>>("arg0", SyscallType::kUint64,
                                                                     arg0));
    AddField(std::make_unique<ClassField<zx_smc_result_t, uint64_t>>("arg1", SyscallType::kUint64,
                                                                     arg1));
    AddField(std::make_unique<ClassField<zx_smc_result_t, uint64_t>>("arg2", SyscallType::kUint64,
                                                                     arg2));
    AddField(std::make_unique<ClassField<zx_smc_result_t, uint64_t>>("arg3", SyscallType::kUint64,
                                                                     arg3));
    AddField(std::make_unique<ClassField<zx_smc_result_t, uint64_t>>("arg6", SyscallType::kUint64,
                                                                     arg6));
  }
  ZxSmcResult(const ZxSmcResult&) = delete;
  ZxSmcResult& operator=(const ZxSmcResult&) = delete;
  static ZxSmcResult* instance_;
};

ZxSmcResult* ZxSmcResult::instance_ = nullptr;

const ZxSmcResult* ZxSmcResult::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxSmcResult;
  }
  return instance_;
}

class ZxSystemPowerctlArgAcpi : public Class<zx_system_powerctl_arg_t> {
 public:
  static const ZxSystemPowerctlArgAcpi* GetClass();

  static uint8_t target_s_state(const zx_system_powerctl_arg_t* from) {
    return from->acpi_transition_s_state.target_s_state;
  }
  static uint8_t sleep_type_a(const zx_system_powerctl_arg_t* from) {
    return from->acpi_transition_s_state.sleep_type_a;
  }
  static uint8_t sleep_type_b(const zx_system_powerctl_arg_t* from) {
    return from->acpi_transition_s_state.sleep_type_b;
  }

 private:
  ZxSystemPowerctlArgAcpi() : Class("zx_system_powerctl_arg_t") {
    AddField(std::make_unique<ClassField<zx_system_powerctl_arg_t, uint8_t>>(
        "target_s_state", SyscallType::kUint8, target_s_state));
    AddField(std::make_unique<ClassField<zx_system_powerctl_arg_t, uint8_t>>(
        "sleep_type_a", SyscallType::kUint8, sleep_type_a));
    AddField(std::make_unique<ClassField<zx_system_powerctl_arg_t, uint8_t>>(
        "sleep_type_b", SyscallType::kUint8, sleep_type_b));
  }
  ZxSystemPowerctlArgAcpi(const ZxSystemPowerctlArgAcpi&) = delete;
  ZxSystemPowerctlArgAcpi& operator=(const ZxSystemPowerctlArgAcpi&) = delete;
  static ZxSystemPowerctlArgAcpi* instance_;
};

ZxSystemPowerctlArgAcpi* ZxSystemPowerctlArgAcpi::instance_ = nullptr;

const ZxSystemPowerctlArgAcpi* ZxSystemPowerctlArgAcpi::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxSystemPowerctlArgAcpi;
  }
  return instance_;
}

class ZxSystemPowerctlArgX86PowerLimit : public Class<zx_system_powerctl_arg_t> {
 public:
  static const ZxSystemPowerctlArgX86PowerLimit* GetClass();

  static uint32_t power_limit(const zx_system_powerctl_arg_t* from) {
    return from->x86_power_limit.power_limit;
  }
  static uint32_t time_window(const zx_system_powerctl_arg_t* from) {
    return from->x86_power_limit.time_window;
  }
  static uint8_t clamp(const zx_system_powerctl_arg_t* from) { return from->x86_power_limit.clamp; }
  static uint8_t enable(const zx_system_powerctl_arg_t* from) {
    return from->x86_power_limit.enable;
  }

 private:
  ZxSystemPowerctlArgX86PowerLimit() : Class("zx_system_powerctl_arg_t") {
    AddField(std::make_unique<ClassField<zx_system_powerctl_arg_t, uint32_t>>(
        "power_limit", SyscallType::kUint32, power_limit));
    AddField(std::make_unique<ClassField<zx_system_powerctl_arg_t, uint32_t>>(
        "time_window", SyscallType::kUint32, time_window));
    AddField(std::make_unique<ClassField<zx_system_powerctl_arg_t, uint8_t>>(
        "clamp", SyscallType::kUint8, clamp));
    AddField(std::make_unique<ClassField<zx_system_powerctl_arg_t, uint8_t>>(
        "enable", SyscallType::kUint8, enable));
  }
  ZxSystemPowerctlArgX86PowerLimit(const ZxSystemPowerctlArgX86PowerLimit&) = delete;
  ZxSystemPowerctlArgX86PowerLimit& operator=(const ZxSystemPowerctlArgX86PowerLimit&) = delete;
  static ZxSystemPowerctlArgX86PowerLimit* instance_;
};

ZxSystemPowerctlArgX86PowerLimit* ZxSystemPowerctlArgX86PowerLimit::instance_ = nullptr;

const ZxSystemPowerctlArgX86PowerLimit* ZxSystemPowerctlArgX86PowerLimit::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxSystemPowerctlArgX86PowerLimit;
  }
  return instance_;
}

class ZxThreadStateDebugRegsAArch64Bp : public Class<zx_thread_state_debug_regs_aarch64_bp_t> {
 public:
  static const ZxThreadStateDebugRegsAArch64Bp* GetClass();

  static uint32_t dbgbcr(const zx_thread_state_debug_regs_aarch64_bp_t* from) {
    return from->dbgbcr;
  }
  static uint64_t dbgbvr(const zx_thread_state_debug_regs_aarch64_bp_t* from) {
    return from->dbgbvr;
  }

 private:
  ZxThreadStateDebugRegsAArch64Bp() : Class("zx_thread_state_debug_regs_aarch64_bp_t") {
    AddField(std::make_unique<ClassField<zx_thread_state_debug_regs_aarch64_bp_t, uint32_t>>(
        "dbgbcr", SyscallType::kUint32Hexa, dbgbcr));
    AddField(std::make_unique<ClassField<zx_thread_state_debug_regs_aarch64_bp_t, uint64_t>>(
        "dbgbvr", SyscallType::kUint64Hexa, dbgbvr));
  }
  ZxThreadStateDebugRegsAArch64Bp(const ZxThreadStateDebugRegsAArch64Bp&) = delete;
  ZxThreadStateDebugRegsAArch64Bp& operator=(const ZxThreadStateDebugRegsAArch64Bp&) = delete;
  static ZxThreadStateDebugRegsAArch64Bp* instance_;
};

ZxThreadStateDebugRegsAArch64Bp* ZxThreadStateDebugRegsAArch64Bp::instance_ = nullptr;

const ZxThreadStateDebugRegsAArch64Bp* ZxThreadStateDebugRegsAArch64Bp::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxThreadStateDebugRegsAArch64Bp;
  }
  return instance_;
}

class ZxThreadStateDebugRegsAArch64Wp : public Class<zx_thread_state_debug_regs_aarch64_wp_t> {
 public:
  static const ZxThreadStateDebugRegsAArch64Wp* GetClass();

  static uint32_t dbgwcr(const zx_thread_state_debug_regs_aarch64_wp_t* from) {
    return from->dbgwcr;
  }
  static uint64_t dbgwvr(const zx_thread_state_debug_regs_aarch64_wp_t* from) {
    return from->dbgwvr;
  }

 private:
  ZxThreadStateDebugRegsAArch64Wp() : Class("zx_thread_state_debug_regs_aarch64_wp_t") {
    AddField(std::make_unique<ClassField<zx_thread_state_debug_regs_aarch64_wp_t, uint32_t>>(
        "dbgwcr", SyscallType::kUint32Hexa, dbgwcr));
    AddField(std::make_unique<ClassField<zx_thread_state_debug_regs_aarch64_wp_t, uint64_t>>(
        "dbgwvr", SyscallType::kUint64Hexa, dbgwvr));
  }
  ZxThreadStateDebugRegsAArch64Wp(const ZxThreadStateDebugRegsAArch64Wp&) = delete;
  ZxThreadStateDebugRegsAArch64Wp& operator=(const ZxThreadStateDebugRegsAArch64Wp&) = delete;
  static ZxThreadStateDebugRegsAArch64Wp* instance_;
};

ZxThreadStateDebugRegsAArch64Wp* ZxThreadStateDebugRegsAArch64Wp::instance_ = nullptr;

const ZxThreadStateDebugRegsAArch64Wp* ZxThreadStateDebugRegsAArch64Wp::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxThreadStateDebugRegsAArch64Wp;
  }
  return instance_;
}

class ZxThreadStateDebugRegsAArch64 : public Class<zx_thread_state_debug_regs_aarch64_t> {
 public:
  static const ZxThreadStateDebugRegsAArch64* GetClass();

  static std::pair<const zx_thread_state_debug_regs_aarch64_bp_t*, int> hw_bps(
      const zx_thread_state_debug_regs_aarch64_t* from) {
    return std::make_pair(
        reinterpret_cast<const zx_thread_state_debug_regs_aarch64_bp_t*>(from->hw_bps),
        sizeof(from->hw_bps) / sizeof(from->hw_bps[0]));
  }
  static uint8_t hw_bps_count(const zx_thread_state_debug_regs_aarch64_t* from) {
    return from->hw_bps_count;
  }
  static std::pair<const zx_thread_state_debug_regs_aarch64_wp_t*, int> hw_wps(
      const zx_thread_state_debug_regs_aarch64_t* from) {
    return std::make_pair(
        reinterpret_cast<const zx_thread_state_debug_regs_aarch64_wp_t*>(from->hw_wps),
        sizeof(from->hw_wps) / sizeof(from->hw_wps[0]));
  }
  static uint8_t hw_wps_count(const zx_thread_state_debug_regs_aarch64_t* from) {
    return from->hw_wps_count;
  }
  static uint32_t esr(const zx_thread_state_debug_regs_aarch64_t* from) { return from->esr; }

 private:
  ZxThreadStateDebugRegsAArch64() : Class("zx_thread_state_debug_regs_aarch64_t") {
    AddField(std::make_unique<ArrayClassField<zx_thread_state_debug_regs_aarch64_t,
                                              zx_thread_state_debug_regs_aarch64_bp_t>>(
        "hw_bps", hw_bps, ZxThreadStateDebugRegsAArch64Bp::GetClass()));
    AddField(std::make_unique<ClassField<zx_thread_state_debug_regs_aarch64_t, uint8_t>>(
        "hw_bps_count", SyscallType::kUint8Hexa, hw_bps_count));
    AddField(std::make_unique<ArrayClassField<zx_thread_state_debug_regs_aarch64_t,
                                              zx_thread_state_debug_regs_aarch64_wp_t>>(
        "hw_wps", hw_wps, ZxThreadStateDebugRegsAArch64Wp::GetClass()));
    AddField(std::make_unique<ClassField<zx_thread_state_debug_regs_aarch64_t, uint8_t>>(
        "hw_wps_count", SyscallType::kUint8Hexa, hw_wps_count));
    AddField(std::make_unique<ClassField<zx_thread_state_debug_regs_aarch64_t, uint32_t>>(
        "esr", SyscallType::kUint32Hexa, esr));
  }
  ZxThreadStateDebugRegsAArch64(const ZxThreadStateDebugRegsAArch64&) = delete;
  ZxThreadStateDebugRegsAArch64& operator=(const ZxThreadStateDebugRegsAArch64&) = delete;
  static ZxThreadStateDebugRegsAArch64* instance_;
};

ZxThreadStateDebugRegsAArch64* ZxThreadStateDebugRegsAArch64::instance_ = nullptr;

const ZxThreadStateDebugRegsAArch64* ZxThreadStateDebugRegsAArch64::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxThreadStateDebugRegsAArch64;
  }
  return instance_;
}

class ZxThreadStateDebugRegsX86 : public Class<zx_thread_state_debug_regs_x86_t> {
 public:
  static const ZxThreadStateDebugRegsX86* GetClass();

  static std::pair<const uint64_t*, int> dr(const zx_thread_state_debug_regs_x86_t* from) {
    return std::make_pair(reinterpret_cast<const uint64_t*>(from->dr),
                          sizeof(from->dr) / sizeof(uint64_t));
  }
  static uint64_t dr6(const zx_thread_state_debug_regs_x86_t* from) { return from->dr6; }
  static uint64_t dr7(const zx_thread_state_debug_regs_x86_t* from) { return from->dr7; }

 private:
  ZxThreadStateDebugRegsX86() : Class("zx_thread_state_debug_regs_x86_t") {
    AddField(std::make_unique<DynamicArrayField<zx_thread_state_debug_regs_x86_t, uint64_t, int>>(
        "dr", SyscallType::kUint64Hexa, dr));
    AddField(std::make_unique<ClassField<zx_thread_state_debug_regs_x86_t, uint64_t>>(
        "dr6", SyscallType::kUint64Hexa, dr6));
    AddField(std::make_unique<ClassField<zx_thread_state_debug_regs_x86_t, uint64_t>>(
        "dr7", SyscallType::kUint64Hexa, dr7));
  }
  ZxThreadStateDebugRegsX86(const ZxThreadStateDebugRegsX86&) = delete;
  ZxThreadStateDebugRegsX86& operator=(const ZxThreadStateDebugRegsX86&) = delete;
  static ZxThreadStateDebugRegsX86* instance_;
};

ZxThreadStateDebugRegsX86* ZxThreadStateDebugRegsX86::instance_ = nullptr;

const ZxThreadStateDebugRegsX86* ZxThreadStateDebugRegsX86::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxThreadStateDebugRegsX86;
  }
  return instance_;
}

class ZxThreadStateGeneralRegsAArch64 : public Class<zx_thread_state_general_regs_aarch64_t> {
 public:
  static const ZxThreadStateGeneralRegsAArch64* GetClass();

  static std::pair<const uint64_t*, int> r(const zx_thread_state_general_regs_aarch64_t* from) {
    return std::make_pair(reinterpret_cast<const uint64_t*>(from->r),
                          sizeof(from->r) / sizeof(uint64_t));
  }
  static uint64_t lr(const zx_thread_state_general_regs_aarch64_t* from) { return from->lr; }
  static uint64_t sp(const zx_thread_state_general_regs_aarch64_t* from) { return from->sp; }
  static uint64_t pc(const zx_thread_state_general_regs_aarch64_t* from) { return from->pc; }
  static uint64_t cpsr(const zx_thread_state_general_regs_aarch64_t* from) { return from->cpsr; }
  static uint64_t tpidr(const zx_thread_state_general_regs_aarch64_t* from) { return from->tpidr; }

 private:
  ZxThreadStateGeneralRegsAArch64() : Class("zx_thread_state_general_regs_aarch64_t") {
    AddField(
        std::make_unique<DynamicArrayField<zx_thread_state_general_regs_aarch64_t, uint64_t, int>>(
            "r", SyscallType::kUint64Hexa, r));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_aarch64_t, uint64_t>>(
        "lr", SyscallType::kUint64Hexa, lr));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_aarch64_t, uint64_t>>(
        "sp", SyscallType::kUint64Hexa, sp));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_aarch64_t, uint64_t>>(
        "pc", SyscallType::kUint64Hexa, pc));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_aarch64_t, uint64_t>>(
        "cpsr", SyscallType::kUint64Hexa, cpsr));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_aarch64_t, uint64_t>>(
        "tpidr", SyscallType::kUint64Hexa, tpidr));
  }
  ZxThreadStateGeneralRegsAArch64(const ZxThreadStateGeneralRegsAArch64&) = delete;
  ZxThreadStateGeneralRegsAArch64& operator=(const ZxThreadStateGeneralRegsAArch64&) = delete;
  static ZxThreadStateGeneralRegsAArch64* instance_;
};

ZxThreadStateGeneralRegsAArch64* ZxThreadStateGeneralRegsAArch64::instance_ = nullptr;

const ZxThreadStateGeneralRegsAArch64* ZxThreadStateGeneralRegsAArch64::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxThreadStateGeneralRegsAArch64;
  }
  return instance_;
}

class ZxThreadStateGeneralRegsX86 : public Class<zx_thread_state_general_regs_x86_t> {
 public:
  static const ZxThreadStateGeneralRegsX86* GetClass();

  static uint64_t rax(const zx_thread_state_general_regs_x86_t* from) { return from->rax; }
  static uint64_t rbx(const zx_thread_state_general_regs_x86_t* from) { return from->rbx; }
  static uint64_t rcx(const zx_thread_state_general_regs_x86_t* from) { return from->rcx; }
  static uint64_t rdx(const zx_thread_state_general_regs_x86_t* from) { return from->rdx; }
  static uint64_t rsi(const zx_thread_state_general_regs_x86_t* from) { return from->rsi; }
  static uint64_t rdi(const zx_thread_state_general_regs_x86_t* from) { return from->rdi; }
  static uint64_t rbp(const zx_thread_state_general_regs_x86_t* from) { return from->rbp; }
  static uint64_t rsp(const zx_thread_state_general_regs_x86_t* from) { return from->rsp; }
  static uint64_t r8(const zx_thread_state_general_regs_x86_t* from) { return from->r8; }
  static uint64_t r9(const zx_thread_state_general_regs_x86_t* from) { return from->r9; }
  static uint64_t r10(const zx_thread_state_general_regs_x86_t* from) { return from->r10; }
  static uint64_t r11(const zx_thread_state_general_regs_x86_t* from) { return from->r11; }
  static uint64_t r12(const zx_thread_state_general_regs_x86_t* from) { return from->r12; }
  static uint64_t r13(const zx_thread_state_general_regs_x86_t* from) { return from->r13; }
  static uint64_t r14(const zx_thread_state_general_regs_x86_t* from) { return from->r14; }
  static uint64_t r15(const zx_thread_state_general_regs_x86_t* from) { return from->r15; }
  static uint64_t rip(const zx_thread_state_general_regs_x86_t* from) { return from->rip; }
  static uint64_t rflags(const zx_thread_state_general_regs_x86_t* from) { return from->rflags; }
  static uint64_t fs_base(const zx_thread_state_general_regs_x86_t* from) { return from->fs_base; }
  static uint64_t gs_base(const zx_thread_state_general_regs_x86_t* from) { return from->gs_base; }

 private:
  ZxThreadStateGeneralRegsX86() : Class("zx_thread_state_general_regs_x86_t") {
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "rax", SyscallType::kUint64Hexa, rax));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "rbx", SyscallType::kUint64Hexa, rbx));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "rcx", SyscallType::kUint64Hexa, rcx));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "rdx", SyscallType::kUint64Hexa, rdx));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "rsi", SyscallType::kUint64Hexa, rsi));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "rdi", SyscallType::kUint64Hexa, rdi));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "rbp", SyscallType::kUint64Hexa, rbp));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "rsp", SyscallType::kUint64Hexa, rsp));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "r8", SyscallType::kUint64Hexa, r8));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "r9", SyscallType::kUint64Hexa, r9));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "r10", SyscallType::kUint64Hexa, r10));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "r11", SyscallType::kUint64Hexa, r11));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "r12", SyscallType::kUint64Hexa, r12));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "r13", SyscallType::kUint64Hexa, r13));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "r14", SyscallType::kUint64Hexa, r14));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "r15", SyscallType::kUint64Hexa, r15));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "rip", SyscallType::kUint64Hexa, rip));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "rflags", SyscallType::kUint64Hexa, rflags));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "fs_base", SyscallType::kUint64Hexa, fs_base));
    AddField(std::make_unique<ClassField<zx_thread_state_general_regs_x86_t, uint64_t>>(
        "gs_base", SyscallType::kUint64Hexa, gs_base));
  }
  ZxThreadStateGeneralRegsX86(const ZxThreadStateGeneralRegsX86&) = delete;
  ZxThreadStateGeneralRegsX86& operator=(const ZxThreadStateGeneralRegsX86&) = delete;
  static ZxThreadStateGeneralRegsX86* instance_;
};

ZxThreadStateGeneralRegsX86* ZxThreadStateGeneralRegsX86::instance_ = nullptr;

const ZxThreadStateGeneralRegsX86* ZxThreadStateGeneralRegsX86::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxThreadStateGeneralRegsX86;
  }
  return instance_;
}

class ZxThreadStateFpRegsX86 : public Class<zx_thread_state_fp_regs_x86_t> {
 public:
  static const ZxThreadStateFpRegsX86* GetClass();

  static uint16_t fcw(const zx_thread_state_fp_regs_x86_t* from) { return from->fcw; }
  static uint16_t fsw(const zx_thread_state_fp_regs_x86_t* from) { return from->fsw; }
  static uint8_t ftw(const zx_thread_state_fp_regs_x86_t* from) { return from->ftw; }
  static uint16_t fop(const zx_thread_state_fp_regs_x86_t* from) { return from->fop; }
  static uint64_t fip(const zx_thread_state_fp_regs_x86_t* from) { return from->fip; }
  static uint64_t fdp(const zx_thread_state_fp_regs_x86_t* from) { return from->fdp; }
  static std::pair<const zx_uint128_t*, int> st(const zx_thread_state_fp_regs_x86_t* from) {
    return std::make_pair(reinterpret_cast<const zx_uint128_t*>(from->st),
                          sizeof(from->st) / sizeof(from->st[0]));
  }

 private:
  ZxThreadStateFpRegsX86() : Class("zx_thread_state_fp_regs_x86_t") {
    AddField(std::make_unique<ClassField<zx_thread_state_fp_regs_x86_t, uint16_t>>(
        "fcw", SyscallType::kUint16Hexa, fcw));
    AddField(std::make_unique<ClassField<zx_thread_state_fp_regs_x86_t, uint16_t>>(
        "fsw", SyscallType::kUint16Hexa, fsw));
    AddField(std::make_unique<ClassField<zx_thread_state_fp_regs_x86_t, uint8_t>>(
        "ftw", SyscallType::kUint8Hexa, ftw));
    AddField(std::make_unique<ClassField<zx_thread_state_fp_regs_x86_t, uint16_t>>(
        "fop", SyscallType::kUint16Hexa, fop));
    AddField(std::make_unique<ClassField<zx_thread_state_fp_regs_x86_t, uint64_t>>(
        "fip", SyscallType::kUint64Hexa, fip));
    AddField(std::make_unique<ClassField<zx_thread_state_fp_regs_x86_t, uint64_t>>(
        "fdp", SyscallType::kUint64Hexa, fdp));
    AddField(std::make_unique<DynamicArrayField<zx_thread_state_fp_regs_x86_t, zx_uint128_t, int>>(
        "st", SyscallType::kUint128Hexa, st));
  }
  ZxThreadStateFpRegsX86(const ZxThreadStateFpRegsX86&) = delete;
  ZxThreadStateFpRegsX86& operator=(const ZxThreadStateFpRegsX86&) = delete;
  static ZxThreadStateFpRegsX86* instance_;
};

ZxThreadStateFpRegsX86* ZxThreadStateFpRegsX86::instance_ = nullptr;

const ZxThreadStateFpRegsX86* ZxThreadStateFpRegsX86::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxThreadStateFpRegsX86;
  }
  return instance_;
}

class ZxThreadStateVectorRegsAArch64 : public Class<zx_thread_state_vector_regs_aarch64_t> {
 public:
  static const ZxThreadStateVectorRegsAArch64* GetClass();

  static uint32_t fpcr(const zx_thread_state_vector_regs_aarch64_t* from) { return from->fpcr; }
  static uint32_t fpsr(const zx_thread_state_vector_regs_aarch64_t* from) { return from->fpsr; }
  static std::pair<const zx_uint128_t*, int> v(const zx_thread_state_vector_regs_aarch64_t* from) {
    return std::make_pair(reinterpret_cast<const zx_uint128_t*>(from->v),
                          sizeof(from->v) / sizeof(from->v[0]));
  }

 private:
  ZxThreadStateVectorRegsAArch64() : Class("zx_thread_state_vector_regs_aarch64_t") {
    AddField(std::make_unique<ClassField<zx_thread_state_vector_regs_aarch64_t, uint32_t>>(
        "fpcr", SyscallType::kUint32Hexa, fpcr));
    AddField(std::make_unique<ClassField<zx_thread_state_vector_regs_aarch64_t, uint32_t>>(
        "fpsr", SyscallType::kUint32Hexa, fpsr));
    AddField(std::make_unique<
             DynamicArrayField<zx_thread_state_vector_regs_aarch64_t, zx_uint128_t, int>>(
        "v", SyscallType::kUint128Hexa, v));
  }
  ZxThreadStateVectorRegsAArch64(const ZxThreadStateVectorRegsAArch64&) = delete;
  ZxThreadStateVectorRegsAArch64& operator=(const ZxThreadStateVectorRegsAArch64&) = delete;
  static ZxThreadStateVectorRegsAArch64* instance_;
};

ZxThreadStateVectorRegsAArch64* ZxThreadStateVectorRegsAArch64::instance_ = nullptr;

const ZxThreadStateVectorRegsAArch64* ZxThreadStateVectorRegsAArch64::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxThreadStateVectorRegsAArch64;
  }
  return instance_;
}

class ZxThreadStateVectorRegsX86Zmm : public Class<zx_thread_state_vector_regs_x86_zmm_t> {
 public:
  static const ZxThreadStateVectorRegsX86Zmm* GetClass();

  static std::pair<const uint64_t*, int> v(const zx_thread_state_vector_regs_x86_zmm_t* from) {
    return std::make_pair(reinterpret_cast<const uint64_t*>(from->v),
                          sizeof(from->v) / sizeof(from->v[0]));
  }

 private:
  ZxThreadStateVectorRegsX86Zmm() : Class("zx_thread_state_vector_regs_x86_zmm_t") {
    AddField(
        std::make_unique<DynamicArrayField<zx_thread_state_vector_regs_x86_zmm_t, uint64_t, int>>(
            "v", SyscallType::kUint64Hexa, v));
  }
  ZxThreadStateVectorRegsX86Zmm(const ZxThreadStateVectorRegsX86Zmm&) = delete;
  ZxThreadStateVectorRegsX86Zmm& operator=(const ZxThreadStateVectorRegsX86Zmm&) = delete;
  static ZxThreadStateVectorRegsX86Zmm* instance_;
};

ZxThreadStateVectorRegsX86Zmm* ZxThreadStateVectorRegsX86Zmm::instance_ = nullptr;

const ZxThreadStateVectorRegsX86Zmm* ZxThreadStateVectorRegsX86Zmm::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxThreadStateVectorRegsX86Zmm;
  }
  return instance_;
}

class ZxThreadStateVectorRegsX86 : public Class<zx_thread_state_vector_regs_x86_t> {
 public:
  static const ZxThreadStateVectorRegsX86* GetClass();

  static std::pair<const zx_thread_state_vector_regs_x86_zmm_t*, int> zmm(
      const zx_thread_state_vector_regs_x86_t* from) {
    return std::make_pair(reinterpret_cast<const zx_thread_state_vector_regs_x86_zmm_t*>(from->zmm),
                          sizeof(from->zmm) / sizeof(from->zmm[0]));
  }
  static std::pair<const uint64_t*, int> opmask(const zx_thread_state_vector_regs_x86_t* from) {
    return std::make_pair(reinterpret_cast<const uint64_t*>(from->opmask),
                          sizeof(from->opmask) / sizeof(from->opmask[0]));
  }
  static uint32_t mxcsr(const zx_thread_state_vector_regs_x86_t* from) { return from->mxcsr; }

 private:
  ZxThreadStateVectorRegsX86() : Class("zx_thread_state_vector_regs_x86_t") {
    AddField(std::make_unique<ArrayClassField<zx_thread_state_vector_regs_x86_t,
                                              zx_thread_state_vector_regs_x86_zmm_t>>(
        "zmm", zmm, ZxThreadStateVectorRegsX86Zmm::GetClass()));
    AddField(std::make_unique<DynamicArrayField<zx_thread_state_vector_regs_x86_t, uint64_t, int>>(
        "opmask", SyscallType::kUint64Hexa, opmask));
    AddField(std::make_unique<ClassField<zx_thread_state_vector_regs_x86_t, uint32_t>>(
        "mxcsr", SyscallType::kUint32Hexa, mxcsr));
  }
  ZxThreadStateVectorRegsX86(const ZxThreadStateVectorRegsX86&) = delete;
  ZxThreadStateVectorRegsX86& operator=(const ZxThreadStateVectorRegsX86&) = delete;
  static ZxThreadStateVectorRegsX86* instance_;
};

ZxThreadStateVectorRegsX86* ZxThreadStateVectorRegsX86::instance_ = nullptr;

const ZxThreadStateVectorRegsX86* ZxThreadStateVectorRegsX86::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxThreadStateVectorRegsX86;
  }
  return instance_;
}

class ZxVcpuIo : public Class<zx_vcpu_io_t> {
 public:
  static const ZxVcpuIo* GetClass();

  static uint8_t access_size(const zx_vcpu_io_t* from) { return from->access_size; }
  static uint8_t u8(const zx_vcpu_io_t* from) { return from->u8; }
  static uint16_t u16(const zx_vcpu_io_t* from) { return from->u16; }
  static uint32_t u32(const zx_vcpu_io_t* from) { return from->u32; }
  static std::pair<const uint8_t*, int> data(const zx_vcpu_io_t* from) {
    return std::make_pair(reinterpret_cast<const uint8_t*>(from->data),
                          sizeof(from->data) / sizeof(uint8_t));
  }

 private:
  ZxVcpuIo() : Class("zx_vcpu_io_t") {
    AddField(std::make_unique<ClassField<zx_vcpu_io_t, uint8_t>>("access_size", SyscallType::kUint8,
                                                                 access_size));
    AddField(
        std::make_unique<ClassField<zx_vcpu_io_t, uint8_t>>("u8", SyscallType::kUint8Hexa, u8));
    AddField(
        std::make_unique<ClassField<zx_vcpu_io_t, uint16_t>>("u16", SyscallType::kUint16Hexa, u16));
    AddField(
        std::make_unique<ClassField<zx_vcpu_io_t, uint32_t>>("u32", SyscallType::kUint32Hexa, u32));
    AddField(std::make_unique<DynamicArrayField<zx_vcpu_io_t, uint8_t, int>>(
        "data", SyscallType::kUint8Hexa, data));
  }
  ZxVcpuIo(const ZxVcpuIo&) = delete;
  ZxVcpuIo& operator=(const ZxVcpuIo&) = delete;
  static ZxVcpuIo* instance_;
};

ZxVcpuIo* ZxVcpuIo::instance_ = nullptr;

const ZxVcpuIo* ZxVcpuIo::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxVcpuIo;
  }
  return instance_;
}

class ZxVcpuStateAArch64 : public Class<zx_vcpu_state_aarch64_t> {
 public:
  static const ZxVcpuStateAArch64* GetClass();

  static std::pair<const uint64_t*, int> x(const zx_vcpu_state_aarch64_t* from) {
    return std::make_pair(reinterpret_cast<const uint64_t*>(from->x),
                          sizeof(from->x) / sizeof(uint64_t));
  }
  static uint64_t sp(const zx_vcpu_state_aarch64_t* from) { return from->sp; }
  static uint32_t cpsr(const zx_vcpu_state_aarch64_t* from) { return from->cpsr; }

 private:
  ZxVcpuStateAArch64() : Class("zx_vcpu_state_aarch64_t") {
    AddField(std::make_unique<DynamicArrayField<zx_vcpu_state_aarch64_t, uint64_t, int>>(
        "x", SyscallType::kUint64Hexa, x));
    AddField(std::make_unique<ClassField<zx_vcpu_state_aarch64_t, uint64_t>>(
        "sp", SyscallType::kUint64Hexa, sp));
    AddField(std::make_unique<ClassField<zx_vcpu_state_aarch64_t, uint32_t>>(
        "cpsr", SyscallType::kUint32Hexa, cpsr));
  }
  ZxVcpuStateAArch64(const ZxVcpuStateAArch64&) = delete;
  ZxVcpuStateAArch64& operator=(const ZxVcpuStateAArch64&) = delete;
  static ZxVcpuStateAArch64* instance_;
};

ZxVcpuStateAArch64* ZxVcpuStateAArch64::instance_ = nullptr;

const ZxVcpuStateAArch64* ZxVcpuStateAArch64::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxVcpuStateAArch64;
  }
  return instance_;
}

class ZxVcpuStateX86 : public Class<zx_vcpu_state_x86_t> {
 public:
  static const ZxVcpuStateX86* GetClass();

  static uint64_t rax(const zx_vcpu_state_x86_t* from) { return from->rax; }
  static uint64_t rcx(const zx_vcpu_state_x86_t* from) { return from->rcx; }
  static uint64_t rdx(const zx_vcpu_state_x86_t* from) { return from->rdx; }
  static uint64_t rbx(const zx_vcpu_state_x86_t* from) { return from->rbx; }
  static uint64_t rsp(const zx_vcpu_state_x86_t* from) { return from->rsp; }
  static uint64_t rbp(const zx_vcpu_state_x86_t* from) { return from->rbp; }
  static uint64_t rsi(const zx_vcpu_state_x86_t* from) { return from->rsi; }
  static uint64_t rdi(const zx_vcpu_state_x86_t* from) { return from->rdi; }
  static uint64_t r8(const zx_vcpu_state_x86_t* from) { return from->r8; }
  static uint64_t r9(const zx_vcpu_state_x86_t* from) { return from->r9; }
  static uint64_t r10(const zx_vcpu_state_x86_t* from) { return from->r10; }
  static uint64_t r11(const zx_vcpu_state_x86_t* from) { return from->r11; }
  static uint64_t r12(const zx_vcpu_state_x86_t* from) { return from->r12; }
  static uint64_t r13(const zx_vcpu_state_x86_t* from) { return from->r13; }
  static uint64_t r14(const zx_vcpu_state_x86_t* from) { return from->r14; }
  static uint64_t r15(const zx_vcpu_state_x86_t* from) { return from->r15; }
  static uint64_t rflags(const zx_vcpu_state_x86_t* from) { return from->rflags; }

 private:
  ZxVcpuStateX86() : Class("zx_vcpu_state_x86_t") {
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "rax", SyscallType::kUint64Hexa, rax));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "rcx", SyscallType::kUint64Hexa, rcx));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "rdx", SyscallType::kUint64Hexa, rdx));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "rbx", SyscallType::kUint64Hexa, rbx));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "rsp", SyscallType::kUint64Hexa, rsp));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "rbp", SyscallType::kUint64Hexa, rbp));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "rsi", SyscallType::kUint64Hexa, rsi));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "rdi", SyscallType::kUint64Hexa, rdi));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "r8", SyscallType::kUint64Hexa, r8));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "r9", SyscallType::kUint64Hexa, r9));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "r10", SyscallType::kUint64Hexa, r10));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "r11", SyscallType::kUint64Hexa, r11));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "r12", SyscallType::kUint64Hexa, r12));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "r13", SyscallType::kUint64Hexa, r13));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "r14", SyscallType::kUint64Hexa, r14));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "r15", SyscallType::kUint64Hexa, r15));
    AddField(std::make_unique<ClassField<zx_vcpu_state_x86_t, uint64_t>>(
        "rflags", SyscallType::kUint64Hexa, rflags));
  }
  ZxVcpuStateX86(const ZxVcpuStateX86&) = delete;
  ZxVcpuStateX86& operator=(const ZxVcpuStateX86&) = delete;
  static ZxVcpuStateX86* instance_;
};

ZxVcpuStateX86* ZxVcpuStateX86::instance_ = nullptr;

const ZxVcpuStateX86* ZxVcpuStateX86::GetClass() {
  if (instance_ == nullptr) {
    instance_ = new ZxVcpuStateX86;
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
    Syscall* extract_handles =
        AddFunction("processargs_extract_handles", SyscallReturnType::kNoReturn);
    // Arguments
    auto nhandles = extract_handles->Argument<uint32_t>(SyscallType::kUint32);
    auto handles = extract_handles->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto handle_info = extract_handles->PointerArgument<uint32_t>(SyscallType::kUint32Hexa);
    // Inputs
    extract_handles->InputBuffer<zx_handle_t, zx_handle_t, uint32_t>(
        "handles", SyscallType::kHandle, std::make_unique<ArgumentAccess<zx_handle_t>>(handles),
        std::make_unique<ArgumentAccess<uint32_t>>(nhandles));
    extract_handles->InputBuffer<uint32_t, uint32_t, uint32_t>(
        "handles", SyscallType::kUint32Hexa,
        std::make_unique<ArgumentAccess<uint32_t>>(handle_info),
        std::make_unique<ArgumentAccess<uint32_t>>(nhandles));
    extract_handles->set_inputs_decoded_action(&SyscallDecoderDispatcher::ExtractHandleInfos);
  }

  {
    Syscall* libc_extensions_init =
        AddFunction("__libc_extensions_init", SyscallReturnType::kNoReturn);
    // Arguments
    auto handle_count = libc_extensions_init->Argument<uint32_t>(SyscallType::kUint32);
    auto handle = libc_extensions_init->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto handle_info = libc_extensions_init->PointerArgument<uint32_t>(SyscallType::kUint32Hexa);
    auto name_count = libc_extensions_init->Argument<uint32_t>(SyscallType::kUint32);
    auto names = libc_extensions_init->PointerArgument<char*>(SyscallType::kChar);
    // Inputs
    libc_extensions_init->Input<uint32_t>("handle_count",
                                          std::make_unique<ArgumentAccess<uint32_t>>(handle_count));
    libc_extensions_init->InputBuffer<zx_handle_t, zx_handle_t, uint32_t>(
        "handle", SyscallType::kHandle, std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
        std::make_unique<ArgumentAccess<uint32_t>>(handle_count));
    libc_extensions_init->InputBuffer<uint32_t, uint32_t, uint32_t>(
        "handle_info", SyscallType::kUint32Hexa,
        std::make_unique<ArgumentAccess<uint32_t>>(handle_info),
        std::make_unique<ArgumentAccess<uint32_t>>(handle_count));
    libc_extensions_init->Input<uint32_t>("name_count",
                                          std::make_unique<ArgumentAccess<uint32_t>>(name_count));
    libc_extensions_init->InputStringBuffer("names", std::make_unique<ArgumentAccess<char*>>(names),
                                            std::make_unique<ArgumentAccess<uint32_t>>(name_count),
                                            80);
    libc_extensions_init->set_inputs_decoded_action(&SyscallDecoderDispatcher::LibcExtensionsInit);
  }

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

  { Add("zx_system_get_dcache_line_size", SyscallReturnType::kUint32); }

  { Add("zx_system_get_num_cpus", SyscallReturnType::kUint32); }

  {
    Syscall* zx_system_get_version = Add("zx_system_get_version", SyscallReturnType::kStatus);
    // Arguments
    auto version = zx_system_get_version->PointerArgument<char>(SyscallType::kChar);
    auto version_size = zx_system_get_version->Argument<size_t>(SyscallType::kSize);
    // Outputs
    zx_system_get_version->OutputString<char>(
        ZX_OK, "version", std::make_unique<ArgumentAccess<char>>(version),
        std::make_unique<ArgumentAccess<size_t>>(version_size));
  }

  { Add("zx_system_get_physmem", SyscallReturnType::kUint64); }

  {
    Syscall* zx_system_get_event = Add("zx_system_get_event", SyscallReturnType::kStatus);
    // Arguments
    auto root_job = zx_system_get_event->Argument<zx_handle_t>(SyscallType::kHandle);
    auto kind = zx_system_get_event->Argument<uint32_t>(SyscallType::kSystemEventType);
    auto event = zx_system_get_event->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_system_get_event->Input<uint32_t>("root_job",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(root_job));
    zx_system_get_event->Input<uint32_t>("kind", std::make_unique<ArgumentAccess<uint32_t>>(kind));
    // Outputs
    zx_system_get_event->Output<zx_handle_t>(ZX_OK, "event",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(event));
  }

  {
    Syscall* zx_system_get_features = Add("zx_system_get_features", SyscallReturnType::kStatus);
    // Arguments
    auto kind = zx_system_get_features->Argument<uint32_t>(SyscallType::kFeatureKind);
    auto features = zx_system_get_features->PointerArgument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_system_get_features->Input<uint32_t>("kind",
                                            std::make_unique<ArgumentAccess<uint32_t>>(kind));
    // Outputs
    zx_system_get_features->Output<uint32_t>(ZX_OK, "features",
                                             std::make_unique<ArgumentAccess<uint32_t>>(features));
  }

  {
    Syscall* zx_system_mexec = Add("zx_system_mexec", SyscallReturnType::kStatus);
    // Arguments
    auto resource = zx_system_mexec->Argument<zx_handle_t>(SyscallType::kHandle);
    auto kernel_vmo = zx_system_mexec->Argument<zx_handle_t>(SyscallType::kHandle);
    auto bootimage_vmo = zx_system_mexec->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_system_mexec->Input<uint32_t>("resource",
                                     std::make_unique<ArgumentAccess<zx_handle_t>>(resource));
    zx_system_mexec->Input<uint32_t>("kernel_vmo",
                                     std::make_unique<ArgumentAccess<zx_handle_t>>(kernel_vmo));
    zx_system_mexec->Input<uint32_t>("bootimage_vmo",
                                     std::make_unique<ArgumentAccess<zx_handle_t>>(bootimage_vmo));
  }

  {
    Syscall* zx_system_mexec_payload_get =
        Add("zx_system_mexec_payload_get", SyscallReturnType::kStatus);
    // Arguments
    auto resource = zx_system_mexec_payload_get->Argument<zx_handle_t>(SyscallType::kHandle);
    auto buffer = zx_system_mexec_payload_get->PointerArgument<uint8_t>(SyscallType::kUint8Hexa);
    auto buffer_size = zx_system_mexec_payload_get->Argument<size_t>(SyscallType::kSize);
    // Inputs
    zx_system_mexec_payload_get->Input<uint32_t>(
        "resource", std::make_unique<ArgumentAccess<zx_handle_t>>(resource));
    zx_system_mexec_payload_get->Input<size_t>(
        "buffer_size", std::make_unique<ArgumentAccess<size_t>>(buffer_size));
    // Outputs
    zx_system_mexec_payload_get->OutputBuffer<uint8_t, uint8_t, size_t>(
        ZX_OK, "buffer", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
  }

  {
    Syscall* zx_system_powerctl = Add("zx_system_powerctl", SyscallReturnType::kStatus);
    // Arguments
    auto resource = zx_system_powerctl->Argument<zx_handle_t>(SyscallType::kHandle);
    auto cmd = zx_system_powerctl->Argument<uint32_t>(SyscallType::kSystemPowerctl);
    auto arg = zx_system_powerctl->PointerArgument<uint8_t>(SyscallType::kUint8);
    // Conditional field Ids
    const uint8_t kZxSystemPowerctl_ZX_SYSTEM_POWERCTL_ACPI_TRANSITION_S_STATE = 1;
    const uint8_t kZxSystemPowerctl_ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1 = 2;
    // Inputs
    zx_system_powerctl->Input<uint32_t>("resource",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(resource));
    zx_system_powerctl->Input<uint32_t>("cmd", std::make_unique<ArgumentAccess<uint32_t>>(cmd));
    zx_system_powerctl
        ->InputObject<zx_system_powerctl_arg_t>("arg",
                                                std::make_unique<ArgumentAccess<uint8_t>>(arg),
                                                ZxSystemPowerctlArgAcpi::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(cmd),
                                   ZX_SYSTEM_POWERCTL_ACPI_TRANSITION_S_STATE)
        ->SetId(kZxSystemPowerctl_ZX_SYSTEM_POWERCTL_ACPI_TRANSITION_S_STATE);
    zx_system_powerctl
        ->InputObject<zx_system_powerctl_arg_t>("arg",
                                                std::make_unique<ArgumentAccess<uint8_t>>(arg),
                                                ZxSystemPowerctlArgX86PowerLimit::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(cmd),
                                   ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1)
        ->SetId(kZxSystemPowerctl_ZX_SYSTEM_POWERCTL_X86_SET_PKG_PL1);
  }

  {
    Syscall* zx_cache_flush = Add("zx_cache_flush", SyscallReturnType::kStatus);
    // Arguments
    auto addr = zx_cache_flush->Argument<zx_vaddr_t>(SyscallType::kVaddr);
    auto size = zx_cache_flush->Argument<size_t>(SyscallType::kSize);
    auto options = zx_cache_flush->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_cache_flush->Input<zx_vaddr_t>("addr", std::make_unique<ArgumentAccess<zx_vaddr_t>>(addr));
    zx_cache_flush->Input<size_t>("size", std::make_unique<ArgumentAccess<size_t>>(size));
    zx_cache_flush->Input<uint32_t>("options", std::make_unique<ArgumentAccess<uint32_t>>(options));
  }

  {
    Syscall* zx_handle_close = Add("zx_handle_close", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_handle_close->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_handle_close->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));

    zx_handle_close->set_compute_statistics([](const OutputEvent* event) {
      auto handle_info = event->invoked_event()->GetHandleInfo(
          event->syscall()->SearchInlineMember("handle", /*invoked=*/true));
      if (handle_info != nullptr) {
        handle_info->AddCloseEvent(event);
      }
    });
  }

  {
    Syscall* zx_handle_close_many = Add("zx_handle_close_many", SyscallReturnType::kStatus);
    // Arguments
    auto handles = zx_handle_close_many->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto num_handles = zx_handle_close_many->Argument<size_t>(SyscallType::kSize);
    // Inputs
    zx_handle_close_many->InputBuffer<zx_handle_t, zx_handle_t, size_t>(
        "handles", SyscallType::kHandle, std::make_unique<ArgumentAccess<zx_handle_t>>(handles),
        std::make_unique<ArgumentAccess<size_t>>(num_handles));

    zx_handle_close_many->set_compute_statistics([](const OutputEvent* event) {
      const fidl_codec::Value* values = event->invoked_event()->GetValue(
          event->syscall()->SearchOutlineMember("handles", /*invoked=*/true));
      FX_DCHECK(values != nullptr);
      auto handles = values->AsVectorValue();
      FX_DCHECK(handles != nullptr);
      for (const auto& value : handles->values()) {
        auto handle_value = value->AsHandleValue();
        FX_DCHECK(handle_value != nullptr);
        if (handle_value->handle().handle != ZX_HANDLE_INVALID) {
          HandleInfo* handle_info =
              event->thread()->process()->SearchHandleInfo(handle_value->handle().handle);
          handle_info->AddCloseEvent(event);
        }
      }
    });
  }

  {
    Syscall* zx_handle_duplicate = Add("zx_handle_duplicate", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_handle_duplicate->Argument<zx_handle_t>(SyscallType::kHandle);
    auto rights = zx_handle_duplicate->Argument<zx_rights_t>(SyscallType::kRights);
    auto out = zx_handle_duplicate->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_handle_duplicate->Input<zx_handle_t>("handle",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_handle_duplicate->Input<zx_rights_t>("rights",
                                            std::make_unique<ArgumentAccess<zx_rights_t>>(rights));
    // Outputs
    zx_handle_duplicate->Output<zx_handle_t>(ZX_OK, "out",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(out));
  }

  {
    Syscall* zx_handle_replace = Add("zx_handle_replace", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_handle_replace->Argument<zx_handle_t>(SyscallType::kHandle);
    auto rights = zx_handle_replace->Argument<zx_rights_t>(SyscallType::kRights);
    auto out = zx_handle_replace->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_handle_replace->Input<zx_handle_t>("handle",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_handle_replace->Input<zx_rights_t>("rights",
                                          std::make_unique<ArgumentAccess<zx_rights_t>>(rights));
    // Outputs
    zx_handle_replace->Output<zx_handle_t>(ZX_OK, "out",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(out));
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
    zx_object_wait_many->InputObjectArray<zx_wait_item_t, size_t>(
        "items", std::make_unique<ArgumentAccess<zx_wait_item_t>>(items),
        std::make_unique<ArgumentAccess<size_t>>(count), ZxWaitItem::GetClass());
    zx_object_wait_many->Input<zx_time_t>("deadline",
                                          std::make_unique<ArgumentAccess<zx_time_t>>(deadline));
    // Outputs
    zx_object_wait_many->OutputObjectArray<zx_wait_item_t, size_t>(
        ZX_OK, "items", std::make_unique<ArgumentAccess<zx_wait_item_t>>(items),
        std::make_unique<ArgumentAccess<size_t>>(count), ZxWaitItem::GetClass());
    zx_object_wait_many->OutputObjectArray<zx_wait_item_t, size_t>(
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
    Syscall* zx_object_get_property = Add("zx_object_get_property", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_object_get_property->Argument<zx_handle_t>(SyscallType::kHandle);
    auto property = zx_object_get_property->Argument<uint32_t>(SyscallType::kPropType);
    auto value = zx_object_get_property->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto value_size = zx_object_get_property->Argument<size_t>(SyscallType::kSize);
    // Conditional field Ids
    const uint8_t kZxObjectGetProperty_ZX_PROP_NAME = 1;
    const uint8_t kZxObjectGetProperty_ZX_PROP_PROCESS_DEBUG_ADDR = 2;
    const uint8_t kZxObjectGetProperty_ZX_PROP_PROCESS_VDSO_BASE_ADDRESS = 3;
    const uint8_t kZxObjectGetProperty_ZX_PROP_SOCKET_RX_THRESHOLD = 4;
    const uint8_t kZxObjectGetProperty_ZX_PROP_SOCKET_TX_THRESHOLD = 5;
    const uint8_t kZxObjectGetProperty_ZX_PROP_EXCEPTION_STATE = 6;
    // Inputs
    zx_object_get_property->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_object_get_property->Input<uint32_t>("property",
                                            std::make_unique<ArgumentAccess<uint32_t>>(property));
    // Outputs
    zx_object_get_property
        ->OutputString<uint8_t>(ZX_OK, "value", std::make_unique<ArgumentAccess<uint8_t>>(value),
                                std::make_unique<ArgumentAccess<size_t>>(value_size))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_NAME)
        ->SetId(kZxObjectGetProperty_ZX_PROP_NAME);
    zx_object_get_property
        ->OutputIndirect<uintptr_t, uint8_t>(ZX_OK, "value", SyscallType::kVaddr,
                                             std::make_unique<ArgumentAccess<uint8_t>>(value))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_PROCESS_DEBUG_ADDR)
        ->SetId(kZxObjectGetProperty_ZX_PROP_PROCESS_DEBUG_ADDR);
    zx_object_get_property
        ->OutputIndirect<uintptr_t, uint8_t>(ZX_OK, "value", SyscallType::kVaddr,
                                             std::make_unique<ArgumentAccess<uint8_t>>(value))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_PROCESS_VDSO_BASE_ADDRESS)
        ->SetId(kZxObjectGetProperty_ZX_PROP_PROCESS_VDSO_BASE_ADDRESS);
    zx_object_get_property
        ->OutputIndirect<size_t, uint8_t>(ZX_OK, "value", SyscallType::kSize,
                                          std::make_unique<ArgumentAccess<uint8_t>>(value))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_SOCKET_RX_THRESHOLD)
        ->SetId(kZxObjectGetProperty_ZX_PROP_SOCKET_RX_THRESHOLD);
    zx_object_get_property
        ->OutputIndirect<size_t, uint8_t>(ZX_OK, "value", SyscallType::kSize,
                                          std::make_unique<ArgumentAccess<uint8_t>>(value))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_SOCKET_TX_THRESHOLD)
        ->SetId(kZxObjectGetProperty_ZX_PROP_SOCKET_TX_THRESHOLD);
    zx_object_get_property
        ->OutputIndirect<uint32_t, uint8_t>(ZX_OK, "value", SyscallType::kExceptionState,
                                            std::make_unique<ArgumentAccess<uint8_t>>(value))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_EXCEPTION_STATE)
        ->SetId(kZxObjectGetProperty_ZX_PROP_EXCEPTION_STATE);
  }

  {
    Syscall* zx_object_set_property = Add("zx_object_set_property", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_object_set_property->Argument<zx_handle_t>(SyscallType::kHandle);
    auto property = zx_object_set_property->Argument<uint32_t>(SyscallType::kPropType);
    auto value = zx_object_set_property->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto value_size = zx_object_set_property->Argument<size_t>(SyscallType::kSize);
    // Conditional field Ids
    const uint8_t kZxObjectSetProperty_ZX_PROP_NAME = 1;
    const uint8_t kZxObjectSetProperty_ZX_PROP_REGISTER_FS = 2;
    const uint8_t kZxObjectSetProperty_ZX_PROP_REGISTER_GS = 3;
    const uint8_t kZxObjectSetProperty_ZX_PROP_PROCESS_DEBUG_ADDR = 4;
    const uint8_t kZxObjectSetProperty_ZX_PROP_SOCKET_RX_THRESHOLD = 5;
    const uint8_t kZxObjectSetProperty_ZX_PROP_SOCKET_TX_THRESHOLD = 6;
    const uint8_t kZxObjectSetProperty_ZX_PROP_JOB_KILL_ON_OOM = 7;
    const uint8_t kZxObjectSetProperty_ZX_PROP_EXCEPTION_STATE = 8;
    // Inputs
    zx_object_set_property->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_object_set_property->Input<uint32_t>("property",
                                            std::make_unique<ArgumentAccess<uint32_t>>(property));
    zx_object_set_property
        ->InputString<uint8_t>("value", std::make_unique<ArgumentAccess<uint8_t>>(value),
                               std::make_unique<ArgumentAccess<size_t>>(value_size))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_NAME)
        ->SetId(kZxObjectSetProperty_ZX_PROP_NAME);
    zx_object_set_property
        ->InputIndirect<uintptr_t, uint8_t>("value", SyscallType::kVaddr,
                                            std::make_unique<ArgumentAccess<uint8_t>>(value))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_REGISTER_FS)
        ->SetId(kZxObjectSetProperty_ZX_PROP_REGISTER_FS);
    zx_object_set_property
        ->InputIndirect<uintptr_t, uint8_t>("value", SyscallType::kVaddr,
                                            std::make_unique<ArgumentAccess<uint8_t>>(value))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_REGISTER_GS)
        ->SetId(kZxObjectSetProperty_ZX_PROP_REGISTER_GS);
    zx_object_set_property
        ->InputIndirect<uintptr_t, uint8_t>("value", SyscallType::kVaddr,
                                            std::make_unique<ArgumentAccess<uint8_t>>(value))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_PROCESS_DEBUG_ADDR)
        ->SetId(kZxObjectSetProperty_ZX_PROP_PROCESS_DEBUG_ADDR);
    zx_object_set_property
        ->InputIndirect<size_t, uint8_t>("value", SyscallType::kSize,
                                         std::make_unique<ArgumentAccess<uint8_t>>(value))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_SOCKET_RX_THRESHOLD)
        ->SetId(kZxObjectSetProperty_ZX_PROP_SOCKET_RX_THRESHOLD);
    zx_object_set_property
        ->InputIndirect<size_t, uint8_t>("value", SyscallType::kSize,
                                         std::make_unique<ArgumentAccess<uint8_t>>(value))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_SOCKET_TX_THRESHOLD)
        ->SetId(kZxObjectSetProperty_ZX_PROP_SOCKET_TX_THRESHOLD);
    zx_object_set_property
        ->InputIndirect<size_t, uint8_t>("value", SyscallType::kSize,
                                         std::make_unique<ArgumentAccess<uint8_t>>(value))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_JOB_KILL_ON_OOM)
        ->SetId(kZxObjectSetProperty_ZX_PROP_JOB_KILL_ON_OOM);
    zx_object_set_property
        ->InputIndirect<uint32_t, uint8_t>("value", SyscallType::kExceptionState,
                                           std::make_unique<ArgumentAccess<uint8_t>>(value))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(property),
                                   ZX_PROP_EXCEPTION_STATE)
        ->SetId(kZxObjectSetProperty_ZX_PROP_EXCEPTION_STATE);
  }

  {
    Syscall* zx_object_get_info = Add("zx_object_get_info", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_object_get_info->Argument<zx_handle_t>(SyscallType::kHandle);
    auto topic =
        zx_object_get_info->Argument<zx_object_info_topic_t>(SyscallType::kObjectInfoTopic);
    auto buffer = zx_object_get_info->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto buffer_size = zx_object_get_info->Argument<size_t>(SyscallType::kSize);
    auto actual = zx_object_get_info->PointerArgument<size_t>(SyscallType::kSize);
    auto avail = zx_object_get_info->PointerArgument<size_t>(SyscallType::kSize);
    // Conditional field Ids
    const uint8_t kZxObjectGetInfo_ZX_INFO_HANDLE_BASIC = 1;
    const uint8_t kZxObjectGetInfo_ZX_INFO_HANDLE_COUNT = 2;
    const uint8_t kZxObjectGetInfo_ZX_INFO_PROCESS_HANDLE_STATS = 3;
    const uint8_t kZxObjectGetInfo_ZX_INFO_JOB = 4;
    const uint8_t kZxObjectGetInfo_ZX_INFO_PROCESS = 5;
    const uint8_t kZxObjectGetInfo_ZX_INFO_PROCESS_THREADS_actual = 6;
    const uint8_t kZxObjectGetInfo_ZX_INFO_PROCESS_THREADS_info = 7;
    const uint8_t kZxObjectGetInfo_ZX_INFO_THREAD = 8;
    const uint8_t kZxObjectGetInfo_ZX_INFO_THREAD_EXCEPTION_REPORT = 9;
    const uint8_t kZxObjectGetInfo_ZX_INFO_THREAD_STATS = 10;
    const uint8_t kZxObjectGetInfo_ZX_INFO_CPU_STATS = 11;
    const uint8_t kZxObjectGetInfo_ZX_INFO_VMAR = 12;
    const uint8_t kZxObjectGetInfo_ZX_INFO_VMO = 13;
    const uint8_t kZxObjectGetInfo_ZX_INFO_SOCKET = 14;
    const uint8_t kZxObjectGetInfo_ZX_INFO_TIMER = 15;
    const uint8_t kZxObjectGetInfo_ZX_INFO_JOB_CHILDREN_actual = 16;
    const uint8_t kZxObjectGetInfo_ZX_INFO_JOB_CHILDREN_info = 17;
    const uint8_t kZxObjectGetInfo_ZX_INFO_JOB_PROCESSES_actual = 18;
    const uint8_t kZxObjectGetInfo_ZX_INFO_JOB_PROCESSES_info = 19;
    const uint8_t kZxObjectGetInfo_ZX_INFO_TASK_STATS = 20;
    const uint8_t kZxObjectGetInfo_ZX_INFO_PROCESS_MAPS_actual = 21;
    const uint8_t kZxObjectGetInfo_ZX_INFO_PROCESS_MAPS_info = 22;
    const uint8_t kZxObjectGetInfo_ZX_INFO_PROCESS_VMOS_actual = 23;
    const uint8_t kZxObjectGetInfo_ZX_INFO_PROCESS_VMOS_info = 24;
    const uint8_t kZxObjectGetInfo_ZX_INFO_KMEM_STATS = 25;
    const uint8_t kZxObjectGetInfo_ZX_INFO_RESOURCE = 26;
    const uint8_t kZxObjectGetInfo_ZX_INFO_BTI = 27;
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
                                   ZX_INFO_HANDLE_BASIC)
        ->SetId(kZxObjectGetInfo_ZX_INFO_HANDLE_BASIC);
    zx_object_get_info
        ->OutputObject<zx_info_handle_count_t>(ZX_OK, "info",
                                               std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                               ZxInfoHandleCount::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_HANDLE_COUNT)
        ->SetId(kZxObjectGetInfo_ZX_INFO_HANDLE_COUNT);
    zx_object_get_info
        ->OutputObject<zx_info_process_handle_stats_t>(
            ZX_OK, "info", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxInfoProcessHandleStats::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_PROCESS_HANDLE_STATS)
        ->SetId(kZxObjectGetInfo_ZX_INFO_PROCESS_HANDLE_STATS);
    zx_object_get_info
        ->OutputObject<zx_info_job_t>(
            ZX_OK, "info", std::make_unique<ArgumentAccess<uint8_t>>(buffer), ZxInfoJob::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic), ZX_INFO_JOB)
        ->SetId(kZxObjectGetInfo_ZX_INFO_JOB);
    zx_object_get_info
        ->OutputObject<zx_info_process_t>(ZX_OK, "info",
                                          std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                          ZxInfoProcess::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_PROCESS)
        ->SetId(kZxObjectGetInfo_ZX_INFO_PROCESS);
    zx_object_get_info
        ->OutputActualAndRequested<size_t>(ZX_OK, "actual",
                                           std::make_unique<ArgumentAccess<size_t>>(actual),
                                           std::make_unique<ArgumentAccess<size_t>>(avail))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_PROCESS_THREADS)
        ->SetId(kZxObjectGetInfo_ZX_INFO_PROCESS_THREADS_actual);
    zx_object_get_info
        ->OutputBuffer<zx_koid_t, uint8_t, size_t>(
            ZX_OK, "info", SyscallType::kKoid, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            std::make_unique<ArgumentAccess<size_t>>(actual))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_PROCESS_THREADS)
        ->SetId(kZxObjectGetInfo_ZX_INFO_PROCESS_THREADS_info);
    zx_object_get_info
        ->OutputObject<zx_info_thread_t>(ZX_OK, "info",
                                         std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                         ZxInfoThread::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_THREAD)
        ->SetId(kZxObjectGetInfo_ZX_INFO_THREAD);
    zx_object_get_info
        ->OutputObject<zx_exception_report_t>(ZX_OK, "info",
                                              std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                              ZxExceptionReport::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_THREAD_EXCEPTION_REPORT)
        ->SetId(kZxObjectGetInfo_ZX_INFO_THREAD_EXCEPTION_REPORT);
    zx_object_get_info
        ->OutputObject<zx_info_thread_stats_t>(ZX_OK, "info",
                                               std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                               ZxInfoThreadStats::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_THREAD_STATS)
        ->SetId(kZxObjectGetInfo_ZX_INFO_THREAD_STATS);
    zx_object_get_info
        ->OutputObject<zx_info_cpu_stats_t>(ZX_OK, "info",
                                            std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                            ZxInfoCpuStats::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_CPU_STATS)
        ->SetId(kZxObjectGetInfo_ZX_INFO_CPU_STATS);
    zx_object_get_info
        ->OutputObject<zx_info_vmar_t>(ZX_OK, "info",
                                       std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                       ZxInfoVmar::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic), ZX_INFO_VMAR)
        ->SetId(kZxObjectGetInfo_ZX_INFO_VMAR);
    zx_object_get_info
        ->OutputObject<zx_info_vmo_t>(
            ZX_OK, "info", std::make_unique<ArgumentAccess<uint8_t>>(buffer), ZxInfoVmo::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic), ZX_INFO_VMO)
        ->SetId(kZxObjectGetInfo_ZX_INFO_VMO);
    zx_object_get_info
        ->OutputObject<zx_info_socket_t>(ZX_OK, "info",
                                         std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                         ZxInfoSocket::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_SOCKET)
        ->SetId(kZxObjectGetInfo_ZX_INFO_SOCKET);
    zx_object_get_info
        ->OutputObject<zx_info_timer_t>(ZX_OK, "info",
                                        std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                        ZxInfoTimer::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic), ZX_INFO_TIMER)
        ->SetId(kZxObjectGetInfo_ZX_INFO_TIMER);
    zx_object_get_info
        ->OutputActualAndRequested<size_t>(ZX_OK, "actual",
                                           std::make_unique<ArgumentAccess<size_t>>(actual),
                                           std::make_unique<ArgumentAccess<size_t>>(avail))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_JOB_CHILDREN)
        ->SetId(kZxObjectGetInfo_ZX_INFO_JOB_CHILDREN_actual);
    zx_object_get_info
        ->OutputBuffer<zx_koid_t, uint8_t, size_t>(
            ZX_OK, "info", SyscallType::kKoid, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            std::make_unique<ArgumentAccess<size_t>>(actual))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_JOB_CHILDREN)
        ->SetId(kZxObjectGetInfo_ZX_INFO_JOB_CHILDREN_info);
    zx_object_get_info
        ->OutputActualAndRequested<size_t>(ZX_OK, "actual",
                                           std::make_unique<ArgumentAccess<size_t>>(actual),
                                           std::make_unique<ArgumentAccess<size_t>>(avail))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_JOB_PROCESSES)
        ->SetId(kZxObjectGetInfo_ZX_INFO_JOB_PROCESSES_actual);
    zx_object_get_info
        ->OutputBuffer<zx_koid_t, uint8_t, size_t>(
            ZX_OK, "info", SyscallType::kKoid, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            std::make_unique<ArgumentAccess<size_t>>(actual))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_JOB_PROCESSES)
        ->SetId(kZxObjectGetInfo_ZX_INFO_JOB_PROCESSES_info);
    zx_object_get_info
        ->OutputObject<zx_info_task_stats_t>(ZX_OK, "info",
                                             std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                             ZxInfoTaskStats::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_TASK_STATS)
        ->SetId(kZxObjectGetInfo_ZX_INFO_TASK_STATS);
    zx_object_get_info
        ->OutputActualAndRequested<size_t>(ZX_OK, "actual",
                                           std::make_unique<ArgumentAccess<size_t>>(actual),
                                           std::make_unique<ArgumentAccess<size_t>>(avail))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_PROCESS_MAPS)
        ->SetId(kZxObjectGetInfo_ZX_INFO_PROCESS_MAPS_actual);
    zx_object_get_info
        ->OutputObjectArray<zx_info_maps_t, size_t>(
            ZX_OK, "info", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            std::make_unique<ArgumentAccess<size_t>>(actual), ZxInfoMaps::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_PROCESS_MAPS)
        ->SetId(kZxObjectGetInfo_ZX_INFO_PROCESS_MAPS_info);
    zx_object_get_info
        ->OutputActualAndRequested<size_t>(ZX_OK, "actual",
                                           std::make_unique<ArgumentAccess<size_t>>(actual),
                                           std::make_unique<ArgumentAccess<size_t>>(avail))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_PROCESS_VMOS)
        ->SetId(kZxObjectGetInfo_ZX_INFO_PROCESS_VMOS_actual);
    zx_object_get_info
        ->OutputObjectArray<zx_info_vmo_t, size_t>(
            ZX_OK, "info", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            std::make_unique<ArgumentAccess<size_t>>(actual), ZxInfoVmo::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_PROCESS_VMOS)
        ->SetId(kZxObjectGetInfo_ZX_INFO_PROCESS_VMOS_info);
    zx_object_get_info
        ->OutputObject<zx_info_kmem_stats_t>(ZX_OK, "info",
                                             std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                             ZxInfoKmemStats::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_KMEM_STATS)
        ->SetId(kZxObjectGetInfo_ZX_INFO_KMEM_STATS);
    zx_object_get_info
        ->OutputObject<zx_info_resource_t>(ZX_OK, "info",
                                           std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                           ZxInfoResource::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_INFO_RESOURCE)
        ->SetId(kZxObjectGetInfo_ZX_INFO_RESOURCE);
    zx_object_get_info
        ->OutputObject<zx_info_bti_t>(
            ZX_OK, "info", std::make_unique<ArgumentAccess<uint8_t>>(buffer), ZxInfoBti::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic), ZX_INFO_BTI)
        ->SetId(kZxObjectGetInfo_ZX_INFO_BTI);
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
    Syscall* zx_object_set_profile = Add("zx_object_set_profile", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_object_set_profile->Argument<zx_handle_t>(SyscallType::kHandle);
    auto profile = zx_object_set_profile->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_object_set_profile->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_object_set_profile->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_object_set_profile->Input<zx_handle_t>(
        "profile", std::make_unique<ArgumentAccess<zx_handle_t>>(profile));
    zx_object_set_profile->Input<uint32_t>("options",
                                           std::make_unique<ArgumentAccess<uint32_t>>(options));
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

    zx_channel_create->set_inference(&SyscallDecoderDispatcher::ZxChannelCreate);

    zx_channel_create->set_compute_statistics([](const OutputEvent* event) {
      auto out0 =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out0", /*invoked=*/false));
      FX_DCHECK(out0 != nullptr);
      out0->AddCreationEvent(event);
      auto out1 =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out1", /*invoked=*/false));
      FX_DCHECK(out1 != nullptr);
      out1->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_channel_read =
        Add("zx_channel_read", SyscallReturnType::kStatus, SyscallKind::kChannelRead);
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
        ZX_OK, "", fidl_codec::SyscallFidlType::kInputMessage,
        std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
        std::make_unique<ArgumentAccess<uint8_t>>(bytes),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes),
        std::make_unique<ArgumentAccess<zx_handle_t>>(handles),
        std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));
    zx_channel_read->Output<uint32_t>(ZX_ERR_BUFFER_TOO_SMALL, "actual_bytes",
                                      std::make_unique<ArgumentAccess<uint32_t>>(actual_bytes));
    zx_channel_read->Output<uint32_t>(ZX_ERR_BUFFER_TOO_SMALL, "actual_handles",
                                      std::make_unique<ArgumentAccess<uint32_t>>(actual_handles));

    zx_channel_read->set_inference(&SyscallDecoderDispatcher::ZxChannelRead);

    zx_channel_read->set_compute_statistics([](const OutputEvent* event) {
      const fidl_codec::FidlMessageValue* message = event->GetMessage();
      if ((message != nullptr) && !message->unknown_direction()) {
        CreateHandleVisitor visitor(event);
        message->Visit(&visitor, nullptr);
        event->thread()->process()->AddEvent(event, message);
      }
    });
  }

  {
    Syscall* zx_channel_read_etc =
        Add("zx_channel_read_etc", SyscallReturnType::kStatus, SyscallKind::kChannelRead);
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
        ZX_OK, "", fidl_codec::SyscallFidlType::kInputMessage,
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

    zx_channel_read_etc->set_inference(&SyscallDecoderDispatcher::ZxChannelRead);

    zx_channel_read_etc->set_compute_statistics([](const OutputEvent* event) {
      const fidl_codec::FidlMessageValue* message = event->GetMessage();
      if ((message != nullptr) && !message->unknown_direction()) {
        CreateHandleVisitor visitor(event);
        message->Visit(&visitor, nullptr);
        event->thread()->process()->AddEvent(event, message);
      }
    });
  }

  {
    Syscall* zx_channel_write =
        Add("zx_channel_write", SyscallReturnType::kStatus, SyscallKind::kChannelWrite);
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
    zx_channel_write->InputFidlMessage("", fidl_codec::SyscallFidlType::kOutputMessage,
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
                                       std::make_unique<ArgumentAccess<uint8_t>>(bytes),
                                       std::make_unique<ArgumentAccess<uint32_t>>(num_bytes),
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(handles),
                                       std::make_unique<ArgumentAccess<uint32_t>>(num_handles));

    zx_channel_write->set_inference(&SyscallDecoderDispatcher::ZxChannelWrite);

    zx_channel_write->set_compute_statistics([](const OutputEvent* event) {
      const fidl_codec::FidlMessageValue* message = event->invoked_event()->GetMessage();
      if ((message != nullptr) && !message->unknown_direction()) {
        CloseHandleVisitor visitor(event);
        message->Visit(&visitor, nullptr);
        event->thread()->process()->AddEvent(event, message);
      }
    });
  }
  {
    Syscall* zx_channel_call =
        Add("zx_channel_call", SyscallReturnType::kStatus, SyscallKind::kChannelCall);
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
        "", fidl_codec::SyscallFidlType::kOutputRequest,
        std::make_unique<ArgumentAccess<zx_handle_t>>(handle),
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
        ZX_OK, "", fidl_codec::SyscallFidlType::kInputResponse,
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

    zx_channel_call->set_inference(&SyscallDecoderDispatcher::ZxChannelCall);

    zx_channel_call->set_compute_statistics([](const OutputEvent* event) {
      const fidl_codec::FidlMessageValue* request = event->invoked_event()->GetMessage();
      if (request != nullptr) {
        CloseHandleVisitor close_handle_visitor(event);
        request->Visit(&close_handle_visitor, nullptr);
        event->thread()->process()->AddEvent(event, request);
      }
      const fidl_codec::FidlMessageValue* response = event->GetMessage();
      if (response != nullptr) {
        CreateHandleVisitor create_handle_visitor(event);
        response->Visit(&create_handle_visitor, nullptr);
      }
    });
  }

  {
    Syscall* zx_socket_create = Add("zx_socket_create", SyscallReturnType::kStatus);
    // Arguments
    auto options = zx_socket_create->Argument<uint32_t>(SyscallType::kSocketCreateOptions);
    auto out0 = zx_socket_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto out1 = zx_socket_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_socket_create->Input<uint32_t>("options",
                                      std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_socket_create->Output<zx_handle_t>(ZX_OK, "out0",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(out0));
    zx_socket_create->Output<zx_handle_t>(ZX_OK, "out1",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(out1));

    zx_socket_create->set_compute_statistics([](const OutputEvent* event) {
      auto out0 =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out0", /*invoked=*/false));
      FX_DCHECK(out0 != nullptr);
      out0->AddCreationEvent(event);
      auto out1 =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out1", /*invoked=*/false));
      FX_DCHECK(out1 != nullptr);
      out1->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_socket_write = Add("zx_socket_write", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_socket_write->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_socket_write->Argument<uint32_t>(SyscallType::kUint32);
    auto buffer = zx_socket_write->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto buffer_size = zx_socket_write->Argument<size_t>(SyscallType::kSize);
    auto actual = zx_socket_write->PointerArgument<size_t>(SyscallType::kSize);
    // Inputs
    zx_socket_write->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_socket_write->Input<uint32_t>("options",
                                     std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_socket_write->InputBuffer<uint8_t, uint8_t, size_t>(
        "buffer", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
    // Outputs
    zx_socket_write->OutputActualAndRequested<size_t>(
        ZX_OK, "actual", std::make_unique<ArgumentAccess<size_t>>(actual),
        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
  }

  {
    Syscall* zx_socket_read = Add("zx_socket_read", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_socket_read->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_socket_read->Argument<uint32_t>(SyscallType::kSocketReadOptions);
    auto buffer = zx_socket_read->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto buffer_size = zx_socket_read->Argument<size_t>(SyscallType::kSize);
    auto actual = zx_socket_read->PointerArgument<size_t>(SyscallType::kSize);
    // Inputs
    zx_socket_read->Input<zx_handle_t>("handle",
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_socket_read->Input<uint32_t>("options", std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_socket_read->Input<size_t>("buffer_size",
                                  std::make_unique<ArgumentAccess<size_t>>(buffer_size));
    // Outputs
    zx_socket_read->OutputActualAndRequested<size_t>(
        ZX_OK, "actual", std::make_unique<ArgumentAccess<size_t>>(actual),
        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
    zx_socket_read->OutputBuffer<uint8_t, uint8_t, size_t>(
        ZX_OK, "buffer", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
        std::make_unique<ArgumentAccess<size_t>>(actual));
  }

  {
    Syscall* zx_socket_shutdown = Add("zx_socket_shutdown", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_socket_shutdown->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_socket_shutdown->Argument<uint32_t>(SyscallType::kSocketShutdownOptions);
    // Inputs
    zx_socket_shutdown->Input<zx_handle_t>("handle",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_socket_shutdown->Input<uint32_t>("options",
                                        std::make_unique<ArgumentAccess<uint32_t>>(options));
  }

  { Add("zx_thread_exit", SyscallReturnType::kNoReturn); }

  {
    Syscall* zx_thread_create = Add("zx_thread_create", SyscallReturnType::kStatus);
    // Arguments
    auto process = zx_thread_create->Argument<zx_handle_t>(SyscallType::kHandle);
    auto name = zx_thread_create->PointerArgument<char>(SyscallType::kChar);
    auto name_size = zx_thread_create->Argument<size_t>(SyscallType::kSize);
    auto options = zx_thread_create->Argument<uint32_t>(SyscallType::kUint32);
    auto out = zx_thread_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_thread_create->Input<zx_handle_t>("process",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(process));
    zx_thread_create->InputString<char>("name", std::make_unique<ArgumentAccess<char>>(name),
                                        std::make_unique<ArgumentAccess<size_t>>(name_size));
    zx_thread_create->Input<uint32_t>("options",
                                      std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_thread_create->Output<zx_handle_t>(ZX_OK, "out",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_thread_create->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_thread_start = Add("zx_thread_start", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_thread_start->Argument<zx_handle_t>(SyscallType::kHandle);
    auto thread_entry = zx_thread_start->Argument<zx_vaddr_t>(SyscallType::kVaddr);
    auto stack = zx_thread_start->Argument<zx_vaddr_t>(SyscallType::kVaddr);
    auto arg1 = zx_thread_start->Argument<uintptr_t>(SyscallType::kUintptr);
    auto arg2 = zx_thread_start->Argument<uintptr_t>(SyscallType::kUintptr);
    // Inputs
    zx_thread_start->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_thread_start->Input<zx_vaddr_t>("thread_entry",
                                       std::make_unique<ArgumentAccess<zx_vaddr_t>>(thread_entry));
    zx_thread_start->Input<zx_vaddr_t>("stack",
                                       std::make_unique<ArgumentAccess<zx_vaddr_t>>(stack));
    zx_thread_start->Input<uintptr_t>("arg1", std::make_unique<ArgumentAccess<uintptr_t>>(arg1));
    zx_thread_start->Input<uintptr_t>("arg2", std::make_unique<ArgumentAccess<uintptr_t>>(arg2));
  }

  {
    Syscall* zx_thread_read_state = Add("zx_thread_read_state", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_thread_read_state->Argument<zx_handle_t>(SyscallType::kHandle);
    auto kind = zx_thread_read_state->Argument<uint32_t>(SyscallType::kThreadStateTopic);
    auto buffer = zx_thread_read_state->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto buffer_size = zx_thread_read_state->Argument<size_t>(SyscallType::kSize);
    // Conditional field Ids
    const uint8_t kZxThreadReadState_ZX_THREAD_STATE_GENERAL_REGS_Arm64 = 1;
    const uint8_t kZxThreadReadState_ZX_THREAD_STATE_GENERAL_REGS_X64 = 2;
    const uint8_t kZxThreadReadState_ZX_THREAD_STATE_FP_REGS_X64 = 3;
    const uint8_t kZxThreadReadState_ZX_THREAD_STATE_VECTOR_REGS_Arm64 = 4;
    const uint8_t kZxThreadReadState_ZX_THREAD_STATE_VECTOR_REGS_X64 = 5;
    const uint8_t kZxThreadReadState_ZX_THREAD_STATE_DEBUG_REGS_Arm64 = 6;
    const uint8_t kZxThreadReadState_ZX_THREAD_STATE_DEBUG_REGS_X64 = 7;
    const uint8_t kZxThreadReadState_ZX_THREAD_X86_REGISTER_FS_X64 = 8;
    const uint8_t kZxThreadReadState_ZX_THREAD_X86_REGISTER_GS_X64 = 9;
    // Inputs
    zx_thread_read_state->Input<zx_handle_t>("handle",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_thread_read_state->Input<uint32_t>("kind", std::make_unique<ArgumentAccess<uint32_t>>(kind));
    zx_thread_read_state->Input<size_t>("buffer_size",
                                        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
    // Outputs
    zx_thread_read_state
        ->OutputObject<zx_thread_state_general_regs_aarch64_t>(
            ZX_OK, "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateGeneralRegsAArch64::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_GENERAL_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kArm64)
        ->SetId(kZxThreadReadState_ZX_THREAD_STATE_GENERAL_REGS_Arm64);
    zx_thread_read_state
        ->OutputObject<zx_thread_state_general_regs_x86_t>(
            ZX_OK, "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateGeneralRegsX86::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_GENERAL_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxThreadReadState_ZX_THREAD_STATE_GENERAL_REGS_X64);
    zx_thread_read_state
        ->OutputObject<zx_thread_state_fp_regs_x86_t>(
            ZX_OK, "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateFpRegsX86::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_FP_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxThreadReadState_ZX_THREAD_STATE_FP_REGS_X64);
    zx_thread_read_state
        ->OutputObject<zx_thread_state_vector_regs_aarch64_t>(
            ZX_OK, "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateVectorRegsAArch64::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_VECTOR_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kArm64)
        ->SetId(kZxThreadReadState_ZX_THREAD_STATE_VECTOR_REGS_Arm64);
    zx_thread_read_state
        ->OutputObject<zx_thread_state_vector_regs_x86_t>(
            ZX_OK, "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateVectorRegsX86::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_VECTOR_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxThreadReadState_ZX_THREAD_STATE_VECTOR_REGS_X64);
    zx_thread_read_state
        ->OutputObject<zx_thread_state_debug_regs_aarch64_t>(
            ZX_OK, "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateDebugRegsAArch64::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_DEBUG_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kArm64)
        ->SetId(kZxThreadReadState_ZX_THREAD_STATE_DEBUG_REGS_Arm64);
    zx_thread_read_state
        ->OutputObject<zx_thread_state_debug_regs_x86_t>(
            ZX_OK, "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateDebugRegsX86::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_DEBUG_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxThreadReadState_ZX_THREAD_STATE_DEBUG_REGS_X64);
    zx_thread_read_state
        ->OutputIndirect<zx_thread_state_single_step_t, uint8_t>(
            ZX_OK, "single_step", SyscallType::kUint32,
            std::make_unique<ArgumentAccess<uint8_t>>(buffer))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_SINGLE_STEP);
    zx_thread_read_state
        ->OutputIndirect<zx_thread_x86_register_fs_t, uint8_t>(
            ZX_OK, "reg", SyscallType::kUint64Hexa,
            std::make_unique<ArgumentAccess<uint8_t>>(buffer))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_X86_REGISTER_FS)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxThreadReadState_ZX_THREAD_X86_REGISTER_FS_X64);
    zx_thread_read_state
        ->OutputIndirect<zx_thread_x86_register_gs_t, uint8_t>(
            ZX_OK, "reg", SyscallType::kUint64Hexa,
            std::make_unique<ArgumentAccess<uint8_t>>(buffer))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_X86_REGISTER_GS)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxThreadReadState_ZX_THREAD_X86_REGISTER_GS_X64);
  }

  {
    Syscall* zx_thread_write_state = Add("zx_thread_write_state", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_thread_write_state->Argument<zx_handle_t>(SyscallType::kHandle);
    auto kind = zx_thread_write_state->Argument<uint32_t>(SyscallType::kThreadStateTopic);
    auto buffer = zx_thread_write_state->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto buffer_size = zx_thread_write_state->Argument<size_t>(SyscallType::kSize);
    // Conditional field Ids
    const uint8_t kZxThreadWriteState_ZX_THREAD_STATE_GENERAL_REGS_Arm64 = 1;
    const uint8_t kZxThreadWriteState_ZX_THREAD_STATE_GENERAL_REGS_X64 = 2;
    const uint8_t kZxThreadWriteState_ZX_THREAD_STATE_FP_REGS_X64 = 3;
    const uint8_t kZxThreadWriteState_ZX_THREAD_STATE_VECTOR_REGS_Arm64 = 4;
    const uint8_t kZxThreadWriteState_ZX_THREAD_STATE_VECTOR_REGS_X64 = 5;
    const uint8_t kZxThreadWriteState_ZX_THREAD_STATE_DEBUG_REGS_Arm64 = 6;
    const uint8_t kZxThreadWriteState_ZX_THREAD_STATE_DEBUG_REGS_X64 = 7;
    const uint8_t kZxThreadWriteState_ZX_THREAD_X86_REGISTER_FS_X64 = 8;
    const uint8_t kZxThreadWriteState_ZX_THREAD_X86_REGISTER_GS_X64 = 9;
    // Inputs
    zx_thread_write_state->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_thread_write_state->Input<uint32_t>("kind",
                                           std::make_unique<ArgumentAccess<uint32_t>>(kind));
    zx_thread_write_state->Input<size_t>("buffer_size",
                                         std::make_unique<ArgumentAccess<size_t>>(buffer_size));
    zx_thread_write_state
        ->InputObject<zx_thread_state_general_regs_aarch64_t>(
            "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateGeneralRegsAArch64::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_GENERAL_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kArm64)
        ->SetId(kZxThreadWriteState_ZX_THREAD_STATE_GENERAL_REGS_Arm64);
    zx_thread_write_state
        ->InputObject<zx_thread_state_general_regs_x86_t>(
            "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateGeneralRegsX86::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_GENERAL_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxThreadWriteState_ZX_THREAD_STATE_GENERAL_REGS_X64);
    zx_thread_write_state
        ->InputObject<zx_thread_state_fp_regs_x86_t>(
            "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateFpRegsX86::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_FP_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxThreadWriteState_ZX_THREAD_STATE_FP_REGS_X64);
    zx_thread_write_state
        ->InputObject<zx_thread_state_vector_regs_aarch64_t>(
            "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateVectorRegsAArch64::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_VECTOR_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kArm64)
        ->SetId(kZxThreadWriteState_ZX_THREAD_STATE_VECTOR_REGS_Arm64);
    zx_thread_write_state
        ->InputObject<zx_thread_state_vector_regs_x86_t>(
            "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateVectorRegsX86::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_VECTOR_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxThreadWriteState_ZX_THREAD_STATE_VECTOR_REGS_X64);
    zx_thread_write_state
        ->InputObject<zx_thread_state_debug_regs_aarch64_t>(
            "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateDebugRegsAArch64::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_DEBUG_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kArm64)
        ->SetId(kZxThreadWriteState_ZX_THREAD_STATE_DEBUG_REGS_Arm64);
    zx_thread_write_state
        ->InputObject<zx_thread_state_debug_regs_x86_t>(
            "regs", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
            ZxThreadStateDebugRegsX86::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_DEBUG_REGS)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxThreadWriteState_ZX_THREAD_STATE_DEBUG_REGS_X64);
    zx_thread_write_state
        ->InputIndirect<zx_thread_state_single_step_t, uint8_t>(
            "single_step", SyscallType::kUint32, std::make_unique<ArgumentAccess<uint8_t>>(buffer))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_STATE_SINGLE_STEP);
    zx_thread_write_state
        ->InputIndirect<zx_thread_x86_register_fs_t, uint8_t>(
            "reg", SyscallType::kUint64Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_X86_REGISTER_FS)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxThreadWriteState_ZX_THREAD_X86_REGISTER_FS_X64);
    zx_thread_write_state
        ->InputIndirect<zx_thread_x86_register_gs_t, uint8_t>(
            "reg", SyscallType::kUint64Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer))
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind),
                                   ZX_THREAD_X86_REGISTER_GS)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxThreadWriteState_ZX_THREAD_X86_REGISTER_GS_X64);
  }

  {
    Syscall* zx_process_exit = Add("zx_process_exit", SyscallReturnType::kNoReturn);
    // Arguments
    auto retcode = zx_process_exit->Argument<int64_t>(SyscallType::kInt64);
    // Inputs
    zx_process_exit->Input<int64_t>("retcode", std::make_unique<ArgumentAccess<int64_t>>(retcode));
  }

  {
    Syscall* zx_process_create = Add("zx_process_create", SyscallReturnType::kStatus);
    // Arguments
    auto job = zx_process_create->Argument<zx_handle_t>(SyscallType::kHandle);
    auto name = zx_process_create->PointerArgument<char>(SyscallType::kChar);
    auto name_size = zx_process_create->Argument<size_t>(SyscallType::kSize);
    auto options = zx_process_create->Argument<uint32_t>(SyscallType::kUint32);
    auto proc_handle = zx_process_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto vmar_handle = zx_process_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_process_create->Input<zx_handle_t>("job",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(job));
    zx_process_create->InputString<char>("name", std::make_unique<ArgumentAccess<char>>(name),
                                         std::make_unique<ArgumentAccess<size_t>>(name_size));
    zx_process_create->Input<uint32_t>("options",
                                       std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_process_create->Output<zx_handle_t>(
        ZX_OK, "proc_handle", std::make_unique<ArgumentAccess<zx_handle_t>>(proc_handle));
    zx_process_create->Output<zx_handle_t>(
        ZX_OK, "vmar_handle", std::make_unique<ArgumentAccess<zx_handle_t>>(vmar_handle));

    zx_process_create->set_compute_statistics([](const OutputEvent* event) {
      auto proc_handle = event->GetHandleInfo(
          event->syscall()->SearchInlineMember("proc_handle", /*invoked=*/false));
      FX_DCHECK(proc_handle != nullptr);
      proc_handle->AddCreationEvent(event);
      auto vmar_handle = event->GetHandleInfo(
          event->syscall()->SearchInlineMember("vmar_handle", /*invoked=*/false));
      FX_DCHECK(vmar_handle != nullptr);
      vmar_handle->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_process_start = Add("zx_process_start", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_process_start->Argument<zx_handle_t>(SyscallType::kHandle);
    auto thread = zx_process_start->Argument<zx_handle_t>(SyscallType::kHandle);
    auto entry = zx_process_start->Argument<zx_vaddr_t>(SyscallType::kVaddr);
    auto stack = zx_process_start->Argument<zx_vaddr_t>(SyscallType::kVaddr);
    auto arg1 = zx_process_start->Argument<zx_handle_t>(SyscallType::kHandle);
    auto arg2 = zx_process_start->Argument<uintptr_t>(SyscallType::kUintptr);
    // Inputs
    zx_process_start->Input<zx_handle_t>("handle",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_process_start->Input<zx_handle_t>("thread",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(thread));
    zx_process_start->Input<zx_vaddr_t>("entry",
                                        std::make_unique<ArgumentAccess<zx_vaddr_t>>(entry));
    zx_process_start->Input<zx_vaddr_t>("stack",
                                        std::make_unique<ArgumentAccess<zx_vaddr_t>>(stack));
    zx_process_start->Input<zx_handle_t>("arg1",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(arg1));
    zx_process_start->Input<uintptr_t>("arg2", std::make_unique<ArgumentAccess<uintptr_t>>(arg2));
  }

  {
    Syscall* zx_process_read_memory = Add("zx_process_read_memory", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_process_read_memory->Argument<zx_handle_t>(SyscallType::kHandle);
    auto vaddr = zx_process_read_memory->Argument<zx_vaddr_t>(SyscallType::kVaddr);
    auto buffer = zx_process_read_memory->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto buffer_size = zx_process_read_memory->Argument<size_t>(SyscallType::kSize);
    auto actual = zx_process_read_memory->PointerArgument<size_t>(SyscallType::kSize);
    // Inputs
    zx_process_read_memory->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_process_read_memory->Input<zx_vaddr_t>("vaddr",
                                              std::make_unique<ArgumentAccess<zx_vaddr_t>>(vaddr));
    zx_process_read_memory->Input<size_t>("buffer_size",
                                          std::make_unique<ArgumentAccess<size_t>>(buffer_size));
    // Outputs
    zx_process_read_memory->OutputBuffer<uint8_t, uint8_t, size_t>(
        ZX_OK, "buffer", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
        std::make_unique<ArgumentAccess<size_t>>(actual));
  }

  {
    Syscall* zx_process_write_memory = Add("zx_process_write_memory", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_process_write_memory->Argument<zx_handle_t>(SyscallType::kHandle);
    auto vaddr = zx_process_write_memory->Argument<zx_vaddr_t>(SyscallType::kVaddr);
    auto buffer = zx_process_write_memory->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto buffer_size = zx_process_write_memory->Argument<size_t>(SyscallType::kSize);
    auto actual = zx_process_write_memory->PointerArgument<size_t>(SyscallType::kSize);
    // Inputs
    zx_process_write_memory->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_process_write_memory->Input<zx_vaddr_t>("vaddr",
                                               std::make_unique<ArgumentAccess<zx_vaddr_t>>(vaddr));
    zx_process_write_memory->InputBuffer<uint8_t, uint8_t, size_t>(
        "buffer", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
    // Outputs
    zx_process_write_memory->Output<size_t>(ZX_OK, "actual",
                                            std::make_unique<ArgumentAccess<size_t>>(actual));
  }

  {
    Syscall* zx_job_create = Add("zx_job_create", SyscallReturnType::kStatus);
    // Arguments
    auto parent_job = zx_job_create->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_job_create->Argument<uint32_t>(SyscallType::kUint32);
    auto out = zx_job_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_job_create->Input<zx_handle_t>("parent_job",
                                      std::make_unique<ArgumentAccess<zx_handle_t>>(parent_job));
    zx_job_create->Input<uint32_t>("options", std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_job_create->Output<zx_handle_t>(ZX_OK, "out",
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_job_create->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_job_set_policy = Add("zx_job_set_policy", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_job_set_policy->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_job_set_policy->Argument<uint32_t>(SyscallType::kUint32);
    auto topic = zx_job_set_policy->Argument<uint32_t>(SyscallType::kPolicyTopic);
    auto policy = zx_job_set_policy->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto count = zx_job_set_policy->Argument<uint32_t>(SyscallType::kUint32);
    // Conditional field Ids
    const uint8_t kZxJobSetPolicy_ZX_JOB_POL_BASIC = 1;
    const uint8_t kZxJobSetPolicy_ZX_JOB_POL_TIMER_SLACK = 2;
    // Inputs
    zx_job_set_policy->Input<zx_handle_t>("handle",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_job_set_policy->Input<uint32_t>("options",
                                       std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_job_set_policy->Input<uint32_t>("topic", std::make_unique<ArgumentAccess<uint32_t>>(topic));
    zx_job_set_policy
        ->InputObjectArray<zx_policy_basic_t, uint32_t>(
            "policy", std::make_unique<ArgumentAccess<uint8_t>>(policy),
            std::make_unique<ArgumentAccess<uint32_t>>(count), ZxPolicyBasic::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_JOB_POL_BASIC)
        ->SetId(kZxJobSetPolicy_ZX_JOB_POL_BASIC);
    zx_job_set_policy
        ->InputObject<zx_policy_timer_slack_t>("policy",
                                               std::make_unique<ArgumentAccess<uint8_t>>(policy),
                                               ZxPolicyTimerSlack::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(topic),
                                   ZX_JOB_POL_TIMER_SLACK)
        ->SetId(kZxJobSetPolicy_ZX_JOB_POL_TIMER_SLACK);
  }

  {
    Syscall* zx_task_suspend = Add("zx_task_suspend", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_task_suspend->Argument<zx_handle_t>(SyscallType::kHandle);
    auto token = zx_task_suspend->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_task_suspend->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    // Outputs
    zx_task_suspend->Output<zx_handle_t>(ZX_OK, "token",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(token));
  }

  {
    Syscall* zx_task_suspend_token = Add("zx_task_suspend_token", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_task_suspend_token->Argument<zx_handle_t>(SyscallType::kHandle);
    auto token = zx_task_suspend_token->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_task_suspend_token->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    // Outputs
    zx_task_suspend_token->Output<zx_handle_t>(
        ZX_OK, "token", std::make_unique<ArgumentAccess<zx_handle_t>>(token));
  }

  {
    Syscall* zx_task_create_exception_channel =
        Add("zx_task_create_exception_channel", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_task_create_exception_channel->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_task_create_exception_channel->Argument<uint32_t>(SyscallType::kUint32);
    auto out = zx_task_create_exception_channel->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_task_create_exception_channel->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_task_create_exception_channel->Input<uint32_t>(
        "options", std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_task_create_exception_channel->Output<zx_handle_t>(
        ZX_OK, "out", std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_task_create_exception_channel->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_task_kill = Add("zx_task_kill", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_task_kill->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_task_kill->Input<zx_handle_t>("handle",
                                     std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
  }

  {
    Syscall* zx_exception_get_thread = Add("zx_exception_get_thread", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_exception_get_thread->Argument<zx_handle_t>(SyscallType::kHandle);
    auto out = zx_exception_get_thread->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_exception_get_thread->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    // Outputs
    zx_exception_get_thread->Output<zx_handle_t>(
        ZX_OK, "out", std::make_unique<ArgumentAccess<zx_handle_t>>(out));
  }

  {
    Syscall* zx_exception_get_process = Add("zx_exception_get_process", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_exception_get_process->Argument<zx_handle_t>(SyscallType::kHandle);
    auto out = zx_exception_get_process->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_exception_get_process->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    // Outputs
    zx_exception_get_process->Output<zx_handle_t>(
        ZX_OK, "out", std::make_unique<ArgumentAccess<zx_handle_t>>(out));
  }

  {
    Syscall* zx_event_create = Add("zx_event_create", SyscallReturnType::kStatus);
    // Arguments
    auto options = zx_event_create->Argument<uint32_t>(SyscallType::kUint32);
    auto out = zx_event_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_event_create->Input<uint32_t>("options",
                                     std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_event_create->Output<zx_handle_t>(ZX_OK, "out",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_event_create->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_eventpair_create = Add("zx_eventpair_create", SyscallReturnType::kStatus);
    // Arguments
    auto options = zx_eventpair_create->Argument<uint32_t>(SyscallType::kUint32);
    auto out0 = zx_eventpair_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto out1 = zx_eventpair_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_eventpair_create->Input<uint32_t>("options",
                                         std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_eventpair_create->Output<zx_handle_t>(ZX_OK, "out0",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(out0));
    zx_eventpair_create->Output<zx_handle_t>(ZX_OK, "out1",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(out1));

    zx_eventpair_create->set_compute_statistics([](const OutputEvent* event) {
      auto out0 =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out0", /*invoked=*/false));
      FX_DCHECK(out0 != nullptr);
      out0->AddCreationEvent(event);
      auto out1 =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out1", /*invoked=*/false));
      FX_DCHECK(out1 != nullptr);
      out1->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_futex_wait = Add("zx_futex_wait", SyscallReturnType::kStatus);
    // Arguments
    auto value_ptr = zx_futex_wait->PointerArgument<zx_futex_t>(SyscallType::kFutex);
    auto current_value = zx_futex_wait->Argument<zx_futex_t>(SyscallType::kFutex);
    auto new_futex_owner = zx_futex_wait->Argument<zx_handle_t>(SyscallType::kHandle);
    auto deadline = zx_futex_wait->Argument<zx_time_t>(SyscallType::kTime);
    // Inputs
    zx_futex_wait->Input<zx_futex_t>("value_ptr",
                                     std::make_unique<ArgumentAccess<zx_futex_t>>(value_ptr));
    zx_futex_wait->Input<zx_futex_t>("current_value",
                                     std::make_unique<ArgumentAccess<zx_futex_t>>(current_value));
    zx_futex_wait->Input<zx_handle_t>(
        "new_futex_owner", std::make_unique<ArgumentAccess<zx_handle_t>>(new_futex_owner));
    zx_futex_wait->Input<zx_time_t>("deadline",
                                    std::make_unique<ArgumentAccess<zx_time_t>>(deadline));
  }

  {
    Syscall* zx_futex_wake = Add("zx_futex_wake", SyscallReturnType::kStatus);
    // Arguments
    auto value_ptr = zx_futex_wake->PointerArgument<zx_futex_t>(SyscallType::kFutex);
    auto wake_count = zx_futex_wake->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_futex_wake->Input<zx_futex_t>("value_ptr",
                                     std::make_unique<ArgumentAccess<zx_futex_t>>(value_ptr));
    zx_futex_wake->Input<uint32_t>("wake_count",
                                   std::make_unique<ArgumentAccess<uint32_t>>(wake_count));
  }

  {
    Syscall* zx_futex_requeue = Add("zx_futex_requeue", SyscallReturnType::kStatus);
    // Arguments
    auto value_ptr = zx_futex_requeue->PointerArgument<zx_futex_t>(SyscallType::kFutex);
    auto wake_count = zx_futex_requeue->Argument<uint32_t>(SyscallType::kUint32);
    auto current_value = zx_futex_requeue->Argument<zx_futex_t>(SyscallType::kFutex);
    auto requeue_ptr = zx_futex_requeue->PointerArgument<zx_futex_t>(SyscallType::kFutex);
    auto requeue_count = zx_futex_requeue->Argument<uint32_t>(SyscallType::kUint32);
    auto new_requeue_owner = zx_futex_requeue->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_futex_requeue->Input<zx_futex_t>("value_ptr",
                                        std::make_unique<ArgumentAccess<zx_futex_t>>(value_ptr));
    zx_futex_requeue->Input<uint32_t>("wake_count",
                                      std::make_unique<ArgumentAccess<uint32_t>>(wake_count));
    zx_futex_requeue->Input<zx_futex_t>(
        "current_value", std::make_unique<ArgumentAccess<zx_futex_t>>(current_value));
    zx_futex_requeue->Input<zx_futex_t>("requeue_ptr",
                                        std::make_unique<ArgumentAccess<zx_futex_t>>(requeue_ptr));
    zx_futex_requeue->Input<uint32_t>("requeue_count",
                                      std::make_unique<ArgumentAccess<uint32_t>>(requeue_count));
    zx_futex_requeue->Input<zx_handle_t>(
        "new_requeue_owner", std::make_unique<ArgumentAccess<zx_handle_t>>(new_requeue_owner));
  }

  {
    Syscall* zx_futex_wake_single_owner =
        Add("zx_futex_wake_single_owner", SyscallReturnType::kStatus);
    // Arguments
    auto value_ptr = zx_futex_wake_single_owner->PointerArgument<zx_futex_t>(SyscallType::kFutex);
    // Inputs
    zx_futex_wake_single_owner->Input<zx_futex_t>(
        "value_ptr", std::make_unique<ArgumentAccess<zx_futex_t>>(value_ptr));
  }

  {
    Syscall* zx_futex_requeue_single_owner =
        Add("zx_futex_requeue_single_owner", SyscallReturnType::kStatus);
    // Arguments
    auto value_ptr =
        zx_futex_requeue_single_owner->PointerArgument<zx_futex_t>(SyscallType::kFutex);
    auto current_value = zx_futex_requeue_single_owner->Argument<zx_futex_t>(SyscallType::kFutex);
    auto requeue_ptr =
        zx_futex_requeue_single_owner->PointerArgument<zx_futex_t>(SyscallType::kFutex);
    auto requeue_count = zx_futex_requeue_single_owner->Argument<uint32_t>(SyscallType::kUint32);
    auto new_requeue_owner =
        zx_futex_requeue_single_owner->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_futex_requeue_single_owner->Input<zx_futex_t>(
        "value_ptr", std::make_unique<ArgumentAccess<zx_futex_t>>(value_ptr));
    zx_futex_requeue_single_owner->Input<zx_futex_t>(
        "current_value", std::make_unique<ArgumentAccess<zx_futex_t>>(current_value));
    zx_futex_requeue_single_owner->Input<zx_futex_t>(
        "requeue_ptr", std::make_unique<ArgumentAccess<zx_futex_t>>(requeue_ptr));
    zx_futex_requeue_single_owner->Input<uint32_t>(
        "requeue_count", std::make_unique<ArgumentAccess<uint32_t>>(requeue_count));
    zx_futex_requeue_single_owner->Input<zx_handle_t>(
        "new_requeue_owner", std::make_unique<ArgumentAccess<zx_handle_t>>(new_requeue_owner));
  }

  {
    Syscall* zx_futex_get_owner = Add("zx_futex_get_owner", SyscallReturnType::kStatus);
    // Arguments
    auto value_ptr = zx_futex_get_owner->PointerArgument<zx_futex_t>(SyscallType::kFutex);
    auto koid = zx_futex_get_owner->PointerArgument<zx_koid_t>(SyscallType::kKoid);
    // Inputs
    zx_futex_get_owner->Input<zx_futex_t>("value_ptr",
                                          std::make_unique<ArgumentAccess<zx_futex_t>>(value_ptr));
    // Outputs
    zx_futex_get_owner->Output<zx_koid_t>(ZX_OK, "koid",
                                          std::make_unique<ArgumentAccess<zx_koid_t>>(koid));
  }

  {
    Syscall* zx_futex_wake_handle_close_thread_exit =
        Add("zx_futex_wake_handle_close_thread_exit", SyscallReturnType::kNoReturn);
    // Arguments
    auto value_ptr =
        zx_futex_wake_handle_close_thread_exit->PointerArgument<zx_futex_t>(SyscallType::kFutex);
    auto wake_count =
        zx_futex_wake_handle_close_thread_exit->Argument<uint32_t>(SyscallType::kUint32);
    auto new_value = zx_futex_wake_handle_close_thread_exit->Argument<int32_t>(SyscallType::kInt32);
    auto close_handle =
        zx_futex_wake_handle_close_thread_exit->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_futex_wake_handle_close_thread_exit->Input<zx_futex_t>(
        "value_ptr", std::make_unique<ArgumentAccess<zx_futex_t>>(value_ptr));
    zx_futex_wake_handle_close_thread_exit->Input<uint32_t>(
        "wake_count", std::make_unique<ArgumentAccess<uint32_t>>(wake_count));
    zx_futex_wake_handle_close_thread_exit->Input<int32_t>(
        "new_value", std::make_unique<ArgumentAccess<int32_t>>(new_value));
    zx_futex_wake_handle_close_thread_exit->Input<zx_handle_t>(
        "close_handle", std::make_unique<ArgumentAccess<zx_handle_t>>(close_handle));
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

    zx_port_create->set_inference(&SyscallDecoderDispatcher::ZxPortCreate);

    zx_port_create->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
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

  {
    Syscall* zx_timer_create = Add("zx_timer_create", SyscallReturnType::kStatus);
    // Arguments
    auto options = zx_timer_create->Argument<uint32_t>(SyscallType::kUint32);
    auto clock_id = zx_timer_create->Argument<zx_clock_t>(SyscallType::kClock);
    auto out = zx_timer_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_timer_create->Input<uint32_t>("options",
                                     std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_timer_create->Input<zx_clock_t>("clock_id",
                                       std::make_unique<ArgumentAccess<zx_clock_t>>(clock_id));
    // Outputs
    zx_timer_create->Output<zx_handle_t>(ZX_OK, "out",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_timer_create->set_inference(&SyscallDecoderDispatcher::ZxTimerCreate);

    zx_timer_create->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_timer_set = Add("zx_timer_set", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_timer_set->Argument<zx_handle_t>(SyscallType::kHandle);
    auto deadline = zx_timer_set->Argument<zx_time_t>(SyscallType::kMonotonicTime);
    auto slack = zx_timer_set->Argument<zx_duration_t>(SyscallType::kDuration);
    // Inputs
    zx_timer_set->Input<zx_handle_t>("handle",
                                     std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_timer_set->Input<zx_time_t>("deadline",
                                   std::make_unique<ArgumentAccess<zx_time_t>>(deadline));
    zx_timer_set->Input<zx_duration_t>("slack",
                                       std::make_unique<ArgumentAccess<zx_duration_t>>(slack));
  }

  {
    Syscall* zx_timer_cancel = Add("zx_timer_cancel", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_timer_cancel->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_timer_cancel->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
  }

  {
    Syscall* zx_vmo_create = Add("zx_vmo_create", SyscallReturnType::kStatus);
    // Arguments
    auto size = zx_vmo_create->Argument<uint64_t>(SyscallType::kUint64);
    auto options = zx_vmo_create->Argument<uint32_t>(SyscallType::kVmoCreationOption);
    auto out = zx_vmo_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_vmo_create->Input<uint64_t>("size", std::make_unique<ArgumentAccess<uint64_t>>(size));
    zx_vmo_create->Input<uint32_t>("options", std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_vmo_create->Output<zx_handle_t>(ZX_OK, "out",
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_vmo_create->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_vmo_read = Add("zx_vmo_read", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vmo_read->Argument<zx_handle_t>(SyscallType::kHandle);
    auto buffer = zx_vmo_read->PointerArgument<uint8_t>(SyscallType::kUint8Hexa);
    auto offset = zx_vmo_read->Argument<uint64_t>(SyscallType::kUint64);
    auto buffer_size = zx_vmo_read->Argument<size_t>(SyscallType::kSize);
    // Inputs
    zx_vmo_read->Input<zx_handle_t>("handle",
                                    std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vmo_read->Input<uint64_t>("offset", std::make_unique<ArgumentAccess<uint64_t>>(offset));
    // Outputs
    zx_vmo_read->OutputBuffer<uint8_t, uint8_t, size_t>(
        ZX_OK, "buffer", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
  }

  {
    Syscall* zx_vmo_write = Add("zx_vmo_write", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vmo_write->Argument<zx_handle_t>(SyscallType::kHandle);
    auto buffer = zx_vmo_write->PointerArgument<uint8_t>(SyscallType::kUint8Hexa);
    auto offset = zx_vmo_write->Argument<uint64_t>(SyscallType::kUint64);
    auto buffer_size = zx_vmo_write->Argument<size_t>(SyscallType::kSize);
    // Inputs
    zx_vmo_write->Input<zx_handle_t>("handle",
                                     std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vmo_write->Input<uint64_t>("offset", std::make_unique<ArgumentAccess<uint64_t>>(offset));
    zx_vmo_write->InputBuffer<uint8_t, uint8_t, size_t>(
        "buffer", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
  }

  {
    Syscall* zx_vmo_get_size = Add("zx_vmo_get_size", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vmo_get_size->Argument<zx_handle_t>(SyscallType::kHandle);
    auto size = zx_vmo_get_size->PointerArgument<uint64_t>(SyscallType::kUint64);
    // Inputs
    zx_vmo_get_size->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    // Outputs
    zx_vmo_get_size->Output<uint64_t>(ZX_OK, "size",
                                      std::make_unique<ArgumentAccess<uint64_t>>(size));
  }

  {
    Syscall* zx_vmo_set_size = Add("zx_vmo_set_size", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vmo_set_size->Argument<zx_handle_t>(SyscallType::kHandle);
    auto size = zx_vmo_set_size->Argument<uint64_t>(SyscallType::kUint64);
    // Inputs
    zx_vmo_set_size->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vmo_set_size->Input<uint64_t>("size", std::make_unique<ArgumentAccess<uint64_t>>(size));
  }

  {
    Syscall* zx_vmo_op_range = Add("zx_vmo_op_range", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vmo_op_range->Argument<zx_handle_t>(SyscallType::kHandle);
    auto op = zx_vmo_op_range->Argument<uint32_t>(SyscallType::kVmoOp);
    auto offset = zx_vmo_op_range->Argument<uint64_t>(SyscallType::kUint64);
    auto size = zx_vmo_op_range->Argument<uint64_t>(SyscallType::kUint64);
    zx_vmo_op_range->PointerArgument<uint8_t>(SyscallType::kUint8);
    zx_vmo_op_range->Argument<size_t>(SyscallType::kSize);
    // Inputs
    zx_vmo_op_range->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vmo_op_range->Input<uint32_t>("op", std::make_unique<ArgumentAccess<uint32_t>>(op));
    zx_vmo_op_range->Input<uint64_t>("offset", std::make_unique<ArgumentAccess<uint64_t>>(offset));
    zx_vmo_op_range->Input<uint64_t>("size", std::make_unique<ArgumentAccess<uint64_t>>(size));
  }

  {
    Syscall* zx_vmo_create_child = Add("zx_vmo_create_child", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vmo_create_child->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_vmo_create_child->Argument<uint32_t>(SyscallType::kVmoOption);
    auto offset = zx_vmo_create_child->Argument<uint64_t>(SyscallType::kUint64);
    auto size = zx_vmo_create_child->Argument<uint64_t>(SyscallType::kUint64);
    auto out = zx_vmo_create_child->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_vmo_create_child->Input<zx_handle_t>("handle",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vmo_create_child->Input<uint32_t>("options",
                                         std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_vmo_create_child->Input<uint64_t>("offset",
                                         std::make_unique<ArgumentAccess<uint64_t>>(offset));
    zx_vmo_create_child->Input<uint64_t>("size", std::make_unique<ArgumentAccess<uint64_t>>(size));
    // Outputs
    zx_vmo_create_child->Output<zx_handle_t>(ZX_OK, "out",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_vmo_create_child->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_vmo_set_cache_policy = Add("zx_vmo_set_cache_policy", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vmo_set_cache_policy->Argument<zx_handle_t>(SyscallType::kHandle);
    auto cache_policy = zx_vmo_set_cache_policy->Argument<uint32_t>(SyscallType::kCachePolicy);
    // Inputs
    zx_vmo_set_cache_policy->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vmo_set_cache_policy->Input<uint32_t>(
        "cache_policy", std::make_unique<ArgumentAccess<uint32_t>>(cache_policy));
  }

  {
    Syscall* zx_vmo_replace_as_executable =
        Add("zx_vmo_replace_as_executable", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vmo_replace_as_executable->Argument<zx_handle_t>(SyscallType::kHandle);
    auto vmex = zx_vmo_replace_as_executable->Argument<zx_handle_t>(SyscallType::kHandle);
    auto out = zx_vmo_replace_as_executable->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_vmo_replace_as_executable->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vmo_replace_as_executable->Input<zx_handle_t>(
        "vmex", std::make_unique<ArgumentAccess<zx_handle_t>>(vmex));
    // Outputs
    zx_vmo_replace_as_executable->Output<zx_handle_t>(
        ZX_OK, "out", std::make_unique<ArgumentAccess<zx_handle_t>>(out));
  }

  {
    Syscall* zx_vmo_create_contiguous = Add("zx_vmo_create_contiguous", SyscallReturnType::kStatus);
    // Arguments
    auto bti = zx_vmo_create_contiguous->Argument<zx_handle_t>(SyscallType::kHandle);
    auto size = zx_vmo_create_contiguous->Argument<size_t>(SyscallType::kSize);
    auto alignment_log2 = zx_vmo_create_contiguous->Argument<uint32_t>(SyscallType::kUint32);
    auto out = zx_vmo_create_contiguous->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_vmo_create_contiguous->Input<zx_handle_t>(
        "bti", std::make_unique<ArgumentAccess<zx_handle_t>>(bti));
    zx_vmo_create_contiguous->Input<size_t>("size", std::make_unique<ArgumentAccess<size_t>>(size));
    zx_vmo_create_contiguous->Input<uint32_t>(
        "alignment_log2", std::make_unique<ArgumentAccess<uint32_t>>(alignment_log2));
    // Outputs
    zx_vmo_create_contiguous->Output<zx_handle_t>(
        ZX_OK, "out", std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_vmo_create_contiguous->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_vmo_create_physical = Add("zx_vmo_create_physical", SyscallReturnType::kStatus);
    // Arguments
    auto resource = zx_vmo_create_physical->Argument<zx_handle_t>(SyscallType::kHandle);
    auto paddr = zx_vmo_create_physical->Argument<zx_paddr_t>(SyscallType::kPaddr);
    auto size = zx_vmo_create_physical->Argument<size_t>(SyscallType::kSize);
    auto out = zx_vmo_create_physical->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_vmo_create_physical->Input<zx_handle_t>(
        "resource", std::make_unique<ArgumentAccess<zx_handle_t>>(resource));
    zx_vmo_create_physical->Input<zx_paddr_t>("paddr",
                                              std::make_unique<ArgumentAccess<zx_paddr_t>>(paddr));
    zx_vmo_create_physical->Input<size_t>("size", std::make_unique<ArgumentAccess<size_t>>(size));
    // Outputs
    zx_vmo_create_physical->Output<zx_handle_t>(ZX_OK, "out",
                                                std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_vmo_create_physical->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_vmar_allocate = Add("zx_vmar_allocate", SyscallReturnType::kStatus);
    // Arguments
    auto parent_vmar = zx_vmar_allocate->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_vmar_allocate->Argument<zx_vm_option_t>(SyscallType::kVmOption);
    auto offset = zx_vmar_allocate->Argument<uint64_t>(SyscallType::kUint64);
    auto size = zx_vmar_allocate->Argument<uint64_t>(SyscallType::kUint64);
    auto child_vmar = zx_vmar_allocate->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto child_addr = zx_vmar_allocate->PointerArgument<zx_vaddr_t>(SyscallType::kVaddr);
    // Inputs
    zx_vmar_allocate->Input<zx_handle_t>(
        "parent_vmar", std::make_unique<ArgumentAccess<zx_handle_t>>(parent_vmar));
    zx_vmar_allocate->Input<zx_vm_option_t>(
        "options", std::make_unique<ArgumentAccess<zx_vm_option_t>>(options));
    zx_vmar_allocate->Input<uint64_t>("offset", std::make_unique<ArgumentAccess<uint64_t>>(offset));
    zx_vmar_allocate->Input<uint64_t>("size", std::make_unique<ArgumentAccess<uint64_t>>(size));
    // Outputs
    zx_vmar_allocate->Output<zx_handle_t>(
        ZX_OK, "child_vmar", std::make_unique<ArgumentAccess<zx_handle_t>>(child_vmar));
    zx_vmar_allocate->Output<zx_vaddr_t>(ZX_OK, "child_addr",
                                         std::make_unique<ArgumentAccess<zx_vaddr_t>>(child_addr));
  }

  {
    Syscall* zx_vmar_destroy = Add("zx_vmar_destroy", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vmar_destroy->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_vmar_destroy->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
  }

  {
    Syscall* zx_vmar_map = Add("zx_vmar_map", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vmar_map->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_vmar_map->Argument<zx_vm_option_t>(SyscallType::kVmOption);
    auto vmar_offset = zx_vmar_map->Argument<uint64_t>(SyscallType::kUint64);
    auto vmo = zx_vmar_map->Argument<zx_handle_t>(SyscallType::kHandle);
    auto vmo_offset = zx_vmar_map->Argument<uint64_t>(SyscallType::kUint64);
    auto len = zx_vmar_map->Argument<uint64_t>(SyscallType::kUint64);
    auto mapped_addr = zx_vmar_map->PointerArgument<zx_vaddr_t>(SyscallType::kVaddr);
    // Inputs
    zx_vmar_map->Input<zx_handle_t>("handle",
                                    std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vmar_map->Input<zx_vm_option_t>("options",
                                       std::make_unique<ArgumentAccess<zx_vm_option_t>>(options));
    zx_vmar_map->Input<uint64_t>("vmar_offset",
                                 std::make_unique<ArgumentAccess<uint64_t>>(vmar_offset));
    zx_vmar_map->Input<zx_handle_t>("vmo", std::make_unique<ArgumentAccess<zx_handle_t>>(vmo));
    zx_vmar_map->Input<uint64_t>("vmo_offset",
                                 std::make_unique<ArgumentAccess<uint64_t>>(vmo_offset));
    zx_vmar_map->Input<uint64_t>("len", std::make_unique<ArgumentAccess<uint64_t>>(len));
    // Outputs
    zx_vmar_map->Output<zx_vaddr_t>(ZX_OK, "mapped_addr",
                                    std::make_unique<ArgumentAccess<zx_vaddr_t>>(mapped_addr));
  }

  {
    Syscall* zx_vmar_unmap = Add("zx_vmar_unmap", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vmar_unmap->Argument<zx_handle_t>(SyscallType::kHandle);
    auto addr = zx_vmar_unmap->Argument<zx_vaddr_t>(SyscallType::kVaddr);
    auto len = zx_vmar_unmap->Argument<uint64_t>(SyscallType::kUint64);
    // Inputs
    zx_vmar_unmap->Input<zx_handle_t>("handle",
                                      std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vmar_unmap->Input<zx_vaddr_t>("addr", std::make_unique<ArgumentAccess<zx_vaddr_t>>(addr));
    zx_vmar_unmap->Input<uint64_t>("len", std::make_unique<ArgumentAccess<uint64_t>>(len));
  }

  {
    Syscall* zx_vmar_protect = Add("zx_vmar_protect", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vmar_protect->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_vmar_protect->Argument<zx_vm_option_t>(SyscallType::kVmOption);
    auto addr = zx_vmar_protect->Argument<zx_vaddr_t>(SyscallType::kVaddr);
    auto len = zx_vmar_protect->Argument<uint64_t>(SyscallType::kUint64);
    // Inputs
    zx_vmar_protect->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vmar_protect->Input<zx_vm_option_t>(
        "options", std::make_unique<ArgumentAccess<zx_vm_option_t>>(options));
    zx_vmar_protect->Input<zx_vaddr_t>("addr", std::make_unique<ArgumentAccess<zx_vaddr_t>>(addr));
    zx_vmar_protect->Input<uint64_t>("len", std::make_unique<ArgumentAccess<uint64_t>>(len));
  }

  {
    Syscall* zx_vmar_unmap_handle_close_thread_exit =
        Add("zx_vmar_unmap_handle_close_thread_exit", SyscallReturnType::kStatus);
    // Arguments
    auto vmar_handle =
        zx_vmar_unmap_handle_close_thread_exit->Argument<zx_handle_t>(SyscallType::kHandle);
    auto addr = zx_vmar_unmap_handle_close_thread_exit->Argument<zx_vaddr_t>(SyscallType::kVaddr);
    auto size = zx_vmar_unmap_handle_close_thread_exit->Argument<size_t>(SyscallType::kSize);
    auto close_handle =
        zx_vmar_unmap_handle_close_thread_exit->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_vmar_unmap_handle_close_thread_exit->Input<zx_handle_t>(
        "vmar_handle", std::make_unique<ArgumentAccess<zx_handle_t>>(vmar_handle));
    zx_vmar_unmap_handle_close_thread_exit->Input<zx_vaddr_t>(
        "addr", std::make_unique<ArgumentAccess<zx_vaddr_t>>(addr));
    zx_vmar_unmap_handle_close_thread_exit->Input<size_t>(
        "size", std::make_unique<ArgumentAccess<size_t>>(size));
    zx_vmar_unmap_handle_close_thread_exit->Input<zx_handle_t>(
        "close_handle", std::make_unique<ArgumentAccess<zx_handle_t>>(close_handle));
  }

  {
    Syscall* zx_cprng_draw = Add("zx_cprng_draw", SyscallReturnType::kVoid);
    // Arguments
    auto buffer = zx_cprng_draw->PointerArgument<uint8_t>(SyscallType::kUint8Hexa);
    auto buffer_size = zx_cprng_draw->Argument<size_t>(SyscallType::kSize);
    // Outputs
    zx_cprng_draw->OutputBuffer<uint8_t, uint8_t, size_t>(
        ZX_OK, "buffer", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
  }

  {
    Syscall* zx_cprng_add_entropy = Add("zx_cprng_add_entropy", SyscallReturnType::kStatus);
    // Arguments
    auto buffer = zx_cprng_add_entropy->PointerArgument<uint8_t>(SyscallType::kUint8Hexa);
    auto buffer_size = zx_cprng_add_entropy->Argument<size_t>(SyscallType::kSize);
    // Inputs
    zx_cprng_add_entropy->InputBuffer<uint8_t, uint8_t, size_t>(
        "buffer", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
  }

  {
    Syscall* zx_fifo_create = Add("zx_fifo_create", SyscallReturnType::kStatus);
    // Arguments
    auto elem_count = zx_fifo_create->Argument<size_t>(SyscallType::kSize);
    auto elem_size = zx_fifo_create->Argument<size_t>(SyscallType::kSize);
    auto options = zx_fifo_create->Argument<uint32_t>(SyscallType::kUint32);
    auto out0 = zx_fifo_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto out1 = zx_fifo_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_fifo_create->Input<size_t>("elem_count",
                                  std::make_unique<ArgumentAccess<size_t>>(elem_count));
    zx_fifo_create->Input<size_t>("elem_size", std::make_unique<ArgumentAccess<size_t>>(elem_size));
    zx_fifo_create->Input<uint32_t>("options", std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_fifo_create->Output<zx_handle_t>(ZX_OK, "out0",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(out0));
    zx_fifo_create->Output<zx_handle_t>(ZX_OK, "out1",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(out1));

    zx_fifo_create->set_compute_statistics([](const OutputEvent* event) {
      auto out0 =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out0", /*invoked=*/false));
      FX_DCHECK(out0 != nullptr);
      out0->AddCreationEvent(event);
      auto out1 =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out1", /*invoked=*/false));
      FX_DCHECK(out1 != nullptr);
      out1->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_fifo_read = Add("zx_fifo_read", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_fifo_read->Argument<zx_handle_t>(SyscallType::kHandle);
    auto elem_size = zx_fifo_read->Argument<size_t>(SyscallType::kSize);
    auto data = zx_fifo_read->PointerArgument<uint8_t>(SyscallType::kUint8Hexa);
    auto count = zx_fifo_read->Argument<size_t>(SyscallType::kSize);
    auto actual_count = zx_fifo_read->PointerArgument<size_t>(SyscallType::kSize);
    // Inputs
    zx_fifo_read->Input<zx_handle_t>("handle",
                                     std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_fifo_read->Input<size_t>("elem_size", std::make_unique<ArgumentAccess<size_t>>(elem_size));
    zx_fifo_read->Input<size_t>("count", std::make_unique<ArgumentAccess<size_t>>(count));
    // Outputs
    zx_fifo_read->OutputActualAndRequested<size_t>(
        ZX_OK, "actual", std::make_unique<ArgumentAccess<size_t>>(actual_count),
        std::make_unique<ArgumentAccess<size_t>>(count));
    zx_fifo_read->OutputBuffer<uint8_t, uint8_t, size_t>(
        ZX_OK, "data", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(data),
        std::make_unique<ArgumentAccess<size_t>>(elem_size),
        std::make_unique<ArgumentAccess<size_t>>(actual_count));
  }

  {
    Syscall* zx_fifo_write = Add("zx_fifo_write", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_fifo_write->Argument<zx_handle_t>(SyscallType::kHandle);
    auto elem_size = zx_fifo_write->Argument<size_t>(SyscallType::kSize);
    auto data = zx_fifo_write->PointerArgument<uint8_t>(SyscallType::kUint8Hexa);
    auto count = zx_fifo_write->Argument<size_t>(SyscallType::kSize);
    auto actual_count = zx_fifo_write->PointerArgument<size_t>(SyscallType::kSize);
    // Inputs
    zx_fifo_write->Input<zx_handle_t>("handle",
                                      std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_fifo_write->Input<size_t>("elem_size", std::make_unique<ArgumentAccess<size_t>>(elem_size));
    zx_fifo_write->Input<size_t>("count", std::make_unique<ArgumentAccess<size_t>>(count));
    zx_fifo_write->InputBuffer<uint8_t, uint8_t, size_t>(
        "data", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(data),
        std::make_unique<ArgumentAccess<size_t>>(elem_size),
        std::make_unique<ArgumentAccess<size_t>>(count));
    // Outputs
    zx_fifo_write->OutputActualAndRequested<size_t>(
        ZX_OK, "actual", std::make_unique<ArgumentAccess<size_t>>(actual_count),
        std::make_unique<ArgumentAccess<size_t>>(count));
  }

  {
    Syscall* zx_profile_create = Add("zx_profile_create", SyscallReturnType::kStatus);
    // Arguments
    auto root_job = zx_profile_create->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_profile_create->Argument<uint32_t>(SyscallType::kUint32);
    auto profile = zx_profile_create->PointerArgument<zx_profile_info_t>(SyscallType::kStruct);
    auto out = zx_profile_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_profile_create->Input<zx_handle_t>("root_job",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(root_job));
    zx_profile_create->Input<uint32_t>("options",
                                       std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_profile_create->InputObject<zx_profile_info_t>(
        "info", std::make_unique<ArgumentAccess<zx_profile_info_t>>(profile),
        ZxProfileInfo::GetClass());
    // Outputs
    zx_profile_create->Output<zx_handle_t>(ZX_OK, "out",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_profile_create->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_debuglog_create = Add("zx_debuglog_create", SyscallReturnType::kStatus);
    // Arguments
    auto resource = zx_debuglog_create->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_debuglog_create->Argument<uint32_t>(SyscallType::kUint32);
    auto out = zx_debuglog_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_debuglog_create->Input<zx_handle_t>("resource",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(resource));
    zx_debuglog_create->Input<uint32_t>("options",
                                        std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_debuglog_create->Output<zx_handle_t>(ZX_OK, "out",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_debuglog_create->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_debuglog_write = Add("zx_debuglog_write", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_debuglog_write->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_debuglog_write->Argument<uint32_t>(SyscallType::kUint32);
    auto buffer = zx_debuglog_write->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto buffer_size = zx_debuglog_write->Argument<size_t>(SyscallType::kSize);
    // Inputs
    zx_debuglog_write->Input<zx_handle_t>("handle",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_debuglog_write->Input<uint32_t>("options",
                                       std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_debuglog_write->InputBuffer<uint8_t, uint8_t, size_t>(
        "buffer", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
  }

  {
    Syscall* zx_debuglog_read = Add("zx_debuglog_read", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_debuglog_read->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_debuglog_read->Argument<uint32_t>(SyscallType::kUint32);
    auto buffer = zx_debuglog_read->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto buffer_size = zx_debuglog_read->Argument<size_t>(SyscallType::kSize);
    // Inputs
    zx_debuglog_read->Input<zx_handle_t>("handle",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_debuglog_read->Input<uint32_t>("options",
                                      std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_debuglog_read->OutputBuffer<uint8_t, uint8_t, size_t>(
        ZX_OK, "buffer", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(buffer),
        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
  }

  {
    Syscall* zx_ktrace_read = Add("zx_ktrace_read", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_ktrace_read->Argument<zx_handle_t>(SyscallType::kHandle);
    auto data = zx_ktrace_read->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto offset = zx_ktrace_read->Argument<uint32_t>(SyscallType::kUint32);
    auto data_size = zx_ktrace_read->Argument<size_t>(SyscallType::kSize);
    auto actual = zx_ktrace_read->PointerArgument<size_t>(SyscallType::kSize);
    // Inputs
    zx_ktrace_read->Input<zx_handle_t>("handle",
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_ktrace_read->Input<uint32_t>("offset", std::make_unique<ArgumentAccess<uint32_t>>(offset));
    // Outputs
    zx_ktrace_read->OutputActualAndRequested<size_t>(
        ZX_OK, "actual", std::make_unique<ArgumentAccess<size_t>>(actual),
        std::make_unique<ArgumentAccess<size_t>>(data_size));
    zx_ktrace_read->OutputBuffer<uint8_t, uint8_t, size_t>(
        ZX_OK, "data", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(data),
        std::make_unique<ArgumentAccess<size_t>>(actual));
  }

  {
    Syscall* zx_ktrace_control = Add("zx_ktrace_control", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_ktrace_control->Argument<zx_handle_t>(SyscallType::kHandle);
    auto action = zx_ktrace_control->Argument<uint32_t>(SyscallType::kKtraceControlAction);
    auto options = zx_ktrace_control->Argument<uint32_t>(SyscallType::kUint32);
    auto ptr = zx_ktrace_control->PointerArgument<char>(SyscallType::kChar);
    // Conditional field Ids
    const uint8_t kZxKtraceControl_KTRACE_ACTION_NEW_PROBE = 1;
    // Inputs
    zx_ktrace_control->Input<zx_handle_t>("handle",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_ktrace_control->Input<uint32_t>("action",
                                       std::make_unique<ArgumentAccess<uint32_t>>(action));
    zx_ktrace_control->Input<uint32_t>("options",
                                       std::make_unique<ArgumentAccess<uint32_t>>(options));
    constexpr uint32_t KTRACE_ACTION_NEW_PROBE = 4;
    zx_ktrace_control
        ->InputFixedSizeString("ptr", std::make_unique<ArgumentAccess<char>>(ptr), ZX_MAX_NAME_LEN)
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(action),
                                   KTRACE_ACTION_NEW_PROBE)
        ->SetId(kZxKtraceControl_KTRACE_ACTION_NEW_PROBE);
  }

  {
    Syscall* zx_ktrace_write = Add("zx_ktrace_write", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_ktrace_write->Argument<zx_handle_t>(SyscallType::kHandle);
    auto id = zx_ktrace_write->Argument<uint32_t>(SyscallType::kUint32);
    auto arg0 = zx_ktrace_write->Argument<uint32_t>(SyscallType::kUint32);
    auto arg1 = zx_ktrace_write->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_ktrace_write->Input<zx_handle_t>("handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_ktrace_write->Input<uint32_t>("id", std::make_unique<ArgumentAccess<uint32_t>>(id));
    zx_ktrace_write->Input<uint32_t>("arg0", std::make_unique<ArgumentAccess<uint32_t>>(arg0));
    zx_ktrace_write->Input<uint32_t>("arg1", std::make_unique<ArgumentAccess<uint32_t>>(arg1));
  }

  {
    Syscall* zx_mtrace_control = Add("zx_mtrace_control", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_mtrace_control->Argument<zx_handle_t>(SyscallType::kHandle);
    auto kind = zx_mtrace_control->Argument<uint32_t>(SyscallType::kUint32);
    auto action = zx_mtrace_control->Argument<uint32_t>(SyscallType::kUint32);
    auto options = zx_mtrace_control->Argument<uint32_t>(SyscallType::kUint32);
    auto ptr = zx_mtrace_control->PointerArgument<uint8_t>(SyscallType::kUint8);
    auto ptr_size = zx_mtrace_control->Argument<size_t>(SyscallType::kSize);
    // Inputs
    zx_mtrace_control->Input<zx_handle_t>("handle",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_mtrace_control->Input<uint32_t>("kind", std::make_unique<ArgumentAccess<uint32_t>>(kind));
    zx_mtrace_control->Input<uint32_t>("action",
                                       std::make_unique<ArgumentAccess<uint32_t>>(action));
    zx_mtrace_control->Input<uint32_t>("options",
                                       std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_mtrace_control->InputBuffer<uint8_t, uint8_t, size_t>(
        "ptr", SyscallType::kUint8Hexa, std::make_unique<ArgumentAccess<uint8_t>>(ptr),
        std::make_unique<ArgumentAccess<size_t>>(ptr_size));
  }

  {
    Syscall* zx_debug_read = Add("zx_debug_read", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_debug_read->Argument<zx_handle_t>(SyscallType::kHandle);
    auto buffer = zx_debug_read->PointerArgument<char>(SyscallType::kChar);
    auto buffer_size = zx_debug_read->Argument<size_t>(SyscallType::kSize);
    auto actual = zx_debug_read->PointerArgument<size_t>(SyscallType::kSize);
    // Inputs
    zx_debug_read->Input<zx_handle_t>("handle",
                                      std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    // Outputs
    zx_debug_read->OutputActualAndRequested<size_t>(
        ZX_OK, "actual", std::make_unique<ArgumentAccess<size_t>>(actual),
        std::make_unique<ArgumentAccess<size_t>>(buffer_size));
    zx_debug_read->OutputString<char>(ZX_OK, "buffer",
                                      std::make_unique<ArgumentAccess<char>>(buffer),
                                      std::make_unique<ArgumentAccess<size_t>>(actual));
  }

  {
    Syscall* zx_debug_write = Add("zx_debug_write", SyscallReturnType::kStatus);
    // Arguments
    auto buffer = zx_debug_write->PointerArgument<char>(SyscallType::kChar);
    auto buffer_size = zx_debug_write->Argument<size_t>(SyscallType::kSize);
    // Inputs
    zx_debug_write->InputString<char>("buffer", std::make_unique<ArgumentAccess<char>>(buffer),
                                      std::make_unique<ArgumentAccess<size_t>>(buffer_size));
  }

  {
    Syscall* zx_debug_send_command = Add("zx_debug_send_command", SyscallReturnType::kStatus);
    // Arguments
    auto resource = zx_debug_send_command->Argument<zx_handle_t>(SyscallType::kHandle);
    auto buffer = zx_debug_send_command->PointerArgument<char>(SyscallType::kChar);
    auto buffer_size = zx_debug_send_command->Argument<size_t>(SyscallType::kSize);
    // Inputs
    zx_debug_send_command->Input<zx_handle_t>(
        "resource", std::make_unique<ArgumentAccess<zx_handle_t>>(resource));
    zx_debug_send_command->InputString<char>("buffer",
                                             std::make_unique<ArgumentAccess<char>>(buffer),
                                             std::make_unique<ArgumentAccess<size_t>>(buffer_size));
  }

  {
    Syscall* zx_interrupt_create = Add("zx_interrupt_create", SyscallReturnType::kStatus);
    // Arguments
    auto src_obj = zx_interrupt_create->Argument<zx_handle_t>(SyscallType::kHandle);
    auto src_num = zx_interrupt_create->Argument<uint32_t>(SyscallType::kUint32);
    auto options = zx_interrupt_create->Argument<uint32_t>(SyscallType::kInterruptFlags);
    auto out_handle = zx_interrupt_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_interrupt_create->Input<zx_handle_t>("src_obj",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(src_obj));
    zx_interrupt_create->Input<uint32_t>("src_num",
                                         std::make_unique<ArgumentAccess<uint32_t>>(src_num));
    zx_interrupt_create->Input<uint32_t>("options",
                                         std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_interrupt_create->Output<zx_handle_t>(
        ZX_OK, "out_handle", std::make_unique<ArgumentAccess<zx_handle_t>>(out_handle));

    zx_interrupt_create->set_compute_statistics([](const OutputEvent* event) {
      auto out_handle = event->GetHandleInfo(
          event->syscall()->SearchInlineMember("out_handle", /*invoked=*/false));
      FX_DCHECK(out_handle != nullptr);
      out_handle->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_interrupt_bind = Add("zx_interrupt_bind", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_interrupt_bind->Argument<zx_handle_t>(SyscallType::kHandle);
    auto port_handle = zx_interrupt_bind->Argument<zx_handle_t>(SyscallType::kHandle);
    auto key = zx_interrupt_bind->Argument<uint64_t>(SyscallType::kUint64);
    auto options = zx_interrupt_bind->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_interrupt_bind->Input<zx_handle_t>("handle",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_interrupt_bind->Input<zx_handle_t>(
        "port_handle", std::make_unique<ArgumentAccess<zx_handle_t>>(port_handle));
    zx_interrupt_bind->Input<uint64_t>("key", std::make_unique<ArgumentAccess<uint64_t>>(key));
    zx_interrupt_bind->Input<uint32_t>("options",
                                       std::make_unique<ArgumentAccess<uint32_t>>(options));
  }

  {
    Syscall* zx_interrupt_wait = Add("zx_interrupt_wait", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_interrupt_wait->Argument<zx_handle_t>(SyscallType::kHandle);
    auto out_timestamp = zx_interrupt_wait->PointerArgument<zx_time_t>(SyscallType::kMonotonicTime);
    // Inputs
    zx_interrupt_wait->Input<zx_handle_t>("handle",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    // Outputs
    zx_interrupt_wait->Output<zx_time_t>(
        ZX_OK, "out_timestamp", std::make_unique<ArgumentAccess<zx_time_t>>(out_timestamp));
  }

  {
    Syscall* zx_interrupt_destroy = Add("zx_interrupt_destroy", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_interrupt_destroy->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_interrupt_destroy->Input<zx_handle_t>("handle",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
  }

  {
    Syscall* zx_interrupt_ack = Add("zx_interrupt_ack", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_interrupt_ack->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_interrupt_ack->Input<zx_handle_t>("handle",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
  }

  {
    Syscall* zx_interrupt_trigger = Add("zx_interrupt_trigger", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_interrupt_trigger->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_interrupt_trigger->Argument<uint32_t>(SyscallType::kUint32);
    auto timestamp = zx_interrupt_trigger->Argument<zx_time_t>(SyscallType::kMonotonicTime);
    // Inputs
    zx_interrupt_trigger->Input<zx_handle_t>("handle",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_interrupt_trigger->Input<uint32_t>("options",
                                          std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_interrupt_trigger->Input<zx_time_t>("timestamp",
                                           std::make_unique<ArgumentAccess<zx_time_t>>(timestamp));
  }

  {
    Syscall* zx_interrupt_bind_vcpu = Add("zx_interrupt_bind_vcpu", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_interrupt_bind_vcpu->Argument<zx_handle_t>(SyscallType::kHandle);
    auto vcpu = zx_interrupt_bind_vcpu->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_interrupt_bind_vcpu->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_interrupt_bind_vcpu->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_interrupt_bind_vcpu->Input<zx_handle_t>("vcpu",
                                               std::make_unique<ArgumentAccess<zx_handle_t>>(vcpu));
    zx_interrupt_bind_vcpu->Input<uint32_t>("options",
                                            std::make_unique<ArgumentAccess<uint32_t>>(options));
  }

  {
    Syscall* zx_ioports_request = Add("zx_ioports_request", SyscallReturnType::kStatus);
    // Arguments
    auto resource = zx_ioports_request->Argument<zx_handle_t>(SyscallType::kHandle);
    auto io_addr = zx_ioports_request->Argument<uint16_t>(SyscallType::kUint16Hexa);
    auto len = zx_ioports_request->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_ioports_request->Input<zx_handle_t>("resource",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(resource));
    zx_ioports_request->Input<uint16_t>("io_addr",
                                        std::make_unique<ArgumentAccess<uint16_t>>(io_addr));
    zx_ioports_request->Input<uint32_t>("len", std::make_unique<ArgumentAccess<uint32_t>>(len));
  }

  {
    Syscall* zx_ioports_release = Add("zx_ioports_release", SyscallReturnType::kStatus);
    // Arguments
    auto resource = zx_ioports_release->Argument<zx_handle_t>(SyscallType::kHandle);
    auto io_addr = zx_ioports_release->Argument<uint16_t>(SyscallType::kUint16Hexa);
    auto len = zx_ioports_release->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_ioports_release->Input<zx_handle_t>("resource",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(resource));
    zx_ioports_release->Input<uint16_t>("io_addr",
                                        std::make_unique<ArgumentAccess<uint16_t>>(io_addr));
    zx_ioports_release->Input<uint32_t>("len", std::make_unique<ArgumentAccess<uint32_t>>(len));
  }

  {
    Syscall* zx_iommu_create = Add("zx_iommu_create", SyscallReturnType::kStatus);
    // Arguments
    auto resource = zx_iommu_create->Argument<zx_handle_t>(SyscallType::kHandle);
    auto type = zx_iommu_create->Argument<uint32_t>(SyscallType::kIommuType);
    auto desc = zx_iommu_create->PointerArgument<uint8_t>(SyscallType::kUint8);
    zx_iommu_create->Argument<size_t>(SyscallType::kSize);
    auto out = zx_iommu_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Conditional field Ids
    const uint8_t kZxIommuCreate_ZX_IOMMU_TYPE_INTEL = 1;
    // Inputs
    zx_iommu_create->Input<zx_handle_t>("resource",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(resource));
    zx_iommu_create->Input<uint32_t>("type", std::make_unique<ArgumentAccess<uint32_t>>(type));
    zx_iommu_create
        ->InputObject<zx_iommu_desc_intel_t>(
            "desc", std::make_unique<ArgumentAccess<uint8_t>>(desc), ZxIommuDescIntel::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(type),
                                   ZX_IOMMU_TYPE_INTEL)
        ->SetId(kZxIommuCreate_ZX_IOMMU_TYPE_INTEL);
    // Outputs
    zx_iommu_create->Output<zx_handle_t>(ZX_OK, "out",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_iommu_create->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_bti_create = Add("zx_bti_create", SyscallReturnType::kStatus);
    // Arguments
    auto iommu = zx_bti_create->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_bti_create->Argument<uint32_t>(SyscallType::kUint32);
    auto bti_id = zx_bti_create->Argument<uint64_t>(SyscallType::kUint64);
    auto out = zx_bti_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_bti_create->Input<zx_handle_t>("iommu",
                                      std::make_unique<ArgumentAccess<zx_handle_t>>(iommu));
    zx_bti_create->Input<uint32_t>("options", std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_bti_create->Input<uint64_t>("bti_id", std::make_unique<ArgumentAccess<uint64_t>>(bti_id));
    // Outputs
    zx_bti_create->Output<zx_handle_t>(ZX_OK, "out",
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_bti_create->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_bti_pin = Add("zx_bti_pin", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_bti_pin->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_bti_pin->Argument<uint32_t>(SyscallType::kBtiPerm);
    auto vmo = zx_bti_pin->Argument<zx_handle_t>(SyscallType::kHandle);
    auto offset = zx_bti_pin->Argument<uint64_t>(SyscallType::kUint64);
    auto size = zx_bti_pin->Argument<uint64_t>(SyscallType::kUint64);
    auto addrs = zx_bti_pin->PointerArgument<zx_paddr_t>(SyscallType::kPaddr);
    auto addrs_count = zx_bti_pin->Argument<size_t>(SyscallType::kSize);
    auto pmt = zx_bti_pin->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_bti_pin->Input<zx_handle_t>("handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_bti_pin->Input<uint32_t>("options", std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_bti_pin->Input<zx_handle_t>("vmo", std::make_unique<ArgumentAccess<zx_handle_t>>(vmo));
    zx_bti_pin->Input<uint64_t>("offset", std::make_unique<ArgumentAccess<uint64_t>>(offset));
    zx_bti_pin->Input<uint64_t>("size", std::make_unique<ArgumentAccess<uint64_t>>(size));
    // Outputs
    zx_bti_pin->OutputBuffer<zx_paddr_t, zx_paddr_t, size_t>(
        ZX_OK, "addrs", SyscallType::kPaddr, std::make_unique<ArgumentAccess<zx_paddr_t>>(addrs),
        std::make_unique<ArgumentAccess<size_t>>(addrs_count));
    zx_bti_pin->Output<zx_handle_t>(ZX_OK, "pmt",
                                    std::make_unique<ArgumentAccess<zx_handle_t>>(pmt));
  }

  {
    Syscall* zx_bti_release_quarantine =
        Add("zx_bti_release_quarantine", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_bti_release_quarantine->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_bti_release_quarantine->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
  }

  {
    Syscall* zx_pmt_unpin = Add("zx_pmt_unpin", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pmt_unpin->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_pmt_unpin->Input<zx_handle_t>("handle",
                                     std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
  }

  {
    Syscall* zx_framebuffer_get_info = Add("zx_framebuffer_get_info", SyscallReturnType::kStatus);
    // Arguments
    auto resource = zx_framebuffer_get_info->Argument<zx_handle_t>(SyscallType::kHandle);
    auto format = zx_framebuffer_get_info->PointerArgument<uint32_t>(SyscallType::kUint32);
    auto width = zx_framebuffer_get_info->PointerArgument<uint32_t>(SyscallType::kUint32);
    auto height = zx_framebuffer_get_info->PointerArgument<uint32_t>(SyscallType::kUint32);
    auto stride = zx_framebuffer_get_info->PointerArgument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_framebuffer_get_info->Input<zx_handle_t>(
        "resource", std::make_unique<ArgumentAccess<zx_handle_t>>(resource));
    // Outputs
    zx_framebuffer_get_info->Output<uint32_t>(ZX_OK, "format",
                                              std::make_unique<ArgumentAccess<uint32_t>>(format));
    zx_framebuffer_get_info->Output<uint32_t>(ZX_OK, "width",
                                              std::make_unique<ArgumentAccess<uint32_t>>(width));
    zx_framebuffer_get_info->Output<uint32_t>(ZX_OK, "height",
                                              std::make_unique<ArgumentAccess<uint32_t>>(height));
    zx_framebuffer_get_info->Output<uint32_t>(ZX_OK, "stride",
                                              std::make_unique<ArgumentAccess<uint32_t>>(stride));
  }

  {
    Syscall* zx_framebuffer_set_range = Add("zx_framebuffer_set_range", SyscallReturnType::kStatus);
    // Arguments
    auto resource = zx_framebuffer_set_range->Argument<zx_handle_t>(SyscallType::kHandle);
    auto vmo = zx_framebuffer_set_range->Argument<zx_handle_t>(SyscallType::kHandle);
    auto len = zx_framebuffer_set_range->Argument<uint32_t>(SyscallType::kUint32);
    auto format = zx_framebuffer_set_range->Argument<uint32_t>(SyscallType::kUint32);
    auto width = zx_framebuffer_set_range->Argument<uint32_t>(SyscallType::kUint32);
    auto height = zx_framebuffer_set_range->Argument<uint32_t>(SyscallType::kUint32);
    auto stride = zx_framebuffer_set_range->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_framebuffer_set_range->Input<zx_handle_t>(
        "resource", std::make_unique<ArgumentAccess<zx_handle_t>>(resource));
    zx_framebuffer_set_range->Input<zx_handle_t>(
        "vmo", std::make_unique<ArgumentAccess<zx_handle_t>>(vmo));
    zx_framebuffer_set_range->Input<uint32_t>("len",
                                              std::make_unique<ArgumentAccess<uint32_t>>(len));
    zx_framebuffer_set_range->Input<uint32_t>("format",
                                              std::make_unique<ArgumentAccess<uint32_t>>(format));
    zx_framebuffer_set_range->Input<uint32_t>("width",
                                              std::make_unique<ArgumentAccess<uint32_t>>(width));
    zx_framebuffer_set_range->Input<uint32_t>("height",
                                              std::make_unique<ArgumentAccess<uint32_t>>(height));
    zx_framebuffer_set_range->Input<uint32_t>("stride",
                                              std::make_unique<ArgumentAccess<uint32_t>>(stride));
  }

  {
    Syscall* zx_pci_get_nth_device = Add("zx_pci_get_nth_device", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pci_get_nth_device->Argument<zx_handle_t>(SyscallType::kHandle);
    auto index = zx_pci_get_nth_device->Argument<uint32_t>(SyscallType::kUint32);
    auto out_info =
        zx_pci_get_nth_device->PointerArgument<zx_pcie_device_info_t>(SyscallType::kStruct);
    auto out_handle = zx_pci_get_nth_device->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_pci_get_nth_device->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_pci_get_nth_device->Input<uint32_t>("index",
                                           std::make_unique<ArgumentAccess<uint32_t>>(index));
    // Outputs
    zx_pci_get_nth_device->InputObject<zx_pcie_device_info_t>(
        "out_info", std::make_unique<ArgumentAccess<zx_pcie_device_info_t>>(out_info),
        ZxPcieDeviceInfo::GetClass());
    zx_pci_get_nth_device->Output<zx_handle_t>(
        ZX_OK, "out_handle", std::make_unique<ArgumentAccess<zx_handle_t>>(out_handle));
  }

  {
    Syscall* zx_pci_enable_bus_master = Add("zx_pci_enable_bus_master", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pci_enable_bus_master->Argument<zx_handle_t>(SyscallType::kHandle);
    auto enable = zx_pci_enable_bus_master->Argument<bool>(SyscallType::kBool);
    // Inputs
    zx_pci_enable_bus_master->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_pci_enable_bus_master->Input<bool>("enable", std::make_unique<ArgumentAccess<bool>>(enable));
  }

  {
    Syscall* zx_pci_reset_device = Add("zx_pci_reset_device", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pci_reset_device->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_pci_reset_device->Input<zx_handle_t>("handle",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
  }

  {
    Syscall* zx_pci_config_read = Add("zx_pci_config_read", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pci_config_read->Argument<zx_handle_t>(SyscallType::kHandle);
    auto offset = zx_pci_config_read->Argument<uint16_t>(SyscallType::kUint16);
    auto width = zx_pci_config_read->Argument<size_t>(SyscallType::kSize);
    auto out_val = zx_pci_config_read->PointerArgument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_pci_config_read->Input<zx_handle_t>("handle",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_pci_config_read->Input<uint16_t>("offset",
                                        std::make_unique<ArgumentAccess<uint16_t>>(offset));
    zx_pci_config_read->Input<size_t>("width", std::make_unique<ArgumentAccess<size_t>>(width));
    // Outputs
    zx_pci_config_read->Output<uint32_t>(ZX_OK, "out_val",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(out_val));
  }

  {
    Syscall* zx_pci_config_write = Add("zx_pci_config_write", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pci_config_write->Argument<zx_handle_t>(SyscallType::kHandle);
    auto offset = zx_pci_config_write->Argument<uint16_t>(SyscallType::kUint16);
    auto width = zx_pci_config_write->Argument<size_t>(SyscallType::kSize);
    auto val = zx_pci_config_write->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_pci_config_write->Input<zx_handle_t>("handle",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_pci_config_write->Input<uint16_t>("offset",
                                         std::make_unique<ArgumentAccess<uint16_t>>(offset));
    zx_pci_config_write->Input<size_t>("width", std::make_unique<ArgumentAccess<size_t>>(width));
    zx_pci_config_write->Input<uint32_t>("val", std::make_unique<ArgumentAccess<uint32_t>>(val));
  }

  {
    Syscall* zx_pci_cfg_pio_rw = Add("zx_pci_cfg_pio_rw", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pci_cfg_pio_rw->Argument<zx_handle_t>(SyscallType::kHandle);
    auto bus = zx_pci_cfg_pio_rw->Argument<uint8_t>(SyscallType::kUint8);
    auto dev = zx_pci_cfg_pio_rw->Argument<uint8_t>(SyscallType::kUint8);
    auto func = zx_pci_cfg_pio_rw->Argument<uint8_t>(SyscallType::kUint8);
    auto offset = zx_pci_cfg_pio_rw->Argument<uint8_t>(SyscallType::kUint8);
    auto val = zx_pci_cfg_pio_rw->PointerArgument<uint32_t>(SyscallType::kUint32);
    auto width = zx_pci_cfg_pio_rw->Argument<size_t>(SyscallType::kSize);
    auto write = zx_pci_cfg_pio_rw->Argument<bool>(SyscallType::kBool);
    // Conditional field Ids
    const uint8_t kZxPciCfgPioRw_true = 1;
    const uint8_t kZxPciCfgPioRw_false = 2;
    // Inputs
    zx_pci_cfg_pio_rw->Input<zx_handle_t>("handle",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_pci_cfg_pio_rw->Input<uint8_t>("bus", std::make_unique<ArgumentAccess<uint8_t>>(bus));
    zx_pci_cfg_pio_rw->Input<uint8_t>("dev", std::make_unique<ArgumentAccess<uint8_t>>(dev));
    zx_pci_cfg_pio_rw->Input<uint8_t>("func", std::make_unique<ArgumentAccess<uint8_t>>(func));
    zx_pci_cfg_pio_rw->Input<uint8_t>("offset", std::make_unique<ArgumentAccess<uint8_t>>(offset));
    zx_pci_cfg_pio_rw->Input<size_t>("width", std::make_unique<ArgumentAccess<size_t>>(width));
    zx_pci_cfg_pio_rw->Input<uint32_t>("val", std::make_unique<ArgumentAccess<uint32_t>>(val))
        ->DisplayIfEqual<bool>(std::make_unique<ArgumentAccess<bool>>(write), true)
        ->SetId(kZxPciCfgPioRw_true);
    zx_pci_cfg_pio_rw->Input<bool>("write", std::make_unique<ArgumentAccess<bool>>(write));
    // Outputs
    zx_pci_cfg_pio_rw
        ->Output<uint32_t>(ZX_OK, "val", std::make_unique<ArgumentAccess<uint32_t>>(val))
        ->DisplayIfEqual<bool>(std::make_unique<ArgumentAccess<bool>>(write), false)
        ->SetId(kZxPciCfgPioRw_false);
  }

  {
    Syscall* zx_pci_get_bar = Add("zx_pci_get_bar", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pci_get_bar->Argument<zx_handle_t>(SyscallType::kHandle);
    auto bar_num = zx_pci_get_bar->Argument<uint32_t>(SyscallType::kUint32);
    auto out_bar = zx_pci_get_bar->PointerArgument<zx_pci_bar_t>(SyscallType::kStruct);
    auto out_handle = zx_pci_get_bar->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_pci_get_bar->Input<zx_handle_t>("handle",
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_pci_get_bar->Input<uint32_t>("bar_num", std::make_unique<ArgumentAccess<uint32_t>>(bar_num));
    // Outputs
    zx_pci_get_bar->OutputObject<zx_pci_bar_t>(
        ZX_OK, "out_bar", std::make_unique<ArgumentAccess<zx_pci_bar_t>>(out_bar),
        ZxPciBar::GetClass());
    zx_pci_get_bar->Output<zx_handle_t>(ZX_OK, "out_handle",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(out_handle));
  }

  {
    Syscall* zx_pci_map_interrupt = Add("zx_pci_map_interrupt", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pci_map_interrupt->Argument<zx_handle_t>(SyscallType::kHandle);
    auto which_irq = zx_pci_map_interrupt->Argument<int32_t>(SyscallType::kInt32);
    auto out_handle = zx_pci_map_interrupt->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_pci_map_interrupt->Input<zx_handle_t>("handle",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_pci_map_interrupt->Input<int32_t>("which_irq",
                                         std::make_unique<ArgumentAccess<int32_t>>(which_irq));
    // Outputs
    zx_pci_map_interrupt->Output<zx_handle_t>(
        ZX_OK, "out_handle", std::make_unique<ArgumentAccess<zx_handle_t>>(out_handle));
  }

  {
    Syscall* zx_pci_query_irq_mode = Add("zx_pci_query_irq_mode", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pci_query_irq_mode->Argument<zx_handle_t>(SyscallType::kHandle);
    auto mode = zx_pci_query_irq_mode->Argument<uint32_t>(SyscallType::kUint32);
    auto out_max_irqs = zx_pci_query_irq_mode->PointerArgument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_pci_query_irq_mode->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_pci_query_irq_mode->Input<uint32_t>("mode",
                                           std::make_unique<ArgumentAccess<uint32_t>>(mode));
    // Outputs
    zx_pci_query_irq_mode->Output<uint32_t>(
        ZX_OK, "out_max_irqs", std::make_unique<ArgumentAccess<uint32_t>>(out_max_irqs));
  }

  {
    Syscall* zx_pci_set_irq_mode = Add("zx_pci_set_irq_mode", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pci_set_irq_mode->Argument<zx_handle_t>(SyscallType::kHandle);
    auto mode = zx_pci_set_irq_mode->Argument<uint32_t>(SyscallType::kUint32);
    auto requested_irq_count = zx_pci_set_irq_mode->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_pci_set_irq_mode->Input<zx_handle_t>("handle",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_pci_set_irq_mode->Input<uint32_t>("mode", std::make_unique<ArgumentAccess<uint32_t>>(mode));
    zx_pci_set_irq_mode->Input<uint32_t>(
        "requested_irq_count", std::make_unique<ArgumentAccess<uint32_t>>(requested_irq_count));
  }

  {
    Syscall* zx_pci_init = Add("zx_pci_init", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pci_init->Argument<zx_handle_t>(SyscallType::kHandle);
    auto init_buf = zx_pci_init->PointerArgument<zx_pci_init_arg_t>(SyscallType::kStruct);
    auto len = zx_pci_init->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_pci_init->Input<zx_handle_t>("handle",
                                    std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_pci_init->InputObject<zx_pci_init_arg_t, uint32_t>(
        "init_buf", std::make_unique<ArgumentAccess<zx_pci_init_arg_t>>(init_buf),
        std::make_unique<ArgumentAccess<uint32_t>>(len), ZxPciInitArg::GetClass());
    zx_pci_init->Input<uint32_t>("len", std::make_unique<ArgumentAccess<uint32_t>>(len));
  }

  {
    Syscall* zx_pci_add_subtract_io_range =
        Add("zx_pci_add_subtract_io_range", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pci_add_subtract_io_range->Argument<zx_handle_t>(SyscallType::kHandle);
    auto mmio = zx_pci_add_subtract_io_range->Argument<bool>(SyscallType::kBool);
    auto base = zx_pci_add_subtract_io_range->Argument<uint64_t>(SyscallType::kUint64);
    auto len = zx_pci_add_subtract_io_range->Argument<uint64_t>(SyscallType::kUint64);
    auto add = zx_pci_add_subtract_io_range->Argument<bool>(SyscallType::kBool);
    // Inputs
    zx_pci_add_subtract_io_range->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_pci_add_subtract_io_range->Input<bool>("mmio", std::make_unique<ArgumentAccess<bool>>(mmio));
    zx_pci_add_subtract_io_range->Input<uint64_t>("base",
                                                  std::make_unique<ArgumentAccess<uint64_t>>(base));
    zx_pci_add_subtract_io_range->Input<uint64_t>("len",
                                                  std::make_unique<ArgumentAccess<uint64_t>>(len));
    zx_pci_add_subtract_io_range->Input<bool>("add", std::make_unique<ArgumentAccess<bool>>(add));
  }

  {
    Syscall* zx_pc_firmware_tables = Add("zx_pc_firmware_tables", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_pc_firmware_tables->Argument<zx_handle_t>(SyscallType::kHandle);
    auto acpi_rsdp = zx_pc_firmware_tables->PointerArgument<zx_paddr_t>(SyscallType::kPaddr);
    auto smbios = zx_pc_firmware_tables->PointerArgument<zx_paddr_t>(SyscallType::kPaddr);
    // Inputs
    zx_pc_firmware_tables->Input<zx_handle_t>(
        "handle", std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    // Outputs
    zx_pc_firmware_tables->Output<zx_paddr_t>(
        ZX_OK, "acpi_rsdp", std::make_unique<ArgumentAccess<zx_paddr_t>>(acpi_rsdp));
    zx_pc_firmware_tables->Output<zx_paddr_t>(ZX_OK, "smbios",
                                              std::make_unique<ArgumentAccess<zx_paddr_t>>(smbios));
  }

  {
    Syscall* zx_smc_call = Add("zx_smc_call", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_smc_call->Argument<zx_handle_t>(SyscallType::kHandle);
    auto parameters = zx_smc_call->PointerArgument<zx_smc_parameters_t>(SyscallType::kStruct);
    auto out_smc_result = zx_smc_call->PointerArgument<zx_smc_result_t>(SyscallType::kStruct);
    // Inputs
    zx_smc_call->Input<zx_handle_t>("handle",
                                    std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_smc_call->InputObject<zx_smc_parameters_t>(
        "parameters", std::make_unique<ArgumentAccess<zx_smc_parameters_t>>(parameters),
        ZxSmcParameters::GetClass());
    // Outputs
    zx_smc_call->OutputObject<zx_smc_result_t>(
        ZX_OK, "out_smc_result", std::make_unique<ArgumentAccess<zx_smc_result_t>>(out_smc_result),
        ZxSmcResult::GetClass());
  }

  {
    Syscall* zx_resource_create = Add("zx_resource_create", SyscallReturnType::kStatus);
    // Arguments
    auto parent_rsrc = zx_resource_create->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_resource_create->Argument<uint32_t>(SyscallType::kRsrcKind);
    auto base = zx_resource_create->Argument<uint64_t>(SyscallType::kUint64);
    auto size = zx_resource_create->Argument<size_t>(SyscallType::kSize);
    auto name = zx_resource_create->PointerArgument<char>(SyscallType::kChar);
    auto name_size = zx_resource_create->Argument<size_t>(SyscallType::kSize);
    auto resource_out = zx_resource_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_resource_create->Input<zx_handle_t>(
        "parent_rsrc", std::make_unique<ArgumentAccess<zx_handle_t>>(parent_rsrc));
    zx_resource_create->Input<uint32_t>("options",
                                        std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_resource_create->Input<uint64_t>("base", std::make_unique<ArgumentAccess<uint64_t>>(base));
    zx_resource_create->Input<size_t>("size", std::make_unique<ArgumentAccess<size_t>>(size));
    zx_resource_create->InputString<char>("name", std::make_unique<ArgumentAccess<char>>(name),
                                          std::make_unique<ArgumentAccess<size_t>>(name_size));
    // Outputs
    zx_resource_create->Output<zx_handle_t>(
        ZX_OK, "resource_out", std::make_unique<ArgumentAccess<zx_handle_t>>(resource_out));

    zx_resource_create->set_compute_statistics([](const OutputEvent* event) {
      auto resource_out = event->GetHandleInfo(
          event->syscall()->SearchInlineMember("resource_out", /*invoked=*/false));
      FX_DCHECK(resource_out != nullptr);
      resource_out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_guest_create = Add("zx_guest_create", SyscallReturnType::kStatus);
    // Arguments
    auto resource = zx_guest_create->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_guest_create->Argument<uint32_t>(SyscallType::kUint32);
    auto guest_handle = zx_guest_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    auto vmar_handle = zx_guest_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_guest_create->Input<zx_handle_t>("resource",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(resource));
    zx_guest_create->Input<uint32_t>("options",
                                     std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_guest_create->Output<zx_handle_t>(
        ZX_OK, "guest_handle", std::make_unique<ArgumentAccess<zx_handle_t>>(guest_handle));
    zx_guest_create->Output<zx_handle_t>(
        ZX_OK, "vmar_handle", std::make_unique<ArgumentAccess<zx_handle_t>>(vmar_handle));

    zx_guest_create->set_compute_statistics([](const OutputEvent* event) {
      auto guest_handle = event->GetHandleInfo(
          event->syscall()->SearchInlineMember("guest_handle", /*invoked=*/false));
      FX_DCHECK(guest_handle != nullptr);
      guest_handle->AddCreationEvent(event);
      auto vmar_handle = event->GetHandleInfo(
          event->syscall()->SearchInlineMember("vmar_handle", /*invoked=*/false));
      FX_DCHECK(vmar_handle != nullptr);
      vmar_handle->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_guest_set_trap = Add("zx_guest_set_trap", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_guest_set_trap->Argument<zx_handle_t>(SyscallType::kHandle);
    auto kind = zx_guest_set_trap->Argument<uint32_t>(SyscallType::kGuestTrap);
    auto addr = zx_guest_set_trap->Argument<zx_vaddr_t>(SyscallType::kVaddr);
    auto size = zx_guest_set_trap->Argument<size_t>(SyscallType::kSize);
    auto port_handle = zx_guest_set_trap->Argument<zx_handle_t>(SyscallType::kHandle);
    auto key = zx_guest_set_trap->Argument<uint64_t>(SyscallType::kUint64);
    // Inputs
    zx_guest_set_trap->Input<zx_handle_t>("handle",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_guest_set_trap->Input<uint32_t>("kind", std::make_unique<ArgumentAccess<uint32_t>>(kind));
    zx_guest_set_trap->Input<zx_vaddr_t>("addr",
                                         std::make_unique<ArgumentAccess<zx_vaddr_t>>(addr));
    zx_guest_set_trap->Input<size_t>("size", std::make_unique<ArgumentAccess<size_t>>(size));
    zx_guest_set_trap->Input<zx_handle_t>(
        "port_handle", std::make_unique<ArgumentAccess<zx_handle_t>>(port_handle));
    zx_guest_set_trap->Input<uint64_t>("key", std::make_unique<ArgumentAccess<uint64_t>>(key));
  }

  {
    Syscall* zx_vcpu_create = Add("zx_vcpu_create", SyscallReturnType::kStatus);
    // Arguments
    auto guest = zx_vcpu_create->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_vcpu_create->Argument<uint32_t>(SyscallType::kUint32);
    auto entry = zx_vcpu_create->Argument<zx_vaddr_t>(SyscallType::kVaddr);
    auto out = zx_vcpu_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_vcpu_create->Input<zx_handle_t>("guest",
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(guest));
    zx_vcpu_create->Input<uint32_t>("options", std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_vcpu_create->Input<zx_vaddr_t>("entry", std::make_unique<ArgumentAccess<zx_vaddr_t>>(entry));
    // Outputs
    zx_vcpu_create->Output<zx_handle_t>(ZX_OK, "out",
                                        std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_vcpu_create->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_vcpu_resume = Add("zx_vcpu_resume", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vcpu_resume->Argument<zx_handle_t>(SyscallType::kHandle);
    auto packet = zx_vcpu_resume->PointerArgument<zx_port_packet_t>(SyscallType::kStruct);
    // Inputs
    zx_vcpu_resume->Input<zx_handle_t>("handle",
                                       std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    // Outputs
    zx_vcpu_resume->OutputObject<zx_port_packet_t>(
        ZX_OK, "packet", std::make_unique<ArgumentAccess<zx_port_packet_t>>(packet),
        ZxPortPacket::GetClass());
  }

  {
    Syscall* zx_vcpu_interrupt = Add("zx_vcpu_interrupt", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vcpu_interrupt->Argument<zx_handle_t>(SyscallType::kHandle);
    auto vector = zx_vcpu_interrupt->Argument<uint32_t>(SyscallType::kUint32);
    // Inputs
    zx_vcpu_interrupt->Input<zx_handle_t>("handle",
                                          std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vcpu_interrupt->Input<uint32_t>("vector",
                                       std::make_unique<ArgumentAccess<uint32_t>>(vector));
  }

  {
    Syscall* zx_vcpu_read_state = Add("zx_vcpu_read_state", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vcpu_read_state->Argument<zx_handle_t>(SyscallType::kHandle);
    auto kind = zx_vcpu_read_state->Argument<uint32_t>(SyscallType::kVcpu);
    auto buffer = zx_vcpu_read_state->PointerArgument<uint8_t>(SyscallType::kUint8);
    zx_vcpu_read_state->Argument<size_t>(SyscallType::kSize);
    // Conditional field Ids
    const uint8_t kZxVcpuReadState_ZX_VCPU_STATE_Arm64 = 1;
    const uint8_t kZxVcpuReadState_ZX_VCPU_STATE_X64 = 2;
    // Inputs
    zx_vcpu_read_state->Input<zx_handle_t>("handle",
                                           std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vcpu_read_state->Input<uint32_t>("kind", std::make_unique<ArgumentAccess<uint32_t>>(kind));
    // Outputs
    zx_vcpu_read_state
        ->OutputObject<zx_vcpu_state_aarch64_t>(ZX_OK, "buffer",
                                                std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                                ZxVcpuStateAArch64::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind), ZX_VCPU_STATE)
        ->DisplayIfArch(debug_ipc::Arch::kArm64)
        ->SetId(kZxVcpuReadState_ZX_VCPU_STATE_Arm64);
    zx_vcpu_read_state
        ->OutputObject<zx_vcpu_state_x86_t>(ZX_OK, "buffer",
                                            std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                            ZxVcpuStateX86::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind), ZX_VCPU_STATE)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxVcpuReadState_ZX_VCPU_STATE_X64);
  }

  {
    Syscall* zx_vcpu_write_state = Add("zx_vcpu_write_state", SyscallReturnType::kStatus);
    // Arguments
    auto handle = zx_vcpu_write_state->Argument<zx_handle_t>(SyscallType::kHandle);
    auto kind = zx_vcpu_write_state->Argument<uint32_t>(SyscallType::kVcpu);
    auto buffer = zx_vcpu_write_state->PointerArgument<uint8_t>(SyscallType::kUint8);
    zx_vcpu_write_state->Argument<size_t>(SyscallType::kSize);
    // Conditional field Ids
    const uint8_t kZxVcpuWriteState_ZX_VCPU_STATE_Arm64 = 1;
    const uint8_t kZxVcpuWriteState_ZX_VCPU_STATE_X64 = 2;
    const uint8_t kZxVcpuWriteState_ZX_VCPU_IO = 3;
    // Inputs
    zx_vcpu_write_state->Input<zx_handle_t>("handle",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(handle));
    zx_vcpu_write_state->Input<uint32_t>("kind", std::make_unique<ArgumentAccess<uint32_t>>(kind));
    zx_vcpu_write_state
        ->InputObject<zx_vcpu_state_aarch64_t>("buffer",
                                               std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                               ZxVcpuStateAArch64::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind), ZX_VCPU_STATE)
        ->DisplayIfArch(debug_ipc::Arch::kArm64)
        ->SetId(kZxVcpuWriteState_ZX_VCPU_STATE_Arm64);
    zx_vcpu_write_state
        ->InputObject<zx_vcpu_state_x86_t>(
            "buffer", std::make_unique<ArgumentAccess<uint8_t>>(buffer), ZxVcpuStateX86::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind), ZX_VCPU_STATE)
        ->DisplayIfArch(debug_ipc::Arch::kX64)
        ->SetId(kZxVcpuWriteState_ZX_VCPU_STATE_X64);
    zx_vcpu_write_state
        ->InputObject<zx_vcpu_io_t>("buffer", std::make_unique<ArgumentAccess<uint8_t>>(buffer),
                                    ZxVcpuIo::GetClass())
        ->DisplayIfEqual<uint32_t>(std::make_unique<ArgumentAccess<uint32_t>>(kind), ZX_VCPU_IO)
        ->SetId(kZxVcpuWriteState_ZX_VCPU_IO);
  }

  {
    Syscall* zx_pager_create = Add("zx_pager_create", SyscallReturnType::kStatus);
    // Arguments
    auto options = zx_pager_create->Argument<uint32_t>(SyscallType::kUint32);
    auto out = zx_pager_create->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_pager_create->Input<uint32_t>("options",
                                     std::make_unique<ArgumentAccess<uint32_t>>(options));
    // Outputs
    zx_pager_create->Output<zx_handle_t>(ZX_OK, "out",
                                         std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_pager_create->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_pager_create_vmo = Add("zx_pager_create_vmo", SyscallReturnType::kStatus);
    // Arguments
    auto pager = zx_pager_create_vmo->Argument<zx_handle_t>(SyscallType::kHandle);
    auto options = zx_pager_create_vmo->Argument<uint32_t>(SyscallType::kUint32);
    auto port = zx_pager_create_vmo->Argument<zx_handle_t>(SyscallType::kHandle);
    auto key = zx_pager_create_vmo->Argument<uint64_t>(SyscallType::kUint64);
    auto size = zx_pager_create_vmo->Argument<uint64_t>(SyscallType::kUint64);
    auto out = zx_pager_create_vmo->PointerArgument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_pager_create_vmo->Input<zx_handle_t>("pager",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(pager));
    zx_pager_create_vmo->Input<uint32_t>("options",
                                         std::make_unique<ArgumentAccess<uint32_t>>(options));
    zx_pager_create_vmo->Input<zx_handle_t>("port",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(port));
    zx_pager_create_vmo->Input<uint64_t>("key", std::make_unique<ArgumentAccess<uint64_t>>(key));
    zx_pager_create_vmo->Input<uint64_t>("size", std::make_unique<ArgumentAccess<uint64_t>>(size));
    // Outputs
    zx_pager_create_vmo->Output<zx_handle_t>(ZX_OK, "out",
                                             std::make_unique<ArgumentAccess<zx_handle_t>>(out));

    zx_pager_create_vmo->set_compute_statistics([](const OutputEvent* event) {
      auto out =
          event->GetHandleInfo(event->syscall()->SearchInlineMember("out", /*invoked=*/false));
      FX_DCHECK(out != nullptr);
      out->AddCreationEvent(event);
    });
  }

  {
    Syscall* zx_pager_detach_vmo = Add("zx_pager_detach_vmo", SyscallReturnType::kStatus);
    // Arguments
    auto pager = zx_pager_detach_vmo->Argument<zx_handle_t>(SyscallType::kHandle);
    auto vmo = zx_pager_detach_vmo->Argument<zx_handle_t>(SyscallType::kHandle);
    // Inputs
    zx_pager_detach_vmo->Input<zx_handle_t>("pager",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(pager));
    zx_pager_detach_vmo->Input<zx_handle_t>("vmo",
                                            std::make_unique<ArgumentAccess<zx_handle_t>>(vmo));
  }

  {
    Syscall* zx_pager_supply_pages = Add("zx_pager_supply_pages", SyscallReturnType::kStatus);
    // Arguments
    auto pager = zx_pager_supply_pages->Argument<zx_handle_t>(SyscallType::kHandle);
    auto pager_vmo = zx_pager_supply_pages->Argument<zx_handle_t>(SyscallType::kHandle);
    auto offset = zx_pager_supply_pages->Argument<uint64_t>(SyscallType::kUint64);
    auto length = zx_pager_supply_pages->Argument<uint64_t>(SyscallType::kUint64);
    auto aux_vmo = zx_pager_supply_pages->Argument<zx_handle_t>(SyscallType::kHandle);
    auto aux_offset = zx_pager_supply_pages->Argument<uint64_t>(SyscallType::kUint64);
    // Inputs
    zx_pager_supply_pages->Input<zx_handle_t>("pager",
                                              std::make_unique<ArgumentAccess<zx_handle_t>>(pager));
    zx_pager_supply_pages->Input<zx_handle_t>(
        "pager_vmo", std::make_unique<ArgumentAccess<zx_handle_t>>(pager_vmo));
    zx_pager_supply_pages->Input<uint64_t>("offset",
                                           std::make_unique<ArgumentAccess<uint64_t>>(offset));
    zx_pager_supply_pages->Input<uint64_t>("length",
                                           std::make_unique<ArgumentAccess<uint64_t>>(length));
    zx_pager_supply_pages->Input<zx_handle_t>(
        "aux_vmo", std::make_unique<ArgumentAccess<zx_handle_t>>(aux_vmo));
    zx_pager_supply_pages->Input<uint64_t>("aux_offset",
                                           std::make_unique<ArgumentAccess<uint64_t>>(aux_offset));
  }
}

}  // namespace fidlcat
