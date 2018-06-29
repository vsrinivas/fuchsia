// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/debug.h>
#include <ddk/protocol/acpi.h>
#include <ddk/protocol/pciroot.h>

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>

#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/syscalls/iommu.h>

#include "cpu-trace.h"
#include "dev.h"
#include "errors.h"
#include "init.h"
#include "iommu.h"
#include "nhlt.h"
#include "pci.h"
#include "powerbtn.h"
#include "power.h"
#include "resources.h"

#define MAX_NAMESPACE_DEPTH 100

typedef struct acpi_device_resource {
    bool writeable;
    uint32_t base_address;
    uint32_t alignment;
    uint32_t address_length;
} acpi_device_resource_t;

typedef struct acpi_device_irq {
    uint8_t trigger;
#define ACPI_IRQ_TRIGGER_LEVEL  0
#define ACPI_IRQ_TRIGGER_EDGE   1
    uint8_t polarity;
#define ACPI_IRQ_ACTIVE_HIGH    0
#define ACPI_IRQ_ACTIVE_LOW     1
#define ACPI_IRQ_ACTIVE_BOTH    2
    uint8_t sharable;
#define ACPI_IRQ_EXCLUSIVE      0
#define ACPI_IRQ_SHARED         1
    uint8_t wake_capable;
    uint8_t pin;
} acpi_device_irq_t;

typedef struct acpi_device {
    zx_device_t* zxdev;

    mtx_t lock;

    bool got_resources;

    // memory resources from _CRS
    acpi_device_resource_t* resources;
    size_t resource_count;

    // interrupt resources from _CRS
    acpi_device_irq_t* irqs;
    size_t irq_count;

    // handle to the corresponding ACPI node
    ACPI_HANDLE ns_node;
} acpi_device_t;

typedef struct {
    zx_device_t* parent;
    bool found_pci;
    int last_pci; // bus number of the last PCI root seen
} publish_acpi_device_ctx_t;

typedef struct {
    uint8_t max;
    uint8_t i;
    auxdata_i2c_device_t* data;
} pci_child_auxdata_ctx_t;

zx_handle_t root_resource_handle;

static zx_device_t* publish_device(zx_device_t* parent, ACPI_HANDLE handle,
                                   ACPI_DEVICE_INFO* info, const char* name,
                                   uint32_t protocol_id, void* protocol_ops);

static void acpi_device_release(void* ctx) {
    acpi_device_t* dev = (acpi_device_t*)ctx;
    free(dev);
}

static zx_protocol_device_t acpi_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = acpi_device_release,
};

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
        ctx->i += 1;
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
        .max = bytes / sizeof(auxdata_i2c_device_t),
        .i = 0,
        .data = data,
    };

    acpi_status = AcpiWalkNamespace(ACPI_TYPE_DEVICE, pci_node, 1,
                                    pci_child_data_callback, NULL, &ctx, NULL);
    if ((acpi_status != AE_OK) && (acpi_status != AE_CTRL_TERMINATE)) {
        *actual = 0;
        return acpi_to_zx_status(acpi_status);
    }

    *actual = ctx.i * sizeof(auxdata_i2c_device_t);

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

typedef struct {
    acpi_device_resource_t* resources;
    size_t resource_count;
    size_t resource_i;

    acpi_device_irq_t* irqs;
    size_t irq_count;
    size_t irq_i;
} acpi_crs_ctx_t;

static ACPI_STATUS report_current_resources_resource_cb(ACPI_RESOURCE* res, void* _ctx) {
    acpi_crs_ctx_t* ctx = (acpi_crs_ctx_t*)_ctx;

    if (resource_is_memory(res)) {
        resource_memory_t mem;
        zx_status_t st = resource_parse_memory(res, &mem);
        // only expect fixed memory resource. resource_parse_memory sets minimum == maximum
        // for this memory resource type.
        if ((st != ZX_OK) || (mem.minimum != mem.maximum)) {
            return AE_ERROR;
        }

        ctx->resources[ctx->resource_i].writeable = mem.writeable;
        ctx->resources[ctx->resource_i].base_address = mem.minimum;
        ctx->resources[ctx->resource_i].alignment = mem.alignment;
        ctx->resources[ctx->resource_i].address_length = mem.address_length;

        ctx->resource_i += 1;

    } else if (resource_is_address(res)) {
        resource_address_t addr;
        zx_status_t st = resource_parse_address(res, &addr);
        if (st != ZX_OK) {
            return AE_ERROR;
        }
        if ((addr.resource_type == RESOURCE_ADDRESS_MEMORY) && addr.min_address_fixed &&
            addr.max_address_fixed && (addr.maximum < addr.minimum)) {

            ctx->resources[ctx->resource_i].writeable = true;
            ctx->resources[ctx->resource_i].base_address = addr.min_address_fixed;
            ctx->resources[ctx->resource_i].alignment = 0;
            ctx->resources[ctx->resource_i].address_length = addr.address_length;

            ctx->resource_i += 1;
        }

    } else if (resource_is_irq(res)) {
        resource_irq_t irq;
        zx_status_t st = resource_parse_irq(res, &irq);
        if (st != ZX_OK) {
            return AE_ERROR;
        }
        for (size_t i = 0; i < irq.pin_count; i++) {
            ctx->irqs[ctx->irq_i].trigger = irq.trigger;
            ctx->irqs[ctx->irq_i].polarity = irq.polarity;
            ctx->irqs[ctx->irq_i].sharable = irq.sharable;
            ctx->irqs[ctx->irq_i].wake_capable = irq.wake_capable;
            ctx->irqs[ctx->irq_i].pin = irq.pins[i];

            ctx->irq_i += 1;
        }
    }

    return AE_OK;
}

static ACPI_STATUS report_current_resources_count_cb(ACPI_RESOURCE* res, void* _ctx) {
    acpi_crs_ctx_t* ctx = (acpi_crs_ctx_t*)_ctx;

    if (resource_is_memory(res)) {
        resource_memory_t mem;
        zx_status_t st = resource_parse_memory(res, &mem);
        if ((st != ZX_OK) || (mem.minimum != mem.maximum)) {
            return AE_ERROR;
        }
        ctx->resource_count += 1;

    } else if (resource_is_address(res)) {
        resource_address_t addr;
        zx_status_t st = resource_parse_address(res, &addr);
        if (st != ZX_OK) {
            return AE_ERROR;
        }
        if ((addr.resource_type == RESOURCE_ADDRESS_MEMORY) && addr.min_address_fixed &&
            addr.max_address_fixed && (addr.maximum < addr.minimum)) {
            ctx->resource_count += 1;
        }

    } else if (resource_is_irq(res)) {
        ctx->irq_count += res->Data.Irq.InterruptCount;
    }

    return AE_OK;
}

static zx_status_t report_current_resources(acpi_device_t* dev) {
    acpi_crs_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (dev->got_resources) {
        return ZX_OK;
    }

    // call _CRS to count number of resources
    ACPI_STATUS acpi_status = AcpiWalkResources(dev->ns_node, (char*)"_CRS",
            report_current_resources_count_cb, &ctx);
    if ((acpi_status != AE_NOT_FOUND) && (acpi_status != AE_OK)) {
        return acpi_to_zx_status(acpi_status);
    }

    if (ctx.resource_count == 0) {
        return ZX_OK;
    }

    // allocate resources
    ctx.resources = calloc(ctx.resource_count, sizeof(acpi_device_resource_t));
    if (!ctx.resources) {
        return ZX_ERR_NO_MEMORY;
    }
    ctx.irqs = calloc(ctx.irq_count, sizeof(acpi_device_irq_t));
    if (!ctx.irqs) {
        free(ctx.resources);
        return ZX_ERR_NO_MEMORY;
    }

    // call _CRS again and fill in resources
    acpi_status = AcpiWalkResources(dev->ns_node, (char*)"_CRS",
            report_current_resources_resource_cb, &ctx);
    if ((acpi_status != AE_NOT_FOUND) && (acpi_status != AE_OK)) {
        free(ctx.resources);
        free(ctx.irqs);
        return acpi_to_zx_status(acpi_status);
    }

    dev->resources = ctx.resources;
    dev->resource_count = ctx.resource_count;
    dev->irqs = ctx.irqs;
    dev->irq_count = ctx.irq_count;

    zxlogf(TRACE, "acpi-bus[%s]: found %zd resources %zx irqs\n", device_get_name(dev->zxdev),
            dev->resource_count, dev->irq_count);
    if (driver_get_log_flags() & DDK_LOG_SPEW) {
        zxlogf(SPEW, "resources:\n");
        for (size_t i = 0; i < dev->resource_count; i++) {
            zxlogf(SPEW, "  %02zd: addr=0x%x length=0x%x align=0x%x writeable=%d\n", i,
                    dev->resources[i].base_address,
                    dev->resources[i].address_length,
                    dev->resources[i].alignment,
                    dev->resources[i].writeable);
        }
        zxlogf(SPEW, "irqs:\n");
        for (size_t i = 0; i < dev->irq_count; i++) {
            zxlogf(SPEW, "  %02zd: pin=%u %s %s %s %s\n", i,
                    dev->irqs[i].pin,
                    dev->irqs[i].trigger ? "edge" : "level",
                    (dev->irqs[i].polarity == 2) ? "both" :
                        (dev->irqs[i].polarity ? "low" : "high"),
                    dev->irqs[i].sharable ? "shared" : "exclusive",
                    dev->irqs[i].wake_capable ? "wake" : "nowake");
        }
    }

    dev->got_resources = true;

    return ZX_OK;
}

static zx_status_t acpi_op_map_resource(void* ctx, uint32_t res_id, uint32_t cache_policy,
        void** out_vaddr, size_t* out_size, zx_handle_t* out_handle) {
    acpi_device_t* dev = (acpi_device_t*)ctx;
    mtx_lock(&dev->lock);

    zx_status_t st = report_current_resources(dev);
    if (st != ZX_OK) {
        goto unlock;
    }

    if (res_id >= dev->resource_count) {
        st = ZX_ERR_NOT_FOUND;
        goto unlock;
    }

    acpi_device_resource_t* res = dev->resources + res_id;
    if (((res->base_address & (PAGE_SIZE - 1)) != 0) ||
        ((res->address_length & (PAGE_SIZE - 1)) != 0)) {
        zxlogf(ERROR, "acpi-bus[%s]: resource id=%d addr=0x%08x len=0x%x is not page aligned\n",
                device_get_name(dev->zxdev), res_id, res->base_address, res->address_length);
        st = ZX_ERR_NOT_FOUND;
        goto unlock;
    }

    zx_handle_t vmo;
    zx_vaddr_t vaddr;
    size_t size = res->address_length;
    st = zx_vmo_create_physical(get_root_resource(), res->base_address, size, &vmo);
    if (st != ZX_OK) {
        goto unlock;
    }

    st = zx_vmo_set_cache_policy(vmo, cache_policy);
    if (st != ZX_OK) {
        zx_handle_close(vmo);
        goto unlock;
    }

    st = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, size,
                     ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE,
                     &vaddr);
    if (st != ZX_OK) {
        zx_handle_close(vmo);
    } else {
        *out_handle = vmo;
        *out_vaddr = (void*)vaddr;
        *out_size = size;
    }
unlock:
    mtx_unlock(&dev->lock);
    return st;
}

static zx_status_t acpi_op_map_interrupt(void* ctx, int which_irq, zx_handle_t* out_handle) {
    acpi_device_t* dev = (acpi_device_t*)ctx;
    mtx_lock(&dev->lock);

    zx_status_t st = report_current_resources(dev);
    if (st != ZX_OK) {
        goto unlock;
    }

    if ((uint)which_irq >= dev->irq_count) {
        st = ZX_ERR_NOT_FOUND;
        goto unlock;
    }

    acpi_device_irq_t* irq = dev->irqs + which_irq;
    zx_handle_t handle;
    st = zx_interrupt_create(get_root_resource(), irq->pin, ZX_INTERRUPT_REMAP_IRQ, &handle);
    if (st != ZX_OK) {
        goto unlock;
    }
    *out_handle = handle;

unlock:
    mtx_unlock(&dev->lock);
    return st;
}

// TODO marking unused until we publish some devices
static __attribute__ ((unused)) acpi_protocol_ops_t acpi_proto = {
    .map_resource = acpi_op_map_resource,
    .map_interrupt = acpi_op_map_interrupt,
};

static zx_protocol_device_t acpi_root_device_proto = {
    .version = DEVICE_OPS_VERSION,
};

static zx_status_t sys_device_suspend(void* ctx, uint32_t flags) {
    switch (flags & DEVICE_SUSPEND_REASON_MASK) {
    case DEVICE_SUSPEND_FLAG_MEXEC: {
        AcpiTerminate();
        return ZX_OK;
    }
    case DEVICE_SUSPEND_FLAG_REBOOT:
        reboot();
        // Kill this driver so that the IPC channel gets closed; devmgr will
        // perform a fallback that should shutdown or reboot the machine.
        exit(0);
    case DEVICE_SUSPEND_FLAG_POWEROFF:
        poweroff();
        exit(0);
    case DEVICE_SUSPEND_FLAG_SUSPEND_RAM:
        return suspend_to_ram();
    default:
        return ZX_ERR_NOT_SUPPORTED;
    };
}

static zx_protocol_device_t sys_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .suspend = sys_device_suspend,
};

static const char* hid_from_acpi_devinfo(ACPI_DEVICE_INFO* info) {
    const char* hid = NULL;
    if ((info->Valid & ACPI_VALID_HID) &&
            (info->HardwareId.Length > 0) &&
            ((info->HardwareId.Length - 1) <= sizeof(uint64_t))) {
        hid = (const char*)info->HardwareId.String;
    }
    return hid;
}

static zx_device_t* publish_device(zx_device_t* parent,
                                   ACPI_HANDLE handle,
                                   ACPI_DEVICE_INFO* info,
                                   const char* name,
                                   uint32_t protocol_id,
                                   void* protocol_ops) {
    zx_device_prop_t props[4];
    int propcount = 0;

    char acpi_name[5] = { 0 };
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
    if ((info->Valid & ACPI_VALID_CID) &&
            (info->CompatibleIdList.Count > 0) &&
            ((info->CompatibleIdList.Ids[0].Length - 1) <= sizeof(uint64_t))) {
        props[propcount].id = BIND_ACPI_CID_0_3;
        props[propcount++].value = htobe32(*((uint32_t*)(cid)));
        props[propcount].id = BIND_ACPI_CID_4_7;
        props[propcount++].value = htobe32(*((uint32_t*)(cid + 4)));
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

    acpi_device_t* dev = calloc(1, sizeof(acpi_device_t));
    if (!dev) {
        return NULL;
    }

    dev->ns_node = handle;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = dev,
        .ops = &acpi_device_proto,
        .proto_id = protocol_id,
        .proto_ops = protocol_ops,
        .props = (propcount > 0) ? props : NULL,
        .prop_count = propcount,
    };

    zx_status_t status;
    if ((status = device_add(parent, &args, &dev->zxdev)) != ZX_OK) {
        zxlogf(ERROR, "acpi: error %d in device_add, parent=%s(%p)\n",
                status, device_get_name(parent), parent);
        free(dev);
        return NULL;
    } else {
        zxlogf(ERROR, "acpi: published device %s(%p), parent=%s(%p), handle=%p\n",
                name, dev, device_get_name(parent), parent, (void*)dev->ns_node);
        return dev->zxdev;
    }
}

static ACPI_STATUS acpi_ns_walk_callback(ACPI_HANDLE object, uint32_t nesting_level,
                                         void* context, void** status) {
    ACPI_DEVICE_INFO* info = NULL;
    ACPI_STATUS acpi_status = AcpiGetObjectInfo(object, &info);
    if (acpi_status != AE_OK) {
        return acpi_status;
    }

    publish_acpi_device_ctx_t* ctx = (publish_acpi_device_ctx_t*)context;
    zx_device_t* parent = ctx->parent;

    // TODO: This is a temporary workaround until we have full ACPI device
    // enumeration. If this is the I2C1 bus, we run _PS0 so the controller
    // is active.
    if (!memcmp(&info->Name, "I2C1", 4)) {
        acpi_status = AcpiEvaluateObject(object, (char*)"_PS0", NULL, NULL);
        if (acpi_status != AE_OK) {
            zxlogf(ERROR, "acpi: acpi error 0x%x in I2C1._PS0\n", acpi_status);
        }

    // Attach the NHLT table as metadata on the HDA device.
    // The ACPI node representing the HDA controller is named "HDAS" on Pixelbook.
    // TODO: This is a temporary workaround for ACPI device enumeration.
    } else if (!memcmp(&info->Name, "HDAS", 4)) {
        // We must have already seen at least one PCI root due to traversal order.
        if (ctx->last_pci == -1) {
            zxlogf(ERROR, "acpi: Found HDAS node, but no prior PCI root was discovered!\n");
        } else if (!(info->Valid & ACPI_VALID_ADR)) {
            zxlogf(ERROR, "acpi: no valid ADR found for HDA device\n");
        } else {
            zx_status_t status = nhlt_publish_metadata(parent,
                                                       ctx->last_pci,
                                                       (uint64_t)info->Address,
                                                       object);
            if ((status != ZX_OK) && (status != ZX_ERR_NOT_FOUND)) {
                zxlogf(ERROR, "acpi: failed to publish NHLT metadata\n");
            }
        }
    }

    const char* hid = hid_from_acpi_devinfo(info);
    if (hid == 0) {
        goto out;
    }
    const char* cid = NULL;
    if ((info->Valid & ACPI_VALID_CID) &&
            (info->CompatibleIdList.Count > 0) &&
            // IDs may be 7 or 8 bytes, and Length includes the null byte
            (info->CompatibleIdList.Ids[0].Length == HID_LENGTH ||
             info->CompatibleIdList.Ids[0].Length == HID_LENGTH + 1)) {
        cid = (const char*)info->CompatibleIdList.Ids[0].String;
    }

    if ((!memcmp(hid, PCI_EXPRESS_ROOT_HID_STRING, HID_LENGTH) ||
         !memcmp(hid, PCI_ROOT_HID_STRING, HID_LENGTH))) {
        if (!ctx->found_pci) {
            // Publish PCI root as top level
            // Only publish one PCI root device for all PCI roots
            // TODO: store context for PCI root protocol
            parent = device_get_parent(parent);
            zx_device_t* pcidev = publish_device(parent, object, info, "pci",
                    ZX_PROTOCOL_PCIROOT, &pciroot_proto);
            ctx->found_pci = (pcidev != NULL);
        }
        // Get the PCI base bus number
        acpi_status = pci_get_bbn(object, &ctx->last_pci);
        if (acpi_status != AE_OK) {
            zxlogf(ERROR, "acpi: failed to get PCI base bus number for device '%s' "
                          "(acpi_status %u)\n", (const char*)&info->Name, acpi_status);
        }
        zxlogf(TRACE, "acpi: found pci root #%u\n", ctx->last_pci);
    } else if (!memcmp(hid, BATTERY_HID_STRING, HID_LENGTH)) {
        battery_init(parent, object);
    } else if (!memcmp(hid, PWRSRC_HID_STRING, HID_LENGTH)) {
        pwrsrc_init(parent, object);
    } else if (!memcmp(hid, EC_HID_STRING, HID_LENGTH)) {
        ec_init(parent, object);
    } else if (!memcmp(hid, GOOGLE_TBMC_HID_STRING, HID_LENGTH)) {
        tbmc_init(parent, object);
    } else if (!memcmp(hid, GOOGLE_CROS_EC_HID_STRING, HID_LENGTH)) {
        cros_ec_lpc_init(parent, object);
    } else if (!memcmp(hid, DPTF_THERMAL_HID_STRING, HID_LENGTH)) {
        thermal_init(parent, info, object);
    } else if (!memcmp(hid, I8042_HID_STRING, HID_LENGTH) ||
               (cid && !memcmp(cid, I8042_HID_STRING, HID_LENGTH))) {
        publish_device(parent, object, info, "i8042", ZX_PROTOCOL_ACPI, &acpi_proto);
    } else if (!memcmp(hid, RTC_HID_STRING, HID_LENGTH) ||
               (cid && !memcmp(cid, RTC_HID_STRING, HID_LENGTH))) {
        publish_device(parent, object, info, "rtc", ZX_PROTOCOL_ACPI, &acpi_proto);
    }

out:
    ACPI_FREE(info);

    return AE_OK;
}

static zx_status_t publish_acpi_devices(zx_device_t* parent) {
    // Walk the ACPI namespace for devices and publish them
    // Only publish a single PCI device
    publish_acpi_device_ctx_t ctx = {
        .parent = parent,
        .found_pci = false,
        .last_pci = -1,
    };
    ACPI_STATUS acpi_status = AcpiWalkNamespace(ACPI_TYPE_DEVICE,
                                                ACPI_ROOT_OBJECT,
                                                MAX_NAMESPACE_DEPTH,
                                                acpi_ns_walk_callback,
                                                NULL, &ctx, NULL);
    if (acpi_status != AE_OK) {
        return ZX_ERR_BAD_STATE;
    } else {
        return ZX_OK;
    }
}

static zx_status_t acpi_drv_create(void* ctx, zx_device_t* parent, const char* name,
                                   const char* _args, zx_handle_t zbi_vmo) {
    // ACPI is the root driver for its devhost so run init in the bind thread.
    zxlogf(TRACE, "acpi: bind to %s %p\n", device_get_name(parent), parent);
    root_resource_handle = get_root_resource();

    // We don't need ZBI VMO handle.
    zx_handle_close(zbi_vmo);

    zx_status_t st;
    if (ZX_OK != (st = init())) {
        zxlogf(ERROR, "acpi: failed to initialize ACPI %d \n",st);
        return ZX_ERR_INTERNAL;
    }

    zxlogf(TRACE, "acpi: initialized\n");

    zx_status_t status = install_powerbtn_handlers();
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi: error %d in install_powerbtn_handlers\n", status);
        return status;
    }

    // Report current resources to kernel PCI driver
    status = pci_report_current_resources(get_root_resource());
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi: WARNING: ACPI failed to report all current resources!\n");
    }

    // Initialize kernel PCI driver
    zx_pci_init_arg_t* arg;
    uint32_t arg_size;
    status = get_pci_init_arg(&arg, &arg_size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi: erorr %d in get_pci_init_arg\n", status);
        return status;
    }

    status = zx_pci_init(get_root_resource(), arg, arg_size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi: error %d in zx_pci_init\n", status);
        return status;
    }

    free(arg);

    // publish sys root
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ops = &sys_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    zx_device_t* sys_root = NULL;
    status = device_add(parent, &args, &sys_root);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi: error %d in device_add(sys)\n", status);
        return status;
    }

    zx_handle_t dummy_iommu_handle;
    status = iommu_manager_get_dummy_iommu(&dummy_iommu_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi-bus: error %d in iommu_manager_get_dummy_iommu()\n", status);
        return status;
    }
    zx_handle_t cpu_trace_bti;
    status = zx_bti_create(dummy_iommu_handle, 0, CPU_TRACE_BTI_ID, &cpu_trace_bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi: error %d in bti_create(cpu_trace_bti)\n", status);
        return status;
    }

    status = publish_cpu_trace(cpu_trace_bti, sys_root);
    if (status != ZX_OK) {
        return status;
    }

    // publish acpi root
    device_add_args_t args2 = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "acpi",
        .ops = &acpi_root_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    zx_device_t* acpi_root = NULL;
    status = device_add(sys_root, &args2, &acpi_root);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi: error %d in device_add(sys/acpi)\n", status);
        device_remove(sys_root);
        return status;
    }

    publish_acpi_devices(acpi_root);

    return ZX_OK;
}

static zx_driver_ops_t acpi_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .create = acpi_drv_create,
};

ZIRCON_DRIVER_BEGIN(acpi, acpi_driver_ops, "zircon", "0.1", 1)
    BI_ABORT_IF_AUTOBIND, // loaded by devcoordinator
ZIRCON_DRIVER_END(acpi)
