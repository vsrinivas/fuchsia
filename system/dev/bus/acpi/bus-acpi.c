// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/acpi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <magenta/compiler.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include "init.h"
#include "ec.h"
#include "pci.h"
#include "powerbtn.h"
#include "processor.h"
#include "power.h"

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define MAX_NAMESPACE_DEPTH 100

typedef struct acpi_device {
    mx_device_t* mxdev;

    // handle to the corresponding ACPI node
    ACPI_HANDLE ns_node;
} acpi_device_t;

typedef struct acpi_ns_walk_ctx {
    mx_device_t* parents[MAX_NAMESPACE_DEPTH + 1];
} acpi_ns_walk_ctx_t;

mx_handle_t root_resource_handle;
mx_handle_t rpc_handle;

static int acpi_rpc_thread(void* arg) {
    xprintf("bus-acpi: rpc thread starting\n");
    mx_status_t status = begin_processing(rpc_handle);
    xprintf("bus-acpi: rpc thread returned %d\n", status);
    return (status == MX_OK) ? 0 : -1;
}

static void acpi_device_release(void* ctx) {
    acpi_device_t* dev = (acpi_device_t*)ctx;
    free(dev);
}

static mx_protocol_device_t acpi_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = acpi_device_release,
};

static mx_status_t acpi_to_mx_status(ACPI_STATUS acpi_status) {
    switch (acpi_status) {
    case AE_ERROR:
    case AE_NO_ACPI_TABLES:
        return MX_ERR_INTERNAL;
    case AE_NO_NAMESPACE:
        return MX_ERR_NOT_FOUND;
    case AE_NO_MEMORY:
        return MX_ERR_NO_MEMORY;
    case AE_NOT_EXIST:
        return MX_ERR_NOT_FOUND;
    case AE_ALREADY_EXISTS:
        return MX_ERR_ALREADY_EXISTS;
    case AE_TYPE:
        return MX_ERR_WRONG_TYPE;
    case AE_NULL_OBJECT:
    case AE_NULL_ENTRY:
        return MX_ERR_NOT_FOUND;
    case AE_BUFFER_OVERFLOW:
        return MX_ERR_BUFFER_TOO_SMALL;
    case AE_STACK_OVERFLOW:
    case AE_STACK_UNDERFLOW:
        return MX_ERR_INTERNAL;
    case AE_NOT_IMPLEMENTED:
    case AE_SUPPORT:
        return MX_ERR_NOT_SUPPORTED;
    case AE_LIMIT:
        return MX_ERR_INTERNAL;
    case AE_TIME:
        return MX_ERR_TIMED_OUT;
    case AE_ACQUIRE_DEADLOCK:
    case AE_RELEASE_DEADLOCK:
    case AE_NOT_ACQUIRED:
    case AE_ALREADY_ACQUIRED:
        return MX_ERR_INTERNAL;
    case AE_NO_HARDWARE_RESPONSE:
        return MX_ERR_TIMED_OUT;
    case AE_NO_GLOBAL_LOCK:
        return MX_ERR_INTERNAL;
    case AE_ABORT_METHOD:
        return MX_ERR_INTERNAL;
    case AE_SAME_HANDLER:
        return MX_ERR_ALREADY_EXISTS;
    case AE_OWNER_ID_LIMIT:
        return MX_ERR_NO_RESOURCES;
    case AE_NOT_CONFIGURED:
        return MX_ERR_NOT_FOUND;
    case AE_ACCESS:
        return MX_ERR_ACCESS_DENIED;
    case AE_IO_ERROR:
        return MX_ERR_IO;
    default:
        return MX_ERR_INTERNAL;
    }
}

static mx_status_t call_acpi_method(acpi_device_t* dev, const char* method, ACPI_OBJECT_LIST* args, void** out_pointer) {
    ACPI_BUFFER buffer = {
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };
    ACPI_STATUS acpi_status = AcpiEvaluateObject(dev->ns_node, (char*)method, args, &buffer);
    if (acpi_status != AE_OK) {
        xprintf("acpi-bus: acpi error 0x%x in evaluate method=%s, dev=%s(%p) handle=%p\n", acpi_status, method, device_get_name(dev->mxdev), dev, (void*)dev->ns_node);
        return acpi_to_mx_status(acpi_status);
    } else {
        *out_pointer = buffer.Pointer;
        return MX_OK;
    }
}

static mx_status_t acpi_bif(void* ctx, acpi_rsp_bif_t* rsp) {
    acpi_device_t* dev = (acpi_device_t*)ctx;

    ACPI_OBJECT* obj = NULL;
    mx_status_t status = call_acpi_method(dev, "_BIF", NULL, (void**)&obj);
    if (status != MX_OK) {
        return status;
    }

    if ((obj->Type != ACPI_TYPE_PACKAGE) || (obj->Package.Count != 13)) {
        status = MX_ERR_INTERNAL;
        goto out;
    }
    ACPI_OBJECT* elem = obj->Package.Elements;
    for (int i = 0; i < 9; i++) {
        if (elem[i].Type != ACPI_TYPE_INTEGER) {
            status = MX_ERR_INTERNAL;
            goto out;
        }
    }
    for (int i = 9; i < 13; i++) {
        if (elem[i].Type != ACPI_TYPE_STRING) {
            status = MX_ERR_INTERNAL;
            goto out;
        }
    }

    rsp->power_unit = elem[0].Integer.Value;
    rsp->design_capacity = elem[1].Integer.Value;
    rsp->last_full_charge_capacity = elem[2].Integer.Value;
    rsp->battery_technology = elem[3].Integer.Value;
    rsp->design_voltage = elem[4].Integer.Value;
    rsp->design_capacity_of_warning = elem[5].Integer.Value;
    rsp->design_capacity_of_low = elem[6].Integer.Value;
    rsp->battery_capacity_granularity = elem[7].Integer.Value;
    rsp->battery_capacity_granularity_2 = elem[8].Integer.Value;

    strncpy(rsp->model_number, elem[9].String.Pointer, sizeof(rsp->model_number));
    rsp->model_number[sizeof(rsp->model_number)-1] = '\0';
    strncpy(rsp->serial_number, elem[9].String.Pointer, sizeof(rsp->serial_number));
    rsp->serial_number[sizeof(rsp->serial_number)-1] = '\0';
    strncpy(rsp->battery_type, elem[9].String.Pointer, sizeof(rsp->battery_type));
    rsp->battery_type[sizeof(rsp->battery_type)-1] = '\0';
    strncpy(rsp->oem_info, elem[9].String.Pointer, sizeof(rsp->oem_info));
    rsp->oem_info[sizeof(rsp->oem_info)-1] = '\0';
out:
    AcpiOsFree(obj);
    return status;
}

static mx_status_t acpi_bst(void* ctx, acpi_rsp_bst_t* rsp) {
    acpi_device_t* dev = (acpi_device_t*)ctx;

    ACPI_OBJECT* obj = NULL;
    mx_status_t status = call_acpi_method(dev, "_BST", NULL, (void**)&obj);
    if (status != MX_OK) {
        return status;
    }

    if ((obj->Type != ACPI_TYPE_PACKAGE) || (obj->Package.Count != 4)) {
        status = MX_ERR_INTERNAL;
        goto out;
    }
    ACPI_OBJECT* elem = obj->Package.Elements;
    for (int i = 0; i < 4; i++) {
        if (elem[i].Type != ACPI_TYPE_INTEGER) {
            status = MX_ERR_INTERNAL;
            goto out;
        }
    }

    rsp->battery_state = elem[0].Integer.Value;
    rsp->battery_present_rate = elem[1].Integer.Value;
    rsp->battery_remaining_capacity = elem[2].Integer.Value;
    rsp->battery_present_voltage = elem[3].Integer.Value;

out:
    AcpiOsFree(obj);
    return status;
}

static acpi_protocol_ops_t acpi_proto = {
    ._BIF = acpi_bif,
    ._BST = acpi_bst,
};

static mx_device_t* publish_device(mx_device_t* parent, ACPI_HANDLE handle, ACPI_DEVICE_INFO* info) {
#if TRACE
    if (!parent) {
        xprintf("acpi-bus: parent is NULL\n");
        return NULL;
    }
#endif
    // ACPI names are always 4 characters in a uint32
    char name[5] = { 0 };
    memcpy(name, &info->Name, sizeof(name) - 1);

    // Publish HID in device props
    const char* hid = (const char*)info->HardwareId.String;
    mx_device_prop_t props[2];
    if ((info->Valid & ACPI_VALID_HID) &&
            (info->HardwareId.Length > 0) &&
            (info->HardwareId.Length <= sizeof(uint64_t))) {
        props[0].id = BIND_ACPI_HID_0_3;
        props[0].value = htobe32(*((uint32_t*)(hid)));
        props[1].id = BIND_ACPI_HID_4_7;
        props[1].value = htobe32(*((uint32_t*)(hid + 4)));
    } else {
        xprintf("acpi-bus: device %s has no HID\n", name);
        hid = NULL;
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
        .proto_id = MX_PROTOCOL_ACPI,
        .proto_ops = &acpi_proto,
        .props = (hid != NULL) ? props : NULL,
        .prop_count = (hid != NULL) ? countof(props) : 0,
    };

    mx_status_t status;
    if ((status = device_add(parent, &args, &dev->mxdev)) != MX_OK) {
        xprintf("acpi-bus: error %d in device_add, parent=%s(%p) hid=%s\n", status, device_get_name(parent), parent, hid);
        free(dev);
        return NULL;
    } else {
        if (hid) {
            xprintf("acpi-bus: published device %s(%p), parent=%s(%p), hid=%s props=0x%x,0x%x handle=%p\n",
                    name, dev, device_get_name(parent), parent,
                    hid, props[0].value, props[1].value, (void*)dev->ns_node);
        } else {
            xprintf("acpi-bus: published device %s(%p), parent=%s(%p), handle=%p\n",
                    name, dev, device_get_name(parent), parent, (void*)dev->ns_node);
        }
        return dev->mxdev;
    }
}

static ACPI_STATUS acpi_ns_walk_callback(ACPI_HANDLE object, uint32_t nesting_level,
                                         void* context, void** status) {
    acpi_ns_walk_ctx_t* ctx = (acpi_ns_walk_ctx_t*)context;

    ACPI_DEVICE_INFO* info = NULL;
    ACPI_STATUS acpi_status = AcpiGetObjectInfo(object, &info);
    if (acpi_status != AE_OK) {
        return acpi_status;
    }

    xprintf("acpi-bus: handle %p nesting level %d\n", (void*)object, nesting_level);
    mx_device_t* dev = publish_device(ctx->parents[nesting_level - 1], object, info);

    // Store the newly created device for DFS traversal
    if (dev) {
        ctx->parents[nesting_level] = dev;
    }

    ACPI_FREE(info);

    return AE_OK;
}

static mx_status_t publish_all_devices(mx_device_t* parent) {
    acpi_ns_walk_ctx_t* ctx = malloc(sizeof(acpi_ns_walk_ctx_t));
    if (!ctx) {
        return MX_ERR_NO_MEMORY;
    }

    ctx->parents[0] = parent;

    // Walk the ACPI namespace for devices and publish them
    ACPI_STATUS acpi_status = AcpiWalkNamespace(ACPI_TYPE_DEVICE,
                                                ACPI_ROOT_OBJECT,
                                                MAX_NAMESPACE_DEPTH,
                                                acpi_ns_walk_callback,
                                                NULL, ctx, NULL);
    free(ctx);
    if (acpi_status != AE_OK) {
        return MX_ERR_BAD_STATE;
    } else {
        return MX_OK;
    }
}

static mx_status_t acpi_drv_bind(void* ctx, mx_device_t* parent, void** cookie) {
    // ACPI is the root driver for its devhost so run init in the bind thread.
    xprintf("bus-acpi: bind to %s %p\n", device_get_name(parent), parent);
    root_resource_handle = get_root_resource();

    // Get RPC channel
    rpc_handle = mx_get_startup_handle(PA_HND(PA_USER0, 10));
    if (rpc_handle == MX_HANDLE_INVALID) {
        xprintf("bus-acpi: no acpi rpc handle\n");
        return MX_ERR_INVALID_ARGS;
    }

    if (init() != MX_OK) {
        xprintf("bus_acpi: failed to initialize ACPI\n");
        return MX_ERR_INTERNAL;
    }

    printf("acpi-bus: initialized\n");

    ec_init();

    mx_status_t status = install_powerbtn_handlers();
    if (status != MX_OK) {
        xprintf("acpi-bus: error %d in install_powerbtn_handlers\n", status);
        return status;
    }

    // Report current resources to kernel PCI driver
    status = pci_report_current_resources(get_root_resource());
    if (status != MX_OK) {
        xprintf("acpi-bus: WARNING: ACPI failed to report all current resources!\n");
    }

    // Initialize kernel PCI driver
    mx_pci_init_arg_t* arg;
    uint32_t arg_size;
    status = get_pci_init_arg(&arg, &arg_size);
    if (status != MX_OK) {
        xprintf("acpi-bus: erorr %d in get_pci_init_arg\n", status);
        return status;
    }

    status = mx_pci_init(get_root_resource(), arg, arg_size);
    if (status != MX_OK) {
        xprintf("acpi-bus: error %d in mx_pci_init\n", status);
        return status;
    }

    free(arg);

    // start rpc thread
    // TODO: probably will be replaced with devmgr rpc mechanism
    thrd_t rpc_thrd;
    int rc = thrd_create_with_name(&rpc_thrd, acpi_rpc_thread, NULL, "acpi-rpc");
    if (rc != thrd_success) {
        xprintf("acpi-bus: error %d in rpc thrd_create\n", rc);
        return MX_ERR_INTERNAL;
    }

    // publish devices
    publish_all_devices(parent);

    return MX_OK;
}

#if 0
// Make this a bus driver when more ACPI devices other than PCI root are published
static mx_status_t acpi_drv_create(void* ctx, mx_device_t* parent,
                                   const char* name, const char* args, mx_handle_t resource) {
    xprintf("acpi_drv_create: name=%s\n", name);
    return acpi_add_pci_root_device(parent, name);
}
#endif

static mx_driver_ops_t acpi_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = acpi_drv_bind,
};

MAGENTA_DRIVER_BEGIN(acpi, acpi_driver_ops, "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_ACPI_BUS),
MAGENTA_DRIVER_END(acpi)
