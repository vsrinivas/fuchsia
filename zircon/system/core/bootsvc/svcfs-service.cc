// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svcfs-service.h"

#include <fuchsia/boot/c/fidl.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/job.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls/log.h>

#include "util.h"

namespace {

struct ArgumentsData {
  zx::vmo vmo;
  size_t size;
};

zx_status_t ArgumentsGet(void* ctx, fidl_txn_t* txn) {
  auto data = static_cast<const ArgumentsData*>(ctx);
  zx::vmo dup;
  zx_status_t status = data->vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to duplicate boot arguments VMO: %s\n", zx_status_get_string(status));
    return status;
  }
  return fuchsia_boot_ArgumentsGet_reply(txn, dup.release(), data->size);
}

constexpr fuchsia_boot_Arguments_ops kArgumentsOps = {
    .Get = ArgumentsGet,
};

zx_status_t FactoryItemsGet(void* ctx, uint32_t extra, fidl_txn_t* txn) {
  auto map = static_cast<bootsvc::FactoryItemMap*>(ctx);
  auto it = map->find(extra);
  if (it == map->end()) {
    return fuchsia_boot_FactoryItemsGet_reply(txn, ZX_HANDLE_INVALID, 0);
  }

  const zx::vmo& vmo = it->second.vmo;
  uint32_t length = it->second.length;
  zx::vmo payload;
  zx_status_t status =
      vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS & ~(ZX_RIGHT_WRITE | ZX_RIGHT_SET_PROPERTY), &payload);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to duplicate handle for factory item VMO: %s",
           zx_status_get_string(status));
    return status;
  }

  return fuchsia_boot_FactoryItemsGet_reply(txn, payload.release(), length);
}

constexpr fuchsia_boot_FactoryItems_ops kFactoryItemsOps = {
    .Get = FactoryItemsGet,
};

struct ItemsData {
  zx::vmo vmo;
  bootsvc::ItemMap map;
};

zx_status_t ItemsGet(void* ctx, uint32_t type, uint32_t extra, fidl_txn_t* txn) {
  auto data = static_cast<const ItemsData*>(ctx);
  auto it = data->map.find(bootsvc::ItemKey{type, extra});
  if (it == data->map.end()) {
    return fuchsia_boot_ItemsGet_reply(txn, ZX_HANDLE_INVALID, 0);
  }
  auto& item = it->second;
  auto buf = std::make_unique<uint8_t[]>(item.length);
  zx_status_t status = data->vmo.read(buf.get(), item.offset, item.length);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to read from boot image VMO: %s\n", zx_status_get_string(status));
    return status;
  }
  zx::vmo payload;
  status = zx::vmo::create(item.length, 0, &payload);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to create payload VMO: %s\n", zx_status_get_string(status));
    return status;
  }
  status = payload.write(buf.get(), 0, item.length);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to write to payload VMO: %s\n", zx_status_get_string(status));
    return status;
  }
  return fuchsia_boot_ItemsGet_reply(txn, payload.release(), item.length);
}

constexpr fuchsia_boot_Items_ops kItemsOps = {
    .Get = ItemsGet,
};

zx_status_t ReadOnlyLogGet(void* ctx, fidl_txn_t* txn) {
  auto root_resource = static_cast<const zx::resource*>(ctx);
  zx::debuglog ret;
  zx_status_t status = zx::debuglog::create(*root_resource, ZX_LOG_FLAG_READABLE, &ret);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to create readable kernel log: %s\n", zx_status_get_string(status));
    return status;
  }

  // Drop write right.
  status = ret.replace((ZX_DEFAULT_LOG_RIGHTS & (~ZX_RIGHT_WRITE)) | ZX_RIGHT_READ, &ret);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to drop write from readable kernel log: %s\n",
           zx_status_get_string(status));
    return status;
  }
  return fuchsia_boot_ReadOnlyLogGet_reply(txn, ret.release());
}

constexpr fuchsia_boot_ReadOnlyLog_ops kReadOnlyLogOps = {
    .Get = ReadOnlyLogGet,
};

zx_status_t WriteOnlyLogGet(void* ctx, fidl_txn_t* txn) {
  auto log = static_cast<const zx::debuglog*>(ctx);
  zx::debuglog dup;
  zx_status_t status = log->duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to duplicate writable kernel log: %s\n", zx_status_get_string(status));
    return status;
  }
  return fuchsia_boot_WriteOnlyLogGet_reply(txn, dup.release());
}

constexpr fuchsia_boot_WriteOnlyLog_ops kWriteOnlyLogOps = {
    .Get = WriteOnlyLogGet,
};

zx_status_t RootResourceGet(void* ctx, fidl_txn_t* txn) {
  auto root_resource = static_cast<const zx::resource*>(ctx);
  zx::resource root_resource_dup;
  zx_status_t status = root_resource->duplicate(ZX_RIGHT_SAME_RIGHTS, &root_resource_dup);
  if (status != ZX_OK) {
    printf("bootsvc: Failed to duplicate root resource handle: %s\n", zx_status_get_string(status));
    return status;
  }
  return fuchsia_boot_RootResourceGet_reply(txn, root_resource_dup.release());
}

constexpr fuchsia_boot_RootResource_ops kRootResourceOps = {
    .Get = RootResourceGet,
};

}  // namespace

namespace bootsvc {

fbl::RefPtr<SvcfsService> SvcfsService::Create(async_dispatcher_t* dispatcher) {
  return fbl::AdoptRef(new SvcfsService(dispatcher));
}

SvcfsService::SvcfsService(async_dispatcher_t* dispatcher)
    : vfs_(dispatcher), root_(fbl::MakeRefCounted<fs::PseudoDir>()) {}

void SvcfsService::AddService(const char* service_name, fbl::RefPtr<fs::Service> service) {
  root_->AddEntry(service_name, std::move(service));
}

zx_status_t SvcfsService::CreateRootConnection(zx::channel* out) {
  return CreateVnodeConnection(&vfs_, root_, fs::Rights::ReadWrite(), out);
}

fbl::RefPtr<fs::Service> CreateArgumentsService(async_dispatcher_t* dispatcher, zx::vmo vmo,
                                                uint64_t size) {
  ArgumentsData data{std::move(vmo), size};
  return fbl::MakeRefCounted<fs::Service>(
      [dispatcher, data = std::move(data)](zx::channel channel) mutable {
        auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Arguments_dispatch);
        return fidl_bind(dispatcher, channel.release(), dispatch, &data, &kArgumentsOps);
      });
}

fbl::RefPtr<fs::Service> CreateFactoryItemsService(async_dispatcher_t* dispatcher,
                                                   FactoryItemMap map) {
  return fbl::MakeRefCounted<fs::Service>(
      [dispatcher, map = std::move(map)](zx::channel channel) mutable {
        auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_FactoryItems_dispatch);
        return fidl_bind(dispatcher, channel.release(), dispatch, &map, &kFactoryItemsOps);
      });
}

fbl::RefPtr<fs::Service> CreateItemsService(async_dispatcher_t* dispatcher, zx::vmo vmo,
                                            ItemMap map) {
  ItemsData data{std::move(vmo), std::move(map)};
  return fbl::MakeRefCounted<fs::Service>(
      [dispatcher, data = std::move(data)](zx::channel channel) mutable {
        auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_Items_dispatch);
        return fidl_bind(dispatcher, channel.release(), dispatch, &data, &kItemsOps);
      });
}

fbl::RefPtr<fs::Service> CreateReadOnlyLogService(async_dispatcher_t* dispatcher,
                                                  const zx::resource& root_resource) {
  return fbl::MakeRefCounted<fs::Service>([dispatcher, &root_resource](zx::channel channel) {
    auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_ReadOnlyLog_dispatch);
    return fidl_bind(dispatcher, channel.release(), dispatch,
                     const_cast<zx::resource*>(&root_resource), &kReadOnlyLogOps);
  });
}

fbl::RefPtr<fs::Service> CreateWriteOnlyLogService(async_dispatcher_t* dispatcher,
                                                   const zx::debuglog& log) {
  return fbl::MakeRefCounted<fs::Service>([dispatcher, &log](zx::channel channel) {
    auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_WriteOnlyLog_dispatch);
    return fidl_bind(dispatcher, channel.release(), dispatch, const_cast<zx::debuglog*>(&log),
                     &kWriteOnlyLogOps);
  });
}

fbl::RefPtr<fs::Service> CreateRootResourceService(async_dispatcher_t* dispatcher,
                                                   const zx::resource& root_resource) {
  return fbl::MakeRefCounted<fs::Service>(
      [dispatcher, &root_resource](zx::channel channel) mutable {
        auto dispatch = reinterpret_cast<fidl_dispatch_t*>(fuchsia_boot_RootResource_dispatch);
        return fidl_bind(dispatcher, channel.release(), dispatch,
                         const_cast<zx::resource*>(&root_resource), &kRootResourceOps);
      });
}

fbl::RefPtr<fs::Service> KernelStatsImpl::CreateService(async_dispatcher_t* dispatcher) {
  return fbl::MakeRefCounted<fs::Service>([dispatcher, this](zx::channel channel) mutable {
    return fidl::Bind(dispatcher, std::move(channel), this);
  });
}

void KernelStatsImpl::GetMemoryStats(GetMemoryStatsCompleter::Sync completer) {
  zx_info_kmem_stats_t mem_stats;
  zx_status_t status = root_resource_.get_info(ZX_INFO_KMEM_STATS, &mem_stats,
                                               sizeof(zx_info_kmem_stats_t), nullptr, nullptr);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  auto builder = llcpp::fuchsia::kernel::MemoryStats::Build();
  builder.set_total_bytes(&mem_stats.total_bytes);
  builder.set_free_bytes(&mem_stats.free_bytes);
  builder.set_wired_bytes(&mem_stats.wired_bytes);
  builder.set_total_heap_bytes(&mem_stats.total_heap_bytes);
  builder.set_free_heap_bytes(&mem_stats.free_heap_bytes);
  builder.set_vmo_bytes(&mem_stats.vmo_bytes);
  builder.set_mmu_overhead_bytes(&mem_stats.mmu_overhead_bytes);
  builder.set_ipc_bytes(&mem_stats.ipc_bytes);
  builder.set_other_bytes(&mem_stats.other_bytes);
  completer.Reply(builder.view());
}

void KernelStatsImpl::GetCpuStats(GetCpuStatsCompleter::Sync completer) {
  zx_info_cpu_stats_t cpu_stats[ZX_CPU_SET_MAX_CPUS];
  size_t actual, available;
  zx_status_t status = root_resource_.get_info(ZX_INFO_CPU_STATS, cpu_stats,
                                               sizeof(zx_info_cpu_stats_t) * ZX_CPU_SET_MAX_CPUS,
                                               &actual, &available);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }

  llcpp::fuchsia::kernel::CpuStats stats;
  stats.actual_num_cpus = actual;
  llcpp::fuchsia::kernel::PerCpuStats per_cpu_stats[available];
  fbl::Vector<std::unique_ptr<llcpp::fuchsia::kernel::PerCpuStats::Builder>> builders;
  stats.per_cpu_stats = fidl::VectorView(per_cpu_stats, available);
  for (uint32_t cpu_num = 0; cpu_num < available; ++cpu_num) {
    builders.push_back(std::make_unique<llcpp::fuchsia::kernel::PerCpuStats::Builder>(
        llcpp::fuchsia::kernel::PerCpuStats::Build()));
    auto& builder = builders[cpu_num];
    auto& cpu_stat = cpu_stats[cpu_num];
    builder->set_cpu_number(&cpu_stat.cpu_number);
    builder->set_flags(&cpu_stat.flags);
    builder->set_idle_time(&cpu_stat.idle_time);
    builder->set_reschedules(&cpu_stat.reschedules);
    builder->set_context_switches(&cpu_stat.context_switches);
    builder->set_irq_preempts(&cpu_stat.irq_preempts);
    builder->set_yields(&cpu_stat.yields);
    builder->set_ints(&cpu_stat.ints);
    builder->set_timer_ints(&cpu_stat.timer_ints);
    builder->set_timers(&cpu_stat.timers);
    builder->set_page_faults(&cpu_stat.page_faults);
    builder->set_exceptions(&cpu_stat.exceptions);
    builder->set_syscalls(&cpu_stat.syscalls);
    builder->set_reschedule_ipis(&cpu_stat.reschedule_ipis);
    builder->set_generic_ipis(&cpu_stat.generic_ipis);
    per_cpu_stats[cpu_num] = builder->view();
  }
  completer.Reply(stats);
}

}  // namespace bootsvc
