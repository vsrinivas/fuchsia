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
#include "util.h"

namespace acpi {

fitx::result<ACPI_STATUS, UniquePtr<ACPI_DEVICE_INFO>> GetObjectInfo(ACPI_HANDLE obj) {
  ACPI_DEVICE_INFO* raw = nullptr;
  ACPI_STATUS acpi_status = AcpiGetObjectInfo(obj, &raw);
  UniquePtr<ACPI_DEVICE_INFO> ret{raw};

  if (ACPI_SUCCESS(acpi_status)) {
    return fitx::ok(std::move(ret));
  }

  return fitx::error(acpi_status);
}

}  // namespace acpi

ACPI_STATUS AcpiDevice::AddResource(ACPI_RESOURCE* res) {
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

zx_status_t AcpiDevice::ReportCurrentResources() {
  if (got_resources_) {
    return ZX_OK;
  }

  // call _CRS to fill in resources
  ACPI_STATUS acpi_status = AcpiWalkResources(
      acpi_handle_, (char*)"_CRS",
      [](ACPI_RESOURCE* res, void* ctx) {
        return reinterpret_cast<AcpiDevice*>(ctx)->AddResource(res);
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

zx_status_t AcpiDevice::AcpiGetPio(uint32_t index, zx::resource* out_pio) {
  fbl::AutoLock<fbl::Mutex> guard{&lock_};
  zx_status_t st = ReportCurrentResources();
  if (st != ZX_OK) {
    return st;
  }

  if (index >= pio_resources_.size()) {
    return ZX_ERR_NOT_FOUND;
  }

  const AcpiDevicePioResource& res = pio_resources_[index];

  // Please do not use get_root_resource() in new code. See ZX-1467.
  // TODO: figure out what to pass to name here
  return zx::resource::create(*zx::unowned_resource{get_root_resource()}, ZX_RSRC_KIND_IOPORT,
                              res.base_address, res.address_length, device_get_name(zxdev_), 0,
                              out_pio);
}

zx_status_t AcpiDevice::AcpiGetMmio(uint32_t index, acpi_mmio* out_mmio) {
  fbl::AutoLock<fbl::Mutex> guard{&lock_};
  zx_status_t st = ReportCurrentResources();
  if (st != ZX_OK) {
    return st;
  }

  if (index >= mmio_resources_.size()) {
    return ZX_ERR_NOT_FOUND;
  }

  const AcpiDeviceMmioResource& res = mmio_resources_[index];
  if (((res.base_address & (PAGE_SIZE - 1)) != 0) ||
      ((res.address_length & (PAGE_SIZE - 1)) != 0)) {
    zxlogf(ERROR, "acpi-bus[%s]: memory id=%d addr=0x%08x len=0x%x is not page aligned",
           device_get_name(zxdev_), index, res.base_address, res.address_length);
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

zx_status_t AcpiDevice::AcpiMapInterrupt(int64_t which_irq, zx::interrupt* out_handle) {
  fbl::AutoLock<fbl::Mutex> guard{&lock_};
  zx_status_t st = ReportCurrentResources();
  if (st != ZX_OK) {
    return st;
  }

  if ((uint)which_irq >= irqs_.size()) {
    return ZX_ERR_NOT_FOUND;
  }

  const AcpiDeviceIrqResource& irq = irqs_[which_irq];
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
  // Please do not use get_root_resource() in new code. See ZX-1467.
  return zx::interrupt::create(*zx::unowned_resource{get_root_resource()}, irq.pin,
                               ZX_INTERRUPT_REMAP_IRQ | mode, out_handle);
}

zx_status_t AcpiDevice::AcpiGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
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

zx_status_t AcpiDevice::AcpiConnectSysmem(zx::channel connection) {
  fbl::AutoLock<fbl::Mutex> guard{&lock_};
  sysmem_protocol_t sysmem;
  zx_status_t st = device_get_protocol(platform_bus_, ZX_PROTOCOL_SYSMEM, &sysmem);
  if (st != ZX_OK) {
    return st;
  }
  return sysmem_connect(&sysmem, connection.release());
}

zx_status_t AcpiDevice::AcpiRegisterSysmemHeap(uint64_t heap, zx::channel connection) {
  fbl::AutoLock<fbl::Mutex> guard{&lock_};
  sysmem_protocol_t sysmem;
  zx_status_t st = device_get_protocol(platform_bus_, ZX_PROTOCOL_SYSMEM, &sysmem);
  if (st != ZX_OK) {
    return st;
  }
  return sysmem_register_heap(&sysmem, heap, connection.release());
}

static const char* hid_from_acpi_devinfo(ACPI_DEVICE_INFO* info) {
  const char* hid = nullptr;
  if ((info->Valid & ACPI_VALID_HID) && (info->HardwareId.Length > 0) &&
      ((info->HardwareId.Length - 1) <= sizeof(uint64_t))) {
    hid = (const char*)info->HardwareId.String;
  }
  return hid;
}

device_add_args_t get_device_add_args(const char* name, ACPI_DEVICE_INFO* info,
                                      std::array<zx_device_prop_t, 4>* out_props) {
  zx_device_prop_t* props = out_props->data();
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

  if (zxlog_level_enabled(TRACE)) {
    // ACPI names are always 4 characters in a uint32
    zxlogf(TRACE, "acpi: got device %s", acpi_name);
    if (info->Valid & ACPI_VALID_HID) {
      zxlogf(TRACE, "     HID=%s", info->HardwareId.String);
    } else {
      zxlogf(TRACE, "     HID=invalid");
    }
    if (info->Valid & ACPI_VALID_ADR) {
      zxlogf(TRACE, "     ADR=0x%" PRIx64 "", (uint64_t)info->Address);
    } else {
      zxlogf(TRACE, "     ADR=invalid");
    }
    if (info->Valid & ACPI_VALID_CID) {
      zxlogf(TRACE, "    CIDS=%d", info->CompatibleIdList.Count);
      for (uint i = 0; i < info->CompatibleIdList.Count; i++) {
        zxlogf(TRACE, "     [%u] %s", i, info->CompatibleIdList.Ids[i].String);
      }
    } else {
      zxlogf(TRACE, "     CID=invalid");
    }
    zxlogf(TRACE, "    devprops:");
    for (int i = 0; i < propcount; i++) {
      zxlogf(TRACE, "     [%d] id=0x%08x value=0x%08x", i, props[i].id, props[i].value);
    }
  }

  return {.name = name,
          .props = (propcount > 0) ? props : nullptr,
          .prop_count = static_cast<uint32_t>(propcount)};
}

zx_device_t* AcpiWalker::PublishAcpiDevice(const char* name, ACPI_HANDLE handle,
                                           ACPI_DEVICE_INFO* info) {
  auto device = std::make_unique<AcpiDevice>(acpi_root_, handle, platform_bus_);
  std::array<zx_device_prop_t, 4> props;
  if (zx_status_t status = device->DdkAdd(name, get_device_add_args(name, info, &props));
      status != ZX_OK) {
    zxlogf(ERROR, "acpi: error %d in DdkAdd, parent=%s(%p)", status, device_get_name(acpi_root_),
           acpi_root_);
    return nullptr;
  } else {
    zxlogf(INFO, "acpi: published device %s(%p), parent=%s(%p), handle=%p", name, device.get(),
           device_get_name(acpi_root_), acpi_root_, device->acpi_handle());
    // device_add takes ownership of device, but only on success.
    return device.release()->zxdev();
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
          zxlogf(DEBUG, "acpi: Ignoring wrong type 0x%x", ref->Type);
        } else {
          zxlogf(DEBUG, "acpi: Enabling HID controller at I2C0.H00A._PR0[%u]", i);
          acpi_status = AcpiEvaluateObject(ref->Reference.Handle, (char*)"_ON", nullptr, nullptr);
          if (acpi_status != AE_OK) {
            zxlogf(ERROR, "acpi: acpi error 0x%x in I2C0._PR0._ON", acpi_status);
          }
        }
      }
      AcpiOsFree(buffer.Pointer);
    }
  }
  // Acer workaround: Turn on the HID controller.
  else if (!memcmp(&info->Name, "I2C1", 4)) {
    zxlogf(DEBUG, "acpi: Enabling HID controller at I2C1");
    acpi_status = AcpiEvaluateObject(object, (char*)"_PS0", nullptr, nullptr);
    if (acpi_status != AE_OK) {
      zxlogf(ERROR, "acpi: acpi error in I2C1._PS0: 0x%x", acpi_status);
    }
  }
}

ACPI_STATUS AcpiWalker::OnDescent(ACPI_HANDLE object) {
  acpi::UniquePtr<ACPI_DEVICE_INFO> info;
  if (auto res = acpi::GetObjectInfo(object); res.is_error()) {
    return res.error_value();
  } else {
    info = std::move(res.value());
  }

  acpi_apply_workarounds(object, info.get());

  // Is this an Intel HDA audio device?  If so, attempt to find the NHLT and publish it as
  // metadata for the driver to pick up later on.
  //
  // TODO(fxb/56832): Remove this when we have a better way to manage driver
  // dependencies on ACPI.
  constexpr uint32_t kHDAS_Id = make_fourcc('H', 'D', 'A', 'S');
  if (info->Name == kHDAS_Id) {
    // We must have already seen at least one PCI root due to traversal order.
    if (last_pci_ == kNoLastPci) {
      zxlogf(ERROR, "acpi: Found HDAS node, but no prior PCI root was discovered!");
    } else if (!(info->Valid & ACPI_VALID_ADR)) {
      zxlogf(ERROR, "acpi: no valid ADR found for HDA device");
    } else {
      // Attaching metadata to the HDAS device /dev/sys/pci/...
      zx_status_t status =
          nhlt_publish_metadata(sys_root_, last_pci_, (uint64_t)info->Address, object);
      if ((status != ZX_OK) && (status != ZX_ERR_NOT_FOUND)) {
        zxlogf(ERROR, "acpi: failed to publish NHLT metadata");
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
    PublishAcpiDevice("i8042", object, info.get());
  } else if (!memcmp(hid, RTC_HID_STRING, HID_LENGTH) ||
             (cid && !memcmp(cid, RTC_HID_STRING, HID_LENGTH))) {
    PublishAcpiDevice("rtc", object, info.get());
  } else if (!memcmp(hid, GOLDFISH_PIPE_HID_STRING, HID_LENGTH)) {
    PublishAcpiDevice("goldfish", object, info.get());
  } else if (!memcmp(hid, SERIAL_HID_STRING, HID_LENGTH)) {
    PublishAcpiDevice("serial", object, info.get());
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
    zxlogf(ERROR, "acpi: failed to initialize pwrbtn device: %d", status);
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
