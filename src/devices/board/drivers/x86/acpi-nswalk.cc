// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <limits.h>
#include <sys/types.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/iommu.h>
#include <zircon/syscalls/resource.h>
#include <zircon/threads.h>

#include <variant>
#include <vector>

#include <acpica/acpi.h>
#include <ddk/debug.h>
#include <ddk/protocol/acpi.h>
#include <ddk/protocol/pciroot.h>
#include <ddk/protocol/sysmem.h>
#include <fbl/auto_lock.h>

#include "acpi-private.h"
#include "acpi.h"
#include "dev.h"
#include "errors.h"
#include "iommu.h"
#include "methods.h"
#include "nhlt.h"
#include "pci.h"
#include "power.h"
#include "resources.h"
#include "sysmem.h"

zx_protocol_device_t acpi_device_proto = [] {
  zx_protocol_device_t ops = {};
  ops.version = DEVICE_OPS_VERSION;
  ops.release = free;
  return ops;
}();

ACPI_STATUS acpi_device_t::AddResource(ACPI_RESOURCE* res) {
  if (resource_is_memory(res)) {
    resource_memory_t mem;
    zx_status_t st = resource_parse_memory(res, &mem);
    // only expect fixed memory resource. resource_parse_memory sets minimum == maximum
    // for this memory resource type.
    if ((st != ZX_OK) || (mem.minimum != mem.maximum)) {
      return AE_ERROR;
    }
    mmio_resources.emplace_back(mem);

  } else if (resource_is_address(res)) {
    resource_address_t addr;
    zx_status_t st = resource_parse_address(res, &addr);
    if (st != ZX_OK) {
      return AE_ERROR;
    }
    if ((addr.resource_type == RESOURCE_ADDRESS_MEMORY) && addr.min_address_fixed &&
        addr.max_address_fixed && (addr.maximum < addr.minimum)) {
      mmio_resources.emplace_back(/* writeable= */ true, addr.min_address_fixed,
                                  /* alignment= */ 0, static_cast<uint32_t>(addr.address_length));
    }

  } else if (resource_is_io(res)) {
    resource_io_t io;
    zx_status_t st = resource_parse_io(res, &io);
    if (st != ZX_OK) {
      return AE_ERROR;
    }

    pio_resources.emplace_back(io);

  } else if (resource_is_irq(res)) {
    resource_irq_t irq;
    zx_status_t st = resource_parse_irq(res, &irq);
    if (st != ZX_OK) {
      return AE_ERROR;
    }
    for (auto i = 0; i < irq.pin_count; i++) {
      irqs.emplace_back(irq, i);
    }
  }

  return AE_OK;
}

zx_status_t acpi_device_t::ReportCurrentResources() {
  if (got_resources) {
    return ZX_OK;
  }

  // call _CRS to fill in resources
  ACPI_STATUS acpi_status = AcpiWalkResources(
      ns_node, (char*)"_CRS",
      [](ACPI_RESOURCE* res, void* ctx) {
        return reinterpret_cast<acpi_device_t*>(ctx)->AddResource(res);
      },
      this);
  if ((acpi_status != AE_NOT_FOUND) && (acpi_status != AE_OK)) {
    return acpi_to_zx_status(acpi_status);
  }

  zxlogf(TRACE, "acpi-bus[%s]: found %zd port resources %zd memory resources %zx irqs\n",
         device_get_name(zxdev), pio_resources.size(), mmio_resources.size(), irqs.size());
  if (driver_get_log_flags() & DDK_LOG_SPEW) {
    zxlogf(SPEW, "port resources:\n");
    for (size_t i = 0; i < pio_resources.size(); i++) {
      zxlogf(SPEW, "  %02zd: addr=0x%x length=0x%x align=0x%x\n", i, pio_resources[i].base_address,
             pio_resources[i].address_length, pio_resources[i].alignment);
    }
    zxlogf(SPEW, "memory resources:\n");
    for (size_t i = 0; i < mmio_resources.size(); i++) {
      zxlogf(SPEW, "  %02zd: addr=0x%x length=0x%x align=0x%x writeable=%d\n", i,
             mmio_resources[i].base_address, mmio_resources[i].address_length,
             mmio_resources[i].alignment, mmio_resources[i].writeable);
    }
    zxlogf(SPEW, "irqs:\n");
    for (size_t i = 0; i < irqs.size(); i++) {
      const char* trigger;
      switch (irqs[i].trigger) {
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
      switch (irqs[i].polarity) {
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
      zxlogf(SPEW, "  %02zd: pin=%u %s %s %s %s\n", i, irqs[i].pin, trigger, polarity,
             (irqs[i].sharable == ACPI_IRQ_SHARED) ? "shared" : "exclusive",
             irqs[i].wake_capable ? "wake" : "nowake");
    }
  }

  got_resources = true;

  return ZX_OK;
}

zx_status_t acpi_device_t::AcpiOpGetPioLocked(uint32_t index, zx_handle_t* out_pio) {
  zx_status_t st = ReportCurrentResources();
  if (st != ZX_OK) {
    return st;
  }

  if (index >= pio_resources.size()) {
    return ZX_ERR_NOT_FOUND;
  }

  const AcpiDevicePioResource& res = pio_resources[index];

  zx_handle_t resource;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  // TODO: figure out what to pass to name here
  st = zx_resource_create(get_root_resource(), ZX_RSRC_KIND_IOPORT, res.base_address,
                          res.address_length, device_get_name(zxdev), 0, &resource);
  if (st != ZX_OK) {
    return st;
  }

  *out_pio = resource;
  return ZX_OK;
}

zx_status_t acpi_device_t::AcpiOpGetMmioLocked(uint32_t index, acpi_mmio* out_mmio) {
  zx_status_t st = ReportCurrentResources();
  if (st != ZX_OK) {
    return st;
  }

  if (index >= mmio_resources.size()) {
    return ZX_ERR_NOT_FOUND;
  }

  const AcpiDeviceMmioResource& res = mmio_resources[index];
  if (((res.base_address & (PAGE_SIZE - 1)) != 0) ||
      ((res.address_length & (PAGE_SIZE - 1)) != 0)) {
    zxlogf(ERROR, "acpi-bus[%s]: memory id=%d addr=0x%08x len=0x%x is not page aligned\n",
           device_get_name(zxdev), index, res.base_address, res.address_length);
    return ZX_ERR_NOT_FOUND;
  }

  zx_handle_t vmo;
  size_t size{res.address_length};
  // Please do not use get_root_resource() in new code. See ZX-1467.
  st = zx_vmo_create_physical(get_root_resource(), res.base_address, size, &vmo);
  if (st != ZX_OK) {
    return st;
  }

  out_mmio->offset = 0;
  out_mmio->size = size;
  out_mmio->vmo = vmo;

  return ZX_OK;
}

zx_status_t acpi_device_t::AcpiOpMapInterruptLocked(int64_t which_irq, zx_handle_t* out_handle) {
  zx_status_t st = ReportCurrentResources();
  if (st != ZX_OK) {
    return st;
  }

  if ((uint)which_irq >= irqs.size()) {
    return ZX_ERR_NOT_FOUND;
  }

  const AcpiDeviceIrqResource& irq = irqs[which_irq];
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
  zx_handle_t handle;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  st = zx_interrupt_create(get_root_resource(), irq.pin, ZX_INTERRUPT_REMAP_IRQ | mode, &handle);
  if (st != ZX_OK) {
    return st;
  }
  *out_handle = handle;

  return st;
}

static zx_status_t acpi_op_get_bti(void* ctx, uint32_t bdf, uint32_t index, zx_handle_t* bti) {
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
  return zx_bti_create(iommu_handle, 0, bdf, bti);
}

zx_status_t acpi_device_t::AcpiOpConnectSysmemLocked(zx_handle_t handle) const {
  sysmem_protocol_t sysmem;
  zx_status_t st = device_get_protocol(platform_bus, ZX_PROTOCOL_SYSMEM, &sysmem);
  if (st != ZX_OK) {
    zx_handle_close(handle);
    return st;
  }
  return sysmem_connect(&sysmem, handle);
}

zx_status_t acpi_device_t::AcpiOpRegisterSysmemHeapLocked(uint64_t heap, zx_handle_t handle) const {
  sysmem_protocol_t sysmem;
  zx_status_t st = device_get_protocol(platform_bus, ZX_PROTOCOL_SYSMEM, &sysmem);
  if (st != ZX_OK) {
    zx_handle_close(handle);
    return st;
  }

  return sysmem_register_heap(&sysmem, heap, handle);
}

template <auto Fn, typename... Args>
auto GuardedDeviceFn(void* ctx, Args... args) {
  auto& device = *reinterpret_cast<acpi_device_t*>(ctx);
  fbl::AutoLock<fbl::Mutex> guard{&device.lock};
  return (device.*Fn)(std::forward<Args&&>(args)...);
}

// TODO marking unused until we publish some devices
__attribute__((unused)) acpi_protocol_ops_t acpi_proto = {
    .get_pio = &GuardedDeviceFn<&acpi_device_t::AcpiOpGetPioLocked>,
    .get_mmio = &GuardedDeviceFn<&acpi_device_t::AcpiOpGetMmioLocked>,
    .map_interrupt = &GuardedDeviceFn<&acpi_device_t::AcpiOpMapInterruptLocked>,
    .get_bti = acpi_op_get_bti,
    .connect_sysmem = &GuardedDeviceFn<&acpi_device_t::AcpiOpConnectSysmemLocked>,
    .register_sysmem_heap = &GuardedDeviceFn<&acpi_device_t::AcpiOpRegisterSysmemHeapLocked>,
};

static const char* hid_from_acpi_devinfo(ACPI_DEVICE_INFO* info) {
  const char* hid = nullptr;
  if ((info->Valid & ACPI_VALID_HID) && (info->HardwareId.Length > 0) &&
      ((info->HardwareId.Length - 1) <= sizeof(uint64_t))) {
    hid = (const char*)info->HardwareId.String;
  }
  return hid;
}

zx_device_t* publish_device(zx_device_t* parent, zx_device_t* platform_bus, ACPI_HANDLE handle,
                            ACPI_DEVICE_INFO* info, const char* name, uint32_t protocol_id,
                            void* protocol_ops) {
  zx_device_prop_t props[4];
  int propcount = 0;

  char acpi_name[5] = {0};
  if (!name) {
    memcpy(acpi_name, &info->Name, sizeof(acpi_name) - 1);
    name = (const char*)acpi_name;
  }

  // Publish HID in device props
  const char* hid = hid_from_acpi_devinfo(info);
  if (hid) {
    props[propcount].id = BIND_ACPI_HID_0_3;
    props[propcount++].value = htobe32(*((uint32_t*)(hid)));
    props[propcount].id = BIND_ACPI_HID_4_7;
    props[propcount++].value = htobe32(*((uint32_t*)(hid + 4)));
  }

  // Publish the first CID in device props
  const char* cid = (const char*)info->CompatibleIdList.Ids[0].String;
  if ((info->Valid & ACPI_VALID_CID) && (info->CompatibleIdList.Count > 0) &&
      ((info->CompatibleIdList.Ids[0].Length - 1) <= sizeof(uint64_t))) {
    props[propcount].id = BIND_ACPI_CID_0_3;

    // Use memcpy() to safely access a uint32_t from a misaligned address.
    uint32_t value;
    memcpy(&value, cid, sizeof(value));
    props[propcount++].value = htobe32(value);
    props[propcount].id = BIND_ACPI_CID_4_7;

    memcpy(&value, cid + 4, sizeof(value));
    props[propcount++].value = htobe32(value);
  }

  if (driver_get_log_flags() & DDK_LOG_SPEW) {
    // ACPI names are always 4 characters in a uint32
    zxlogf(SPEW, "acpi: got device %s\n", acpi_name);
    if (info->Valid & ACPI_VALID_HID) {
      zxlogf(SPEW, "     HID=%s\n", info->HardwareId.String);
    } else {
      zxlogf(SPEW, "     HID=invalid\n");
    }
    if (info->Valid & ACPI_VALID_ADR) {
      zxlogf(SPEW, "     ADR=0x%" PRIx64 "\n", (uint64_t)info->Address);
    } else {
      zxlogf(SPEW, "     ADR=invalid\n");
    }
    if (info->Valid & ACPI_VALID_CID) {
      zxlogf(SPEW, "    CIDS=%d\n", info->CompatibleIdList.Count);
      for (uint i = 0; i < info->CompatibleIdList.Count; i++) {
        zxlogf(SPEW, "     [%u] %s\n", i, info->CompatibleIdList.Ids[i].String);
      }
    } else {
      zxlogf(SPEW, "     CID=invalid\n");
    }
    zxlogf(SPEW, "    devprops:\n");
    for (int i = 0; i < propcount; i++) {
      zxlogf(SPEW, "     [%d] id=0x%08x value=0x%08x\n", i, props[i].id, props[i].value);
    }
  }

  auto dev = std::make_unique<acpi_device_t>();
  dev->platform_bus = platform_bus;
  dev->ns_node = handle;

  device_add_args_t args = {};

  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = name;
  args.ctx = dev.get();
  args.ops = &acpi_device_proto;
  args.props = (propcount > 0) ? props : nullptr;
  args.prop_count = static_cast<uint32_t>(propcount);
  args.proto_id = protocol_id;
  args.proto_ops = protocol_ops;

  zx_status_t status;
  if ((status = device_add(parent, &args, &dev->zxdev)) != ZX_OK) {
    zxlogf(ERROR, "acpi: error %d in device_add, parent=%s(%p)\n", status, device_get_name(parent),
           parent);
    return nullptr;
  } else {
    zxlogf(INFO, "acpi: published device %s(%p), parent=%s(%p), handle=%p\n", name, dev.get(),
           device_get_name(parent), parent, (void*)dev->ns_node);
    // device_add takes ownership of args.ctx, but only on success.
    return dev.release()->zxdev;
  }
}

static void acpi_apply_workarounds(ACPI_HANDLE object, ACPI_DEVICE_INFO* info) {
  ACPI_STATUS acpi_status;
  // Slate workaround: Turn on the HID controller.
  if (!memcmp(&info->Name, "I2C0", 4)) {
    ACPI_BUFFER buffer = {
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = nullptr,
    };
    acpi_status = AcpiEvaluateObject(object, (char*)"H00A._PR0", nullptr, &buffer);
    if (acpi_status == AE_OK) {
      ACPI_OBJECT* pkg = static_cast<ACPI_OBJECT*>(buffer.Pointer);
      for (unsigned i = 0; i < pkg->Package.Count; i++) {
        ACPI_OBJECT* ref = &pkg->Package.Elements[i];
        if (ref->Type != ACPI_TYPE_LOCAL_REFERENCE) {
          zxlogf(TRACE, "acpi: Ignoring wrong type 0x%x\n", ref->Type);
        } else {
          zxlogf(TRACE, "acpi: Enabling HID controller at I2C0.H00A._PR0[%u]\n", i);
          acpi_status = AcpiEvaluateObject(ref->Reference.Handle, (char*)"_ON", nullptr, nullptr);
          if (acpi_status != AE_OK) {
            zxlogf(ERROR, "acpi: acpi error 0x%x in I2C0._PR0._ON\n", acpi_status);
          }
        }
      }
      AcpiOsFree(buffer.Pointer);
    }
  }
  // Acer workaround: Turn on the HID controller.
  else if (!memcmp(&info->Name, "I2C1", 4)) {
    zxlogf(TRACE, "acpi: Enabling HID controller at I2C1\n");
    acpi_status = AcpiEvaluateObject(object, (char*)"_PS0", nullptr, nullptr);
    if (acpi_status != AE_OK) {
      zxlogf(ERROR, "acpi: acpi error in I2C1._PS0: 0x%x\n", acpi_status);
    }
  }
}

ACPI_STATUS AcpiWalker::OnDescent(ACPI_HANDLE object) {
  ACPI_DEVICE_INFO* info_rawptr = nullptr;
  ACPI_STATUS acpi_status = AcpiGetObjectInfo(object, &info_rawptr);
  auto acpi_free = [](auto mem) { ACPI_FREE(mem); };
  std::unique_ptr<ACPI_DEVICE_INFO, decltype(acpi_free)> info{info_rawptr, acpi_free};
  if (acpi_status != AE_OK) {
    return acpi_status;
  }

  acpi_apply_workarounds(object, info.get());
  if (!memcmp(&info->Name, "HDAS", 4)) {
    // We must have already seen at least one PCI root due to traversal order.
    if (last_pci_ == kNoLastPci) {
      zxlogf(ERROR, "acpi: Found HDAS node, but no prior PCI root was discovered!\n");
    } else if (!(info->Valid & ACPI_VALID_ADR)) {
      zxlogf(ERROR, "acpi: no valid ADR found for HDA device\n");
    } else {
      // Attaching metadata to the HDAS device /dev/sys/pci/...
      zx_status_t status =
          nhlt_publish_metadata(sys_root_, last_pci_, (uint64_t)info->Address, object);
      if ((status != ZX_OK) && (status != ZX_ERR_NOT_FOUND)) {
        zxlogf(ERROR, "acpi: failed to publish NHLT metadata\n");
      }
    }
  }

  const char* hid = hid_from_acpi_devinfo(info.get());
  if (hid == nullptr) {
    return AE_OK;
  }
  const char* cid = nullptr;
  if ((info->Valid & ACPI_VALID_CID) && (info->CompatibleIdList.Count > 0) &&
      // IDs may be 7 or 8 bytes, and Length includes the null byte
      (info->CompatibleIdList.Ids[0].Length == HID_LENGTH ||
       info->CompatibleIdList.Ids[0].Length == HID_LENGTH + 1)) {
    cid = (const char*)info->CompatibleIdList.Ids[0].String;
  }

  if ((!memcmp(hid, PCI_EXPRESS_ROOT_HID_STRING, HID_LENGTH) ||
       !memcmp(hid, PCI_ROOT_HID_STRING, HID_LENGTH))) {
    pci_init(sys_root_, object, info.get(), this);
  } else if (!memcmp(hid, BATTERY_HID_STRING, HID_LENGTH)) {
    battery_init(acpi_root_, object);
  } else if (!memcmp(hid, LID_HID_STRING, HID_LENGTH)) {
    lid_init(acpi_root_, object);
  } else if (!memcmp(hid, PWRSRC_HID_STRING, HID_LENGTH)) {
    pwrsrc_init(acpi_root_, object);
  } else if (!memcmp(hid, EC_HID_STRING, HID_LENGTH)) {
    ec_init(acpi_root_, object);
  } else if (!memcmp(hid, GOOGLE_TBMC_HID_STRING, HID_LENGTH)) {
    tbmc_init(acpi_root_, object);
  } else if (!memcmp(hid, GOOGLE_CROS_EC_HID_STRING, HID_LENGTH)) {
    cros_ec_lpc_init(acpi_root_, object);
  } else if (!memcmp(hid, DPTF_THERMAL_HID_STRING, HID_LENGTH)) {
    thermal_init(acpi_root_, info.get(), object);
  } else if (!memcmp(hid, I8042_HID_STRING, HID_LENGTH) ||
             (cid && !memcmp(cid, I8042_HID_STRING, HID_LENGTH))) {
    publish_device(acpi_root_, platform_bus_, object, info.get(), "i8042", ZX_PROTOCOL_ACPI,
                   &acpi_proto);
  } else if (!memcmp(hid, RTC_HID_STRING, HID_LENGTH) ||
             (cid && !memcmp(cid, RTC_HID_STRING, HID_LENGTH))) {
    publish_device(acpi_root_, platform_bus_, object, info.get(), "rtc", ZX_PROTOCOL_ACPI,
                   &acpi_proto);
  } else if (!memcmp(hid, GOLDFISH_PIPE_HID_STRING, HID_LENGTH)) {
    publish_device(acpi_root_, platform_bus_, object, info.get(), "goldfish", ZX_PROTOCOL_ACPI,
                   &acpi_proto);
  } else if (!memcmp(hid, SERIAL_HID_STRING, HID_LENGTH)) {
    publish_device(acpi_root_, platform_bus_, object, info.get(), "serial", ZX_PROTOCOL_ACPI,
                   &acpi_proto);
  }
  return AE_OK;
}

zx_status_t acpi_suspend(uint8_t requested_state, bool enable_wake, uint8_t suspend_reason,
                         uint8_t* out_state) {
  switch (suspend_reason & DEVICE_MASK_SUSPEND_REASON) {
    case DEVICE_SUSPEND_REASON_MEXEC: {
      AcpiTerminate();
      return ZX_OK;
    }
    case DEVICE_SUSPEND_REASON_REBOOT:
      if (suspend_reason == DEVICE_SUSPEND_REASON_REBOOT_BOOTLOADER) {
        reboot_bootloader();
      } else if (suspend_reason == DEVICE_SUSPEND_REASON_REBOOT_RECOVERY) {
        reboot_recovery();
      } else {
        reboot();
      }
      // Kill this driver so that the IPC channel gets closed; devmgr will
      // perform a fallback that should shutdown or reboot the machine.
      exit(0);
    case DEVICE_SUSPEND_REASON_POWEROFF:
      poweroff();
      exit(0);
    case DEVICE_SUSPEND_REASON_SUSPEND_RAM:
      return suspend_to_ram();
    default:
      return ZX_ERR_NOT_SUPPORTED;
  };
}

zx_status_t publish_acpi_devices(zx_device_t* parent, zx_device_t* sys_root,
                                 zx_device_t* acpi_root_) {
  zx_status_t status = pwrbtn_init(acpi_root_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi: failed to initialize pwrbtn device: %d\n", status);
  }

  // Walk the ACPI namespace for devices and publish them
  // Only publish a single PCI device
  AcpiWalker walker{sys_root, acpi_root_, parent};
  ACPI_STATUS acpi_status = AcpiWalkNamespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
                                              MAX_NAMESPACE_DEPTH, &AcpiWalker::OnDescentCallback,
                                              &AcpiWalker::OnAscentCallback, &walker, nullptr);
  if (acpi_status != AE_OK) {
    return ZX_ERR_BAD_STATE;
  } else {
    return ZX_OK;
  }
}
