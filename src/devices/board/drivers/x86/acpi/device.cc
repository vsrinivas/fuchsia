// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <lib/ddk/debug.h>
#include <zircon/syscalls/resource.h>

#include <fbl/auto_lock.h>

#include "src/devices/board/drivers/x86/include/errors.h"
#include "src/devices/board/drivers/x86/include/sysmem.h"
#include "src/devices/lib/iommu/iommu.h"

namespace acpi {
ACPI_STATUS Device::AddResource(ACPI_RESOURCE* res) {
  if (resource_is_memory(res)) {
    resource_memory_t mem;
    zx_status_t st = resource_parse_memory(res, &mem);
    // only expect fixed memory resource. resource_parse_memory sets minimum == maximum
    // for this memory resource type.
    if ((st != ZX_OK) || (mem.minimum != mem.maximum)) {
      return AE_ERROR;
    }
    mmio_resources_.emplace_back(mem);

  } else if (resource_is_address(res)) {
    resource_address_t addr;
    zx_status_t st = resource_parse_address(res, &addr);
    if (st != ZX_OK) {
      return AE_ERROR;
    }
    if ((addr.resource_type == RESOURCE_ADDRESS_MEMORY) && addr.min_address_fixed &&
        addr.max_address_fixed && (addr.maximum < addr.minimum)) {
      mmio_resources_.emplace_back(/* writeable= */ true, addr.min_address_fixed,
                                   /* alignment= */ 0, static_cast<uint32_t>(addr.address_length));
    }

  } else if (resource_is_io(res)) {
    resource_io_t io;
    zx_status_t st = resource_parse_io(res, &io);
    if (st != ZX_OK) {
      return AE_ERROR;
    }

    pio_resources_.emplace_back(io);

  } else if (resource_is_irq(res)) {
    resource_irq_t irq;
    zx_status_t st = resource_parse_irq(res, &irq);
    if (st != ZX_OK) {
      return AE_ERROR;
    }
    for (auto i = 0; i < irq.pin_count; i++) {
      irqs_.emplace_back(irq, i);
    }
  }

  return AE_OK;
}

zx_status_t Device::ReportCurrentResources() {
  if (got_resources_) {
    return ZX_OK;
  }

  // call _CRS to fill in resources
  ACPI_STATUS acpi_status = AcpiWalkResources(
      acpi_handle_, (char*)"_CRS",
      [](ACPI_RESOURCE* res, void* ctx) {
        return reinterpret_cast<Device*>(ctx)->AddResource(res);
      },
      this);
  if ((acpi_status != AE_NOT_FOUND) && (acpi_status != AE_OK)) {
    return acpi_to_zx_status(acpi_status);
  }

  zxlogf(DEBUG, "acpi-bus[%s]: found %zd port resources %zd memory resources %zx irqs",
         device_get_name(zxdev_), pio_resources_.size(), mmio_resources_.size(), irqs_.size());
  if (zxlog_level_enabled(TRACE)) {
    zxlogf(TRACE, "port resources:");
    for (size_t i = 0; i < pio_resources_.size(); i++) {
      zxlogf(TRACE, "  %02zd: addr=0x%x length=0x%x align=0x%x", i, pio_resources_[i].base_address,
             pio_resources_[i].address_length, pio_resources_[i].alignment);
    }
    zxlogf(TRACE, "memory resources:");
    for (size_t i = 0; i < mmio_resources_.size(); i++) {
      zxlogf(TRACE, "  %02zd: addr=0x%x length=0x%x align=0x%x writeable=%d", i,
             mmio_resources_[i].base_address, mmio_resources_[i].address_length,
             mmio_resources_[i].alignment, mmio_resources_[i].writeable);
    }
    zxlogf(TRACE, "irqs:");
    for (size_t i = 0; i < irqs_.size(); i++) {
      const char* trigger;
      switch (irqs_[i].trigger) {
        case ACPI_IRQ_TRIGGER_EDGE:
          trigger = "edge";
          break;
        case ACPI_IRQ_TRIGGER_LEVEL:
          trigger = "level";
          break;
        default:
          trigger = "bad_trigger";
          break;
      }
      const char* polarity;
      switch (irqs_[i].polarity) {
        case ACPI_IRQ_ACTIVE_BOTH:
          polarity = "both";
          break;
        case ACPI_IRQ_ACTIVE_LOW:
          polarity = "low";
          break;
        case ACPI_IRQ_ACTIVE_HIGH:
          polarity = "high";
          break;
        default:
          polarity = "bad_polarity";
          break;
      }
      zxlogf(TRACE, "  %02zd: pin=%u %s %s %s %s", i, irqs_[i].pin, trigger, polarity,
             (irqs_[i].sharable == ACPI_IRQ_SHARED) ? "shared" : "exclusive",
             irqs_[i].wake_capable ? "wake" : "nowake");
    }
  }

  got_resources_ = true;

  return ZX_OK;
}

zx_status_t Device::AcpiGetPio(uint32_t index, zx::resource* out_pio) {
  fbl::AutoLock<fbl::Mutex> guard{&lock_};
  zx_status_t st = ReportCurrentResources();
  if (st != ZX_OK) {
    return st;
  }

  if (index >= pio_resources_.size()) {
    return ZX_ERR_NOT_FOUND;
  }

  const DevicePioResource& res = pio_resources_[index];

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  // TODO: figure out what to pass to name here
  return zx::resource::create(*zx::unowned_resource{get_root_resource()}, ZX_RSRC_KIND_IOPORT,
                              res.base_address, res.address_length, device_get_name(zxdev_), 0,
                              out_pio);
}

zx_status_t Device::AcpiGetMmio(uint32_t index, acpi_mmio* out_mmio) {
  fbl::AutoLock<fbl::Mutex> guard{&lock_};
  zx_status_t st = ReportCurrentResources();
  if (st != ZX_OK) {
    return st;
  }

  if (index >= mmio_resources_.size()) {
    return ZX_ERR_NOT_FOUND;
  }

  const DeviceMmioResource& res = mmio_resources_[index];
  // TODO(fxbug.dev/67899): This check becomes overly pessimistic at larger page sizes.
  if (((res.base_address & (zx_system_get_page_size() - 1)) != 0) ||
      ((res.address_length & (zx_system_get_page_size() - 1)) != 0)) {
    zxlogf(ERROR, "acpi-bus[%s]: memory id=%d addr=0x%08x len=0x%x is not page aligned",
           device_get_name(zxdev_), index, res.base_address, res.address_length);
    return ZX_ERR_NOT_FOUND;
  }

  zx_handle_t vmo;
  size_t size{res.address_length};
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  st = zx_vmo_create_physical(get_root_resource(), res.base_address, size, &vmo);
  if (st != ZX_OK) {
    return st;
  }

  out_mmio->offset = 0;
  out_mmio->size = size;
  out_mmio->vmo = vmo;

  return ZX_OK;
}

zx_status_t Device::AcpiMapInterrupt(int64_t which_irq, zx::interrupt* out_handle) {
  fbl::AutoLock<fbl::Mutex> guard{&lock_};
  zx_status_t st = ReportCurrentResources();
  if (st != ZX_OK) {
    return st;
  }

  if (static_cast<uint64_t>(which_irq) >= irqs_.size()) {
    return ZX_ERR_NOT_FOUND;
  }

  const DeviceIrqResource& irq = irqs_[which_irq];
  uint32_t mode;
  mode = ZX_INTERRUPT_MODE_DEFAULT;
  st = ZX_OK;
  switch (irq.trigger) {
    case ACPI_IRQ_TRIGGER_EDGE:
      switch (irq.polarity) {
        case ACPI_IRQ_ACTIVE_BOTH:
          mode = ZX_INTERRUPT_MODE_EDGE_BOTH;
          break;
        case ACPI_IRQ_ACTIVE_LOW:
          mode = ZX_INTERRUPT_MODE_EDGE_LOW;
          break;
        case ACPI_IRQ_ACTIVE_HIGH:
          mode = ZX_INTERRUPT_MODE_EDGE_HIGH;
          break;
        default:
          st = ZX_ERR_INVALID_ARGS;
          break;
      }
      break;
    case ACPI_IRQ_TRIGGER_LEVEL:
      switch (irq.polarity) {
        case ACPI_IRQ_ACTIVE_LOW:
          mode = ZX_INTERRUPT_MODE_LEVEL_LOW;
          break;
        case ACPI_IRQ_ACTIVE_HIGH:
          mode = ZX_INTERRUPT_MODE_LEVEL_HIGH;
          break;
        default:
          st = ZX_ERR_INVALID_ARGS;
          break;
      }
      break;
    default:
      st = ZX_ERR_INVALID_ARGS;
      break;
  }
  if (st != ZX_OK) {
    return st;
  }
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  return zx::interrupt::create(*zx::unowned_resource{get_root_resource()}, irq.pin,
                               ZX_INTERRUPT_REMAP_IRQ | mode, out_handle);
}

zx_status_t Device::AcpiGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
  // The x86 IOMMU world uses PCI BDFs as the hardware identifiers, so there
  // will only be one BTI per device.
  ZX_ASSERT(index == 0);
  // For dummy IOMMUs, the bti_id just needs to be unique.  For Intel IOMMUs,
  // the bti_ids correspond to PCI BDFs.
  zx_handle_t iommu_handle;
  zx_status_t status = iommu_manager_iommu_for_bdf(bdf, &iommu_handle);
  if (status != ZX_OK) {
    return status;
  }
  return zx::bti::create(*zx::unowned_iommu{iommu_handle}, 0, bdf, bti);
}

zx_status_t Device::AcpiConnectSysmem(zx::channel connection) {
  fbl::AutoLock<fbl::Mutex> guard{&lock_};
  sysmem_protocol_t sysmem;
  zx_status_t st = device_get_protocol(platform_bus_, ZX_PROTOCOL_SYSMEM, &sysmem);
  if (st != ZX_OK) {
    return st;
  }
  return sysmem_connect(&sysmem, connection.release());
}

zx_status_t Device::AcpiRegisterSysmemHeap(uint64_t heap, zx::channel connection) {
  fbl::AutoLock<fbl::Mutex> guard{&lock_};
  sysmem_protocol_t sysmem;
  zx_status_t st = device_get_protocol(platform_bus_, ZX_PROTOCOL_SYSMEM, &sysmem);
  if (st != ZX_OK) {
    return st;
  }
  return sysmem_register_heap(&sysmem, heap, connection.release());
}
}  // namespace acpi
