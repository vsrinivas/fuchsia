// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/acpi/c/banjo.h>
#include <fuchsia/hardware/pciroot/c/banjo.h>
#include <fuchsia/hardware/sysmem/c/banjo.h>
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
#include <fbl/auto_lock.h>

#include "acpi-private.h"
#include "acpi.h"
#include "dev.h"
#include "errors.h"
#include "i2c.h"
#include "methods.h"
#include "nhlt.h"
#include "pci.h"
#include "power.h"
#include "resources.h"
#include "src/devices/lib/iommu/iommu.h"
#include "sysmem.h"
#include "util.h"

namespace {

const std::string_view hid_from_acpi_devinfo(const ACPI_DEVICE_INFO& info) {
  if ((info.Valid & ACPI_VALID_HID) && (info.HardwareId.Length > 0) &&
      ((info.HardwareId.Length - 1) <= sizeof(uint64_t))) {
    // ACPICA string lengths include the NULL terminator.
    return std::string_view{info.HardwareId.String, info.HardwareId.Length - 1};
  }

  return std::string_view{};
}

const std::string_view cid_from_acpi_devinfo(const ACPI_DEVICE_INFO& info) {
  if ((info.Valid & ACPI_VALID_CID) && (info.CompatibleIdList.Count > 0) &&
      (info.CompatibleIdList.Ids[0].Length > 0)) {
    // ACPICA string lengths include the NULL terminator.
    return std::string_view{info.CompatibleIdList.Ids[0].String,
                            info.CompatibleIdList.Ids[0].Length - 1};
  }

  return std::string_view{};
}

void acpi_apply_workarounds(ACPI_HANDLE object, ACPI_DEVICE_INFO* info) {
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

// A small lambda helper we will use in order to publish generic ACPI devices.
zx_device_t* PublishAcpiDevice(zx_device_t* acpi_root, zx_device_t* platform_bus, const char* name,
                               ACPI_HANDLE handle, ACPI_DEVICE_INFO* info) {
  auto device = std::make_unique<acpi::Device>(acpi_root, handle, platform_bus);
  std::array<zx_device_prop_t, 4> props;
  if (zx_status_t status = device->DdkAdd(name, get_device_add_args(name, info, &props));
      status != ZX_OK) {
    zxlogf(ERROR, "acpi: error %d in DdkAdd, parent=%s(%p)", status, device_get_name(acpi_root),
           acpi_root);
    return nullptr;
  } else {
    zxlogf(INFO, "acpi: published device %s(%p), parent=%s(%p), handle=%p", name, device.get(),
           device_get_name(acpi_root), acpi_root, device->acpi_handle());
    // device_add takes ownership of device, but only on success.
    return device.release()->zxdev();
  }
}

// A small helper class we can use to track the BBN (either "Base Bus
// Number" or "Bios Bus Number") of the last PCI bus node we encountered while
// walking the ACPI namespace.
class LastPciBbnTracker {
 public:
  LastPciBbnTracker() = default;

  // If we are ascending through the level where we noticed a valid PCI BBN,
  // then we are no longer valid.
  void Ascend(uint32_t level) {
    if (valid_ && (level == level_)) {
      valid_ = false;
    }
  }

  zx_status_t Descend(uint32_t level, ACPI_HANDLE object, const ACPI_DEVICE_INFO& obj_info) {
    // Are we descending into a device node which has a hardware ID, and does
    // that hardware ID indicate a PCI/PCIe bus?  If so, try to extract the base
    // bus number and stash it as our last seen PCI bus number.
    const std::string_view hid = hid_from_acpi_devinfo(obj_info);
    if ((hid == PCI_EXPRESS_ROOT_HID_STRING) || (hid == PCI_ROOT_HID_STRING)) {
      uint8_t bbn;
      zx_status_t status = acpi_bbn_call(object, &bbn);

      if (status == ZX_ERR_NOT_FOUND) {
        zxlogf(WARNING, "acpi: PCI/PCIe device \"%s\" missing _BBN entry, defaulting to 0",
               fourcc_to_string(obj_info.Name).str);
        bbn = 0;
        return ZX_OK;
      } else if (status != ZX_OK) {
        zxlogf(ERROR, "acpi: failed to fetch BBN for PCI/PCIe device \"%s\"",
               fourcc_to_string(obj_info.Name).str);
        return ZX_ERR_BAD_STATE;
      }

      if (valid_) {
        zxlogf(ERROR,
               "acpi: Nested PCI roots detected when descending into PCI/PCIe device \"%s\" (prev "
               "bbn %u at level %u, child bbn %u at "
               "level %u",
               fourcc_to_string(obj_info.Name).str, bbn_, level_, bbn, level);
        return ZX_ERR_BAD_STATE;
      }

      valid_ = true;
      level_ = level;
      bbn_ = bbn;
    }

    return ZX_OK;
  }

  bool has_value() const { return valid_; }
  uint8_t bbn() const {
    ZX_DEBUG_ASSERT(valid_);
    return bbn_;
  }

 private:
  bool valid_ = false;
  uint32_t level_ = 0;
  uint8_t bbn_ = 0;
};

}  // namespace

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

  if ((uint)which_irq >= irqs_.size()) {
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

device_add_args_t get_device_add_args(const char* name, ACPI_DEVICE_INFO* info,
                                      std::array<zx_device_prop_t, 4>* out_props) {
  uint32_t propcount = 0;

  // Publish HID, and the first CID (if present), in device props
  if (zx_status_t status = acpi::ExtractHidToDevProps(*info, *out_props, propcount);
      status != ZX_OK) {
    zxlogf(WARNING, "Failed to extract HID into dev_props for acpi device \"%s\" (status %d)\n",
           fourcc_to_string(info->Name).str, status);
  }
  if (zx_status_t status = acpi::ExtractCidToDevProps(*info, *out_props, propcount);
      status != ZX_OK) {
    zxlogf(WARNING, "Failed to extract CID into dev_props for acpi device \"%s\" (status %d)\n",
           fourcc_to_string(info->Name).str, status);
  }

  if (zxlog_level_enabled(TRACE)) {
    // ACPI names are always 4 characters in a uint32
    zxlogf(TRACE, "acpi: got device %s", fourcc_to_string(info->Name).str);
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
    for (size_t i = 0; i < propcount; i++) {
      zxlogf(TRACE, "     [%zu] id=0x%08x value=0x%08x", i, (*out_props)[i].id,
             (*out_props)[i].value);
    }
  }

  return {.name = name,
          .props = (propcount > 0) ? out_props->data() : nullptr,
          .prop_count = propcount};
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

zx_status_t publish_acpi_devices(zx_device_t* platform_bus, zx_device_t* sys_root,
                                 zx_device_t* acpi_root) {
  zx_status_t status = pwrbtn_init(acpi_root);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi: failed to initialize pwrbtn device: %d", status);
  }

  // Walk the devices in the ACPI tree, handling any device specific quirks as
  // we go, and publishing any static metadata we need to publish before
  // publishing any devices.
  //
  // TODO(fxbug.dev/56832): Remove this pass when we have a better way to manage
  // driver dependencies on ACPI.  Once drivers can access their metadata
  // directly via a connection to the ACPI driver, we will not need to bother
  // with publishing static metadata before we publish devices.
  LastPciBbnTracker last_pci_bbn;

  ACPI_STATUS acpi_status;
  acpi_status = acpi::WalkNamespace(
      ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, MAX_NAMESPACE_DEPTH,
      [sys_root, &last_pci_bbn](ACPI_HANDLE object, uint32_t level,
                                acpi::WalkDirection dir) -> ACPI_STATUS {
        // If we are ascending, tell our PciBbn tracker so that it can properly
        // invalidate our last BBN when needed.
        if (dir == acpi::WalkDirection::Ascending) {
          last_pci_bbn.Ascend(level);
          return AE_OK;
        }

        // We are descending.  Grab our object info.
        acpi::UniquePtr<ACPI_DEVICE_INFO> info;
        if (auto res = acpi::GetObjectInfo(object); res.is_error()) {
          return res.error_value();
        } else {
          info = std::move(res.value());
        }

        // Apply any workarounds for quirks.
        acpi_apply_workarounds(object, info.get());

        // If this is a PCI node we are passing through, track it's BBN.  We
        // will need it in order to publish metadata for the devices we
        // encounter.  If we encounter a fatal condition, terminate the walk.
        if (last_pci_bbn.Descend(level, object, *info) != ZX_OK) {
          return AE_ERROR;
        }

        // Is this an HDAS (Intel HDA audio controller) or I2Cx (I2C bus) device node
        // under PCI?  If so, attempt to publish their relevant metadata so that the
        // device driver can access it when the PCI device itself is finally
        // published.
        //
        // TODO(fxbug.dev/56832): Remove this when we have a better way to manage driver
        // dependencies on ACPI.
        constexpr uint32_t kMAXL_Id = make_fourcc('M', 'A', 'X', 'L');
        constexpr uint32_t kMAXR_Id = make_fourcc('M', 'A', 'X', 'R');
        constexpr uint32_t kRT53_Id = make_fourcc('R', 'T', '5', '3');
        constexpr uint32_t kRT54_Id = make_fourcc('R', 'T', '5', '4');
        constexpr uint32_t kHDAS_Id = make_fourcc('H', 'D', 'A', 'S');
        constexpr uint32_t kI2Cx_Id = make_fourcc('I', '2', 'C', 0);
        constexpr uint32_t kI2Cx_Mask = make_fourcc(0xFF, 0xFF, 0xFF, 0x00);

        if ((info->Name == kMAXL_Id) || (info->Name == kMAXR_Id) || (info->Name == kRT53_Id) ||
            (info->Name == kRT54_Id) || (info->Name == kHDAS_Id) ||
            ((info->Name & kI2Cx_Mask) == kI2Cx_Id)) {
          // We must have already seen at least one PCI root due to traversal order.
          if (!last_pci_bbn.has_value()) {
            zxlogf(WARNING,
                   "acpi: Found HDAS/I2Cx node (\"%s\"), but no prior PCI root was discovered!",
                   fourcc_to_string(info->Name).str);
          } else if (!(info->Valid & ACPI_VALID_ADR)) {
            zxlogf(WARNING, "acpi: no valid ADR found for device \"%s\"",
                   fourcc_to_string(info->Name).str);
          } else {
            if (info->Name == kHDAS_Id) {
              // Attaching metadata to the HDAS device /dev/sys/pci/...
              zx_status_t status = nhlt_publish_metadata(
                  sys_root, last_pci_bbn.bbn(), static_cast<uint64_t>(info->Address), object);
              if ((status != ZX_OK) && (status != ZX_ERR_NOT_FOUND)) {
                zxlogf(ERROR, "acpi: failed to publish NHLT metadata");
              }
            } else {
              // Attaching metadata to the I2Cx device /dev/sys/pci/...
              zx_status_t status =
                  I2cBusPublishMetadata(sys_root, last_pci_bbn.bbn(),
                                        static_cast<uint64_t>(info->Address), *info, object);
              if ((status != ZX_OK) && (status != ZX_ERR_NOT_FOUND)) {
                zxlogf(ERROR, "acpi: failed to publish I2C metadata");
              }
            }
          }
        }

        return AE_OK;
      });

  if (!ACPI_SUCCESS(acpi_status)) {
    zxlogf(WARNING, "acpi: Error (%d) during fixup and metadata pass", acpi_status);
  }

  // Now walk the ACPI namespace looking for devices we understand, and publish
  // them.  For now, publish only the first PCI bus we encounter.
  bool published_pci_bus = false;
  acpi_status = acpi::WalkNamespace(
      ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, MAX_NAMESPACE_DEPTH,
      [sys_root, acpi_root, platform_bus, &published_pci_bus](
          ACPI_HANDLE object, uint32_t level, acpi::WalkDirection dir) -> ACPI_STATUS {
        // We don't have anything useful to do during the ascent phase.  Just
        // skip it.
        if (dir == acpi::WalkDirection::Ascending) {
          return AE_OK;
        }

        // We are descending.  Grab our object info.
        acpi::UniquePtr<ACPI_DEVICE_INFO> info;
        if (auto res = acpi::GetObjectInfo(object); res.is_error()) {
          return res.error_value();
        } else {
          info = std::move(res.value());
        }

        // Extract pointers to the hardware ID and the compatible ID if present.
        // If there is no hardware ID, just skip the device.
        const std::string_view hid = hid_from_acpi_devinfo(*info);
        const std::string_view cid = cid_from_acpi_devinfo(*info);
        if (hid.empty()) {
          return AE_OK;
        }

        // Now, if we recognize the HID, go ahead and deal with publishing the
        // device.
        if ((hid == PCI_EXPRESS_ROOT_HID_STRING) || (hid == PCI_ROOT_HID_STRING)) {
          if (!published_pci_bus) {
            if (pci_init(sys_root, platform_bus, object, info.get()) == ZX_OK) {
              published_pci_bus = true;
            } else {
              zxlogf(WARNING, "Skipping extra PCI/PCIe bus \"%s\"",
                     fourcc_to_string(info->Name).str);
            }
          }
        } else if (hid == BATTERY_HID_STRING) {
          battery_init(acpi_root, object);
        } else if (hid == LID_HID_STRING) {
          lid_init(acpi_root, object);
        } else if (hid == PWRSRC_HID_STRING) {
          pwrsrc_init(acpi_root, object);
        } else if (hid == EC_HID_STRING) {
          ec_init(acpi_root, object);
        } else if (hid == GOOGLE_TBMC_HID_STRING) {
          tbmc_init(acpi_root, object);
        } else if (hid == GOOGLE_CROS_EC_HID_STRING) {
          cros_ec_lpc_init(acpi_root, object);
        } else if (hid == DPTF_THERMAL_HID_STRING) {
          thermal_init(acpi_root, info.get(), object);
        } else if ((hid == I8042_HID_STRING) || (cid == I8042_HID_STRING)) {
          PublishAcpiDevice(acpi_root, platform_bus, "i8042", object, info.get());
        } else if ((hid == RTC_HID_STRING) || (cid == RTC_HID_STRING)) {
          PublishAcpiDevice(acpi_root, platform_bus, "rtc", object, info.get());
        } else if (hid == GOLDFISH_PIPE_HID_STRING) {
          PublishAcpiDevice(acpi_root, platform_bus, "goldfish", object, info.get());
        } else if (hid == GOLDFISH_SYNC_HID_STRING) {
          PublishAcpiDevice(acpi_root, platform_bus, "goldfish-sync", object, info.get());
        } else if (hid == SERIAL_HID_STRING) {
          PublishAcpiDevice(acpi_root, platform_bus, "serial", object, info.get());
        }
        return AE_OK;
      });

  if (acpi_status != AE_OK) {
    return ZX_ERR_BAD_STATE;
  } else {
    return ZX_OK;
  }
}
