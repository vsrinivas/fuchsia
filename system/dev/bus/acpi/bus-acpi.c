// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/debug.h>
#include <ddk/protocol/pciroot.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include "init.h"
#include "dev.h"
#include "pci.h"
#include "powerbtn.h"
#include "processor.h"
#include "power.h"

#define MXDEBUG 0

#define MAX_NAMESPACE_DEPTH 100

#define HID_LENGTH 8

typedef struct acpi_device {
    zx_device_t* zxdev;

    // handle to the corresponding ACPI node
    ACPI_HANDLE ns_node;
} acpi_device_t;

typedef struct {
    zx_device_t* parent;
    bool found_pci;
} publish_acpi_device_ctx_t;

zx_handle_t root_resource_handle;

static void acpi_device_release(void* ctx) {
    acpi_device_t* dev = (acpi_device_t*)ctx;
    free(dev);
}

static zx_protocol_device_t acpi_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = acpi_device_release,
};

static pciroot_protocol_ops_t pciroot_proto = {
};

static zx_protocol_device_t acpi_root_device_proto = {
    .version = DEVICE_OPS_VERSION,
};

static zx_status_t sys_device_suspend(void* ctx, uint32_t flags) {
    switch (flags) {
    case DEVICE_SUSPEND_FLAG_REBOOT:
        reboot();
        // Kill this driver so that the IPC channel gets closed; devmgr will
        // perform a fallback that should shutdown or reboot the machine.
        exit(0);
    case DEVICE_SUSPEND_FLAG_POWEROFF:
        poweroff();
        exit(0);
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
        char acpi_name[5] = { 0 };
        memcpy(acpi_name, &info->Name, sizeof(acpi_name) - 1);
        zxlogf(SPEW, "acpi-bus: got device %s\n", acpi_name);
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

    // TODO: publish pciroot and other acpi devices in separate devhosts?

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
        zxlogf(ERROR, "acpi-bus: error %d in device_add, parent=%s(%p)\n",
                status, device_get_name(parent), parent);
        free(dev);
        return NULL;
    } else {
        zxlogf(ERROR, "acpi-bus: published device %s(%p), parent=%s(%p), handle=%p\n",
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

    // TODO: This is a temporary workaround until we have full ACPI device
    // enumeration. If this is the I2C1 bus, we run _PS0 so the controller
    // is active.
    if (!memcmp(&info->Name, "I2C1", 4)) {
        ACPI_STATUS acpi_status = AcpiEvaluateObject(object, (char*)"_PS0", NULL, NULL);
        if (acpi_status != AE_OK) {
            zxlogf(ERROR, "acpi-bus: acpi error 0x%x in I2C1._PS0\n", acpi_status);
        }
    }

    zxlogf(TRACE, "acpi-bus: handle %p nesting level %d\n", (void*)object, nesting_level);

    publish_acpi_device_ctx_t* ctx = (publish_acpi_device_ctx_t*)context;
    zx_device_t* parent = ctx->parent;
    const char* hid = hid_from_acpi_devinfo(info);
    if (hid == 0) {
        goto out;
    }
    if (!ctx->found_pci && (!memcmp(hid, PCI_EXPRESS_ROOT_HID_STRING, HID_LENGTH) ||
                            !memcmp(hid, PCI_ROOT_HID_STRING, HID_LENGTH))) {
        // Publish PCI root as top level
        // Only publish one PCI root device for all PCI roots
        // TODO: store context for PCI root protocol
        parent = device_get_parent(parent);
        zx_device_t* pcidev = publish_device(parent, object, info, "pci",
                ZX_PROTOCOL_PCIROOT, &pciroot_proto);
        ctx->found_pci = (pcidev != NULL);
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
                                   const char* _args, zx_handle_t rpc_channel) {
    // ACPI is the root driver for its devhost so run init in the bind thread.
    zxlogf(TRACE, "acpi-bus: bind to %s %p\n", device_get_name(parent), parent);
    root_resource_handle = get_root_resource();

    if (init() != ZX_OK) {
        zxlogf(ERROR, "acpi-bus: failed to initialize ACPI\n");
        return ZX_ERR_INTERNAL;
    }

    zxlogf(TRACE, "acpi-bus: initialized\n");

    zx_status_t status = install_powerbtn_handlers();
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi-bus: error %d in install_powerbtn_handlers\n", status);
        return status;
    }

    // Report current resources to kernel PCI driver
    status = pci_report_current_resources(get_root_resource());
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi-bus: WARNING: ACPI failed to report all current resources!\n");
    }

    // Initialize kernel PCI driver
    zx_pci_init_arg_t* arg;
    uint32_t arg_size;
    status = get_pci_init_arg(&arg, &arg_size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi-bus: erorr %d in get_pci_init_arg\n", status);
        return status;
    }

    status = zx_pci_init(get_root_resource(), arg, arg_size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi-bus: error %d in zx_pci_init\n", status);
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
        zxlogf(ERROR, "acpi-bus: error %d in device_add(sys)\n", status);
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
        zxlogf(ERROR, "acpi-bus: error %d in device_add(sys/acpi)\n", status);
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
