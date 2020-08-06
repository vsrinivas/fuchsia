// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <endian.h>
#include <inttypes.h>
#include <lib/pci/pio.h>
#include <zircon/compiler.h>
#include <zircon/hw/i2c.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <array>
#include <memory>

#include <acpica/acpi.h>
#include <ddk/debug.h>
#include <ddk/protocol/auxdata.h>
#include <ddk/protocol/sysmem.h>

#include "acpi-private.h"
#include "dev.h"
#include "errors.h"
#include "iommu.h"
#include "pci.h"
#include "pci_allocators.h"

static ACPI_STATUS find_pci_child_callback(ACPI_HANDLE object, uint32_t /* nesting_level */,
                                           void* context, void** out_value) {
  acpi::UniquePtr<ACPI_DEVICE_INFO> info;
  if (auto res = acpi::GetObjectInfo(object); res.is_error()) {
    zxlogf(DEBUG, "bus-acpi: acpi::GetObjectInfo failed %d", res.error_value());
    return res.error_value();
  } else {
    info = std::move(res.value());
  }

  ACPI_OBJECT obj = {
      .Type = ACPI_TYPE_INTEGER,
  };
  ACPI_BUFFER buffer = {
      .Length = sizeof(obj),
      .Pointer = &obj,
  };
  ACPI_STATUS acpi_status = AcpiEvaluateObject(object, const_cast<char*>("_ADR"), nullptr, &buffer);
  if (acpi_status != AE_OK) {
    return AE_OK;
  }
  uint32_t addr = *static_cast<uint32_t*>(context);
  auto* out_handle = static_cast<ACPI_HANDLE*>(out_value);
  if (addr == obj.Integer.Value) {
    *out_handle = object;
    return AE_CTRL_TERMINATE;
  }
  return AE_OK;
}

static ACPI_STATUS pci_child_data_resources_callback(ACPI_RESOURCE* res, void* context) {
  auto* ctx = static_cast<pci_child_auxdata_ctx_t*>(context);
  auxdata_i2c_device_t* child = ctx->data + ctx->i;

  if (res->Type != ACPI_RESOURCE_TYPE_SERIAL_BUS) {
    return AE_NOT_FOUND;
  }
  if (res->Data.I2cSerialBus.Type != ACPI_RESOURCE_SERIAL_TYPE_I2C) {
    return AE_NOT_FOUND;
  }

  ACPI_RESOURCE_I2C_SERIALBUS* i2c = &res->Data.I2cSerialBus;
  child->is_bus_controller = i2c->SlaveMode;
  child->ten_bit = i2c->AccessMode;
  child->address = i2c->SlaveAddress;
  child->bus_speed = i2c->ConnectionSpeed;

  return AE_CTRL_TERMINATE;
}

static ACPI_STATUS pci_child_data_callback(ACPI_HANDLE object, uint32_t /*nesting_level*/,
                                           void* context, void** /*out_value*/) {
  auto* ctx = static_cast<pci_child_auxdata_ctx_t*>(context);
  if ((ctx->i + 1) > ctx->max) {
    return AE_CTRL_TERMINATE;
  }

  auxdata_i2c_device_t* data = ctx->data + ctx->i;
  data->protocol_id = ZX_PROTOCOL_I2C;

  if (auto res = acpi::GetObjectInfo(object); res.is_ok()) {
    // These length fields count the trailing NUL.
    // Publish HID
    auto& info = res.value();
    if ((info->Valid & ACPI_VALID_HID) && info->HardwareId.Length <= HID_LENGTH + 1) {
      const char* hid = info->HardwareId.String;
      data->props[data->propcount].id = BIND_ACPI_HID_0_3;
      data->props[data->propcount++].value = htobe32(*((uint32_t*)(hid)));
      data->props[data->propcount].id = BIND_ACPI_HID_4_7;
      data->props[data->propcount++].value = htobe32(*((uint32_t*)(hid + 4)));
    }
    // Check for I2C HID devices via CID
    if ((info->Valid & ACPI_VALID_CID) && info->CompatibleIdList.Count > 0) {
      ACPI_PNP_DEVICE_ID* cid = &info->CompatibleIdList.Ids[0];
      if (cid->Length <= CID_LENGTH + 1) {
        if (!strncmp(cid->String, I2C_HID_CID_STRING, CID_LENGTH)) {
          data->props[data->propcount].id = BIND_I2C_CLASS;
          data->props[data->propcount++].value = I2C_CLASS_HID;
        }
        data->props[data->propcount].id = BIND_ACPI_CID_0_3;
        data->props[data->propcount++].value = htobe32(*((uint32_t*)(cid->String)));
        data->props[data->propcount].id = BIND_ACPI_CID_4_7;
        data->props[data->propcount++].value = htobe32(*((uint32_t*)(cid->String + 4)));
      }
    }
  }
  ZX_ASSERT(data->propcount <= AUXDATA_MAX_DEVPROPS);

  // call _CRS to get i2c info
  ACPI_STATUS acpi_status =
      AcpiWalkResources(object, (char*)"_CRS", pci_child_data_resources_callback, ctx);
  if ((acpi_status == AE_OK) || (acpi_status == AE_CTRL_TERMINATE)) {
    ctx->i++;
  }
  return AE_OK;
}

static zx_status_t pciroot_op_get_auxdata(void* context, const char* args, void* data, size_t bytes,
                                          size_t* actual) {
  auto* dev = static_cast<acpi::Device*>(context);

  std::array<char, 16> type = {};
  uint32_t bus_id, dev_id, func_id;
  int n;
  if ((n = sscanf(args, "%[^,],%02x:%02x:%02x", type.data(), &bus_id, &dev_id, &func_id)) != 4) {
    return ZX_ERR_INVALID_ARGS;
  }

  zxlogf(TRACE, "bus-acpi: get_auxdata type '%s' device %02x:%02x:%02x", type.data(), bus_id,
         dev_id, func_id);

  if (strcmp(type.data(), "i2c-child") != 0) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (bytes < (2 * sizeof(uint32_t))) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  ACPI_HANDLE pci_node = nullptr;
  uint32_t addr = (dev_id << 16) | func_id;

  // Look for the child node with this device and function id
  ACPI_STATUS acpi_status = AcpiWalkNamespace(ACPI_TYPE_DEVICE, dev->acpi_handle(), 1,
                                              find_pci_child_callback, nullptr, &addr, &pci_node);
  if ((acpi_status != AE_OK) && (acpi_status != AE_CTRL_TERMINATE)) {
    return acpi_to_zx_status(acpi_status);
  }
  if (pci_node == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }

  memset(data, 0, bytes);

  // Look for as many children as can fit in the provided buffer
  pci_child_auxdata_ctx_t ctx = {
      .max = static_cast<uint8_t>(bytes / sizeof(auxdata_i2c_device_t)),
      .i = 0,
      .data = static_cast<auxdata_i2c_device_t*>(data),
  };

  acpi_status = AcpiWalkNamespace(ACPI_TYPE_DEVICE, pci_node, 1, pci_child_data_callback, nullptr,
                                  &ctx, nullptr);
  if ((acpi_status != AE_OK) && (acpi_status != AE_CTRL_TERMINATE)) {
    *actual = 0;
    return acpi_to_zx_status(acpi_status);
  }

  *actual = ctx.i * sizeof(auxdata_i2c_device_t);

  zxlogf(TRACE, "bus-acpi: get_auxdata '%s' %u devs actual %zu", args, ctx.i, *actual);

  return ZX_OK;
}

static zx_status_t pciroot_op_get_bti(void* /*context*/, uint32_t bdf, uint32_t index,
                                      zx_handle_t* bti) {
  // The x86 IOMMU world uses PCI BDFs as the hardware identifiers, so there
  // will only be one BTI per device.
  if (index != 0) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  // For dummy IOMMUs, the bti_id just needs to be unique.  For Intel IOMMUs,
  // the bti_ids correspond to PCI BDFs.
  zx_handle_t iommu_handle;
  zx_status_t status = iommu_manager_iommu_for_bdf(bdf, &iommu_handle);
  if (status != ZX_OK) {
    return status;
  }
  return zx_bti_create(iommu_handle, 0, bdf, bti);
}

static zx_status_t pciroot_op_connect_sysmem(void* context, zx_handle_t handle) {
  auto* dev = static_cast<acpi::Device*>(context);
  sysmem_protocol_t sysmem;
  zx_status_t status = device_get_protocol(dev->platform_bus(), ZX_PROTOCOL_SYSMEM, &sysmem);
  if (status != ZX_OK) {
    zx_handle_close(handle);
    return status;
  }
  return sysmem_connect(&sysmem, handle);
}

#ifdef ENABLE_USER_PCI
zx_status_t x64Pciroot::PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
  return pciroot_op_get_bti(nullptr, bdf, index, bti->reset_and_get_address());
}

zx_status_t x64Pciroot::PcirootConnectSysmem(zx::handle handle) {
  sysmem_protocol_t sysmem;
  zx_status_t status = device_get_protocol(context_.platform_bus, ZX_PROTOCOL_SYSMEM, &sysmem);
  if (status != ZX_OK) {
    return status;
  }
  return sysmem_connect(&sysmem, handle.release());
}

zx_status_t x64Pciroot::PcirootGetPciPlatformInfo(pci_platform_info_t* info) {
  *info = context_.info;
  return ZX_OK;
}

zx_status_t x64Pciroot::PcirootConfigRead8(const pci_bdf_t* address, uint16_t offset,
                                           uint8_t* value) {
  return pci_pio_read8(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t x64Pciroot::PcirootConfigRead16(const pci_bdf_t* address, uint16_t offset,
                                            uint16_t* value) {
  return pci_pio_read16(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t x64Pciroot::PcirootConfigRead32(const pci_bdf_t* address, uint16_t offset,
                                            uint32_t* value) {
  return pci_pio_read32(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t x64Pciroot::PcirootConfigWrite8(const pci_bdf_t* address, uint16_t offset,
                                            uint8_t value) {
  return pci_pio_write8(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t x64Pciroot::PcirootConfigWrite16(const pci_bdf_t* address, uint16_t offset,
                                             uint16_t value) {
  return pci_pio_write16(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t x64Pciroot::PcirootConfigWrite32(const pci_bdf_t* address, uint16_t offset,
                                             uint32_t value) {
  return pci_pio_write32(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t x64Pciroot::Create(PciRootHost* root_host, x64Pciroot::Context ctx, zx_device_t* parent,
                               const char* name) {
  auto pciroot = new x64Pciroot(root_host, std::move(ctx), parent, name);
  return pciroot->DdkAdd(name);
}

#else  // TODO(cja): remove after the switch to userspace pci
static zx_status_t pciroot_op_get_pci_platform_info(void*, pci_platform_info_t*) {
  return ZX_ERR_NOT_SUPPORTED;
}

static bool pciroot_op_driver_should_proxy_config(void* /*ctx*/) { return false; }

static zx_status_t pciroot_op_config_read8(void*, const pci_bdf_t*, uint16_t, uint8_t*) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t pciroot_op_config_read16(void*, const pci_bdf_t*, uint16_t, uint16_t*) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t pciroot_op_config_read32(void*, const pci_bdf_t*, uint16_t, uint32_t*) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t pciroot_op_config_write8(void*, const pci_bdf_t*, uint16_t, uint8_t) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t pciroot_op_config_write16(void*, const pci_bdf_t*, uint16_t, uint16_t) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t pciroot_op_config_write32(void*, const pci_bdf_t*, uint16_t, uint32_t) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t pciroot_op_allocate_msi(void*, uint32_t, bool, zx_handle_t*) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t pciroot_op_get_address_space(void*, size_t, zx_paddr_t, pci_address_space_t,
                                                bool, zx_paddr_t*, zx_handle_t*, zx_handle_t*) {
  return ZX_ERR_NOT_SUPPORTED;
}

static pciroot_protocol_ops_t pciroot_proto = {
    .connect_sysmem = pciroot_op_connect_sysmem,
    .get_auxdata = pciroot_op_get_auxdata,
    .get_bti = pciroot_op_get_bti,
    .get_pci_platform_info = pciroot_op_get_pci_platform_info,
    .driver_should_proxy_config = pciroot_op_driver_should_proxy_config,
    .config_read8 = pciroot_op_config_read8,
    .config_read16 = pciroot_op_config_read16,
    .config_read32 = pciroot_op_config_read32,
    .config_write8 = pciroot_op_config_write8,
    .config_write16 = pciroot_op_config_write16,
    .config_write32 = pciroot_op_config_write32,
    .get_address_space = pciroot_op_get_address_space,
    .allocate_msi = pciroot_op_allocate_msi,
};

pciroot_protocol_ops_t* get_pciroot_ops(void) { return &pciroot_proto; }

#endif  // ENABLE_USER_PCI
