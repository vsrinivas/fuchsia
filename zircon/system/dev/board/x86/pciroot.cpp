// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include <endian.h>

#include <acpica/acpi.h>
#include <ddk/debug.h>
#include <ddk/protocol/auxdata.h>
#include <ddk/protocol/pciroot.h>
#include <ddk/protocol/sysmem.h>
#include <pci/pio.h>
#include <zircon/hw/i2c.h>
#include <zircon/types.h>

#include "acpi-private.h"
#include "dev.h"
#include "errors.h"
#include "iommu.h"
#include "pci.h"
#include "pciroot.h"

static ACPI_STATUS find_pci_child_callback(ACPI_HANDLE object,
                                           uint32_t nesting_level,
                                           void* context,
                                           void** out_value) {
    ACPI_DEVICE_INFO* info;
    ACPI_STATUS acpi_status = AcpiGetObjectInfo(object, &info);
    if (acpi_status != AE_OK) {
        zxlogf(TRACE, "bus-acpi: AcpiGetObjectInfo failed %d\n", acpi_status);
        return acpi_status;
    }
    ACPI_FREE(info);
    ACPI_OBJECT obj = {
        .Type = ACPI_TYPE_INTEGER,
    };
    ACPI_BUFFER buffer = {
        .Length = sizeof(obj),
        .Pointer = &obj,
    };
    acpi_status = AcpiEvaluateObject(object, (char*)"_ADR", NULL, &buffer);
    if (acpi_status != AE_OK) {
        return AE_OK;
    }
    uint32_t addr = *(uint32_t*)context;
    ACPI_HANDLE* out_handle = (ACPI_HANDLE*)out_value;
    if (addr == obj.Integer.Value) {
        *out_handle = object;
        return AE_CTRL_TERMINATE;
    } else {
        return AE_OK;
    }
}

static ACPI_STATUS pci_child_data_resources_callback(ACPI_RESOURCE* res, void* context) {
    pci_child_auxdata_ctx_t* ctx = (pci_child_auxdata_ctx_t*)context;
    auxdata_i2c_device_t* child = ctx->data + ctx->i;

    if (res->Type != ACPI_RESOURCE_TYPE_SERIAL_BUS) {
        return AE_NOT_FOUND;
    }
    if (res->Data.I2cSerialBus.Type != ACPI_RESOURCE_SERIAL_TYPE_I2C) {
        return AE_NOT_FOUND;
    }

    ACPI_RESOURCE_I2C_SERIALBUS* i2c = &res->Data.I2cSerialBus;
    child->bus_master = i2c->SlaveMode;
    child->ten_bit = i2c->AccessMode;
    child->address = i2c->SlaveAddress;
    child->bus_speed = i2c->ConnectionSpeed;

    return AE_CTRL_TERMINATE;
}

static ACPI_STATUS pci_child_data_callback(ACPI_HANDLE object,
                                           uint32_t nesting_level,
                                           void* context,
                                           void** out_value) {
    pci_child_auxdata_ctx_t* ctx = (pci_child_auxdata_ctx_t*)context;
    if ((ctx->i + 1) > ctx->max) {
        return AE_CTRL_TERMINATE;
    }

    auxdata_i2c_device_t* data = ctx->data + ctx->i;
    data->protocol_id = ZX_PROTOCOL_I2C;

    ACPI_DEVICE_INFO* info = NULL;
    ACPI_STATUS acpi_status = AcpiGetObjectInfo(object, &info);
    if (acpi_status == AE_OK) {
        // These length fields count the trailing NUL.
        // Publish HID
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
        ACPI_FREE(info);
    }
    ZX_ASSERT(data->propcount <= AUXDATA_MAX_DEVPROPS);

    // call _CRS to get i2c info
    acpi_status = AcpiWalkResources(object, (char*)"_CRS", pci_child_data_resources_callback, ctx);
    if ((acpi_status == AE_OK) || (acpi_status == AE_CTRL_TERMINATE)) {
        ctx->i++;
    }
    return AE_OK;
}

static zx_status_t pciroot_op_get_auxdata(void* context,
                                          const char* args,
                                          void* data,
                                          size_t bytes,
                                          size_t* actual) {
    acpi_device_t* dev = (acpi_device_t*)context;

    char type[16];
    uint32_t bus_id, dev_id, func_id;
    int n;
    if ((n = sscanf(args, "%[^,],%02x:%02x:%02x", type, &bus_id, &dev_id, &func_id)) != 4) {
        return ZX_ERR_INVALID_ARGS;
    }

    zxlogf(SPEW, "bus-acpi: get_auxdata type '%s' device %02x:%02x:%02x\n", type, bus_id, dev_id,
           func_id);

    if (strcmp(type, "i2c-child")) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    if (bytes < (2 * sizeof(uint32_t))) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    ACPI_HANDLE pci_node = NULL;
    uint32_t addr = (dev_id << 16) | func_id;

    // Look for the child node with this device and function id
    ACPI_STATUS acpi_status = AcpiWalkNamespace(ACPI_TYPE_DEVICE, dev->ns_node, 1,
                                                find_pci_child_callback, NULL, &addr, &pci_node);
    if ((acpi_status != AE_OK) && (acpi_status != AE_CTRL_TERMINATE)) {
        return acpi_to_zx_status(acpi_status);
    }
    if (pci_node == NULL) {
        return ZX_ERR_NOT_FOUND;
    }

    memset(data, 0, bytes);

    // Look for as many children as can fit in the provided buffer
    pci_child_auxdata_ctx_t ctx = {
        .max = static_cast<uint8_t>(bytes / sizeof(auxdata_i2c_device_t)),
        .i = 0,
        .data = static_cast<auxdata_i2c_device_t*>(data),
    };

    acpi_status =
        AcpiWalkNamespace(ACPI_TYPE_DEVICE, pci_node, 1, pci_child_data_callback, NULL, &ctx, NULL);
    if ((acpi_status != AE_OK) && (acpi_status != AE_CTRL_TERMINATE)) {
        *actual = 0;
        return acpi_to_zx_status(acpi_status);
    }

    *actual = ctx.i * sizeof(auxdata_i2c_device_t);

    zxlogf(SPEW, "bus-acpi: get_auxdata '%s' %u devs actual %zu\n", args, ctx.i, *actual);

    return ZX_OK;
}

static zx_status_t pciroot_op_get_bti(void* context,
                                      uint32_t bdf,
                                      uint32_t index,
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
    acpi_device_t* dev = (acpi_device_t*)context;
    sysmem_protocol_t sysmem;
    zx_status_t status = device_get_protocol(dev->platform_bus, ZX_PROTOCOL_SYSMEM, &sysmem);
    if (status != ZX_OK) {
        zx_handle_close(handle);
        return status;
    }
    return sysmem_connect(&sysmem, handle);
}

#ifdef ENABLE_USER_PCI
zx_status_t Pciroot::PcirootGetAuxdata(const char* args, void* data, size_t bytes, size_t* actual) {
    return pciroot_op_get_auxdata(c_context(), args, data, bytes, actual);
}

zx_status_t Pciroot::PcirootGetBti(uint32_t bdf, uint32_t index, zx::bti* bti) {
    return pciroot_op_get_bti(c_context(), bdf, index, bti->reset_and_get_address());
}

zx_status_t Pciroot::PcirootConnectSysmem(zx::handle handle) {
    sysmem_protocol_t sysmem;
    zx_status_t status = device_get_protocol(platform_bus_, ZX_PROTOCOL_SYSMEM, &sysmem);
    if (status != ZX_OK) {
        return status;
    }
    return sysmem_connect(&sysmem, handle.release());
}

zx_status_t Pciroot::PcirootGetPciPlatformInfo(pci_platform_info_t* info) {
    *info = ctx_->info;
    return ZX_OK;
}

zx_status_t Pciroot::PcirootGetPciIrqInfo(pci_irq_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
}

bool Pciroot::PcirootDriverShouldProxyConfig(void) {
    // If we have no mcfg then all config access will need to be through IOports which
    // are proxied over pciroot.
    return !pci_platform_has_mcfg();
}

zx_status_t Pciroot::PcirootConfigRead8(const pci_bdf_t* address, uint16_t offset, uint8_t* value) {
    return pci_pio_read8(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t Pciroot::PcirootConfigRead16(const pci_bdf_t* address,
                                         uint16_t offset,
                                         uint16_t* value) {
    return pci_pio_read16(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t Pciroot::PcirootConfigRead32(const pci_bdf_t* address,
                                         uint16_t offset,
                                         uint32_t* value) {
    return pci_pio_read32(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t Pciroot::PcirootConfigWrite8(const pci_bdf_t* address, uint16_t offset, uint8_t value) {
    return pci_pio_write8(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t Pciroot::PcirootConfigWrite16(const pci_bdf_t* address,
                                          uint16_t offset,
                                          uint16_t value) {
    return pci_pio_write16(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t Pciroot::PcirootConfigWrite32(const pci_bdf_t* address,
                                          uint16_t offset,
                                          uint32_t value) {
    return pci_pio_write32(*address, static_cast<uint8_t>(offset), value);
}

zx_status_t Pciroot::PcirootAllocMsiBlock(uint64_t requested_irqs,
                                          bool can_target_64bit,
                                          msi_block_t* out_block) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Pciroot::PcirootFreeMsiBlock(const msi_block_t* block) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Pciroot::PcirootMaskUnmaskMsi(uint64_t msi_id, bool mask) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Pciroot::PcirootGetAddressSpace(size_t len,
                                            pci_address_space_t type,
                                            bool low,
                                            uint64_t* out_base) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Pciroot::PcirootFreeAddressSpace(uint64_t base, size_t len, pci_address_space_t type) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Pciroot::Create(fbl::unique_ptr<pciroot_ctx_t> ctx,
                            zx_device_t* parent,
                            zx_device_t* platform_bus,
                            const char* name) {
    fbl::AllocChecker ac;
    auto pciroot = new (&ac) Pciroot(std::move(ctx), parent, platform_bus, name);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    return pciroot->DdkAdd(name);
}

#else  // TODO(cja): remove after the switch to userspace pci
static zx_status_t pciroot_op_get_pci_platform_info(void*, pci_platform_info_t*) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t pciroot_op_get_pci_irq_info(void*, pci_irq_info_t*) {
    return ZX_ERR_NOT_SUPPORTED;
}

static bool pciroot_op_driver_should_proxy_config(void* ctx) {
    return false;
}

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

static zx_status_t pciroot_op_alloc_msi_block(void*, uint64_t, bool, msi_block_t*) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t pciroot_op_free_msi_block(void*, const msi_block_t*) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t pciroot_op_mask_unmask_msi(void*, uint64_t, bool) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t pciroot_op_get_address_space(void*,
                                                size_t,
                                                pci_address_space_t,
                                                bool,
                                                uint64_t*) {
    return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t pciroot_op_free_address_space(void*, uint64_t, size_t, pci_address_space_t) {
    return ZX_ERR_NOT_SUPPORTED;
}

static pciroot_protocol_ops_t pciroot_proto = {
    .get_auxdata = pciroot_op_get_auxdata,
    .get_bti = pciroot_op_get_bti,
    .connect_sysmem = pciroot_op_connect_sysmem,
    .get_pci_platform_info = pciroot_op_get_pci_platform_info,
    .get_pci_irq_info = pciroot_op_get_pci_irq_info,
    .driver_should_proxy_config = pciroot_op_driver_should_proxy_config,
    .config_read8 = pciroot_op_config_read8,
    .config_read16 = pciroot_op_config_read16,
    .config_read32 = pciroot_op_config_read32,
    .config_write8 = pciroot_op_config_write8,
    .config_write16 = pciroot_op_config_write16,
    .config_write32 = pciroot_op_config_write32,
    .alloc_msi_block = pciroot_op_alloc_msi_block,
    .free_msi_block = pciroot_op_free_msi_block,
    .mask_unmask_msi = pciroot_op_mask_unmask_msi,
    .get_address_space = pciroot_op_get_address_space,
    .free_address_space = pciroot_op_free_address_space,
};

pciroot_protocol_ops_t* get_pciroot_ops(void) {
    return &pciroot_proto;
}

#endif  // ENABLE_USER_PCI
