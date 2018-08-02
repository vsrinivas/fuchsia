// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <acpica/acpi.h>
#include <zircon/types.h>
#include <ddk/debug.h>
#include <ddk/protocol/pciroot.h>

#include "acpi-private.h"
#include "dev.h"
#include "errors.h"
#include "iommu.h"
#include "pciroot.h"

static ACPI_STATUS find_pci_child_callback(ACPI_HANDLE object, uint32_t nesting_level,
                                           void* context, void** out_value) {
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
                                           void* context, void** out_value) {
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
            if (!strncmp(hid, GOOGLE_TPM_HID_STRING, HID_LENGTH)) {
                data->protocol_id = ZX_PROTOCOL_TPM;
            }
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
                    data->protocol_id = ZX_PROTOCOL_I2C_HID;
                }
                data->props[data->propcount].id = BIND_ACPI_CID_0_3;
                data->props[data->propcount++].value = htobe32(*((uint32_t*)(cid->String)));
                data->props[data->propcount].id = BIND_ACPI_CID_4_7;
                data->props[data->propcount++].value = htobe32(*((uint32_t*)(cid->String + 4)));
            }
        }
        ACPI_FREE(info);
    }

    // call _CRS to get i2c info
    acpi_status = AcpiWalkResources(object, (char*)"_CRS",
                                    pci_child_data_resources_callback, ctx);
    if ((acpi_status == AE_OK) || (acpi_status == AE_CTRL_TERMINATE)) {
        ctx->i++;
    }
    return AE_OK;
}

static zx_status_t pciroot_op_get_auxdata(void* context, const char* args,
                                          void* data, uint32_t bytes,
                                          uint32_t* actual) {
    acpi_device_t* dev = (acpi_device_t*)context;

    char type[16];
    uint32_t bus_id, dev_id, func_id;
    int n;
    if ((n = sscanf(args, "%[^,],%02x:%02x:%02x", type, &bus_id, &dev_id, &func_id)) != 4) {
        return ZX_ERR_INVALID_ARGS;
    }

    zxlogf(SPEW, "bus-acpi: get_auxdata type '%s' device %02x:%02x:%02x\n", type,
           bus_id, dev_id, func_id);

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
                                                find_pci_child_callback, NULL,
                                                &addr, &pci_node);
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

    acpi_status = AcpiWalkNamespace(ACPI_TYPE_DEVICE, pci_node, 1,
                                    pci_child_data_callback, NULL, &ctx, NULL);
    if ((acpi_status != AE_OK) && (acpi_status != AE_CTRL_TERMINATE)) {
        *actual = 0;
        return acpi_to_zx_status(acpi_status);
    }

    *actual = static_cast<uint32_t>(ctx.i * sizeof(auxdata_i2c_device_t));

    zxlogf(SPEW, "bus-acpi: get_auxdata '%s' %u devs actual %u\n",
           args, ctx.i, *actual);

    return ZX_OK;
}

static zx_status_t pciroot_op_get_bti(void* context, uint32_t bdf, uint32_t index,
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

static pciroot_protocol_ops_t pciroot_proto = {
    .get_auxdata = pciroot_op_get_auxdata,
    .get_bti = pciroot_op_get_bti,
};

pciroot_protocol_ops_t* get_pciroot_ops(void) {
    return &pciroot_proto;
}
