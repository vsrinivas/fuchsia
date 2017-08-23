// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dev.h"

#include <hw/inout.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mxio/debug.h>

#include "errors.h"

#define MXDEBUG 0

/* EC commands */
#define EC_CMD_QUERY 0x84

/* EC status register bits */
#define EC_SC_SCI_EVT (1 << 5)
#define EC_SC_IBF (1 << 1)
#define EC_SC_OBF (1 << 0)

/* Thread shutdown signals */
#define EC_THREAD_SHUTDOWN MX_USER_SIGNAL_0
#define EC_THREAD_SHUTDOWN_DONE MX_USER_SIGNAL_1

typedef struct acpi_ec_device {
    mx_device_t* mxdev;

    ACPI_HANDLE acpi_handle;

    // PIO addresses for EC device
    uint16_t cmd_port;
    uint16_t data_port;

    // GPE for EC events
    ACPI_HANDLE gpe_block;
    UINT32 gpe;

    // thread for processing System Control Interrupts
    thrd_t sci_thread;
    mx_handle_t pending_sci_evt;

    bool gpe_setup : 1;
    bool thread_setup : 1;
} acpi_ec_device_t;

static ACPI_STATUS get_ec_handle(ACPI_HANDLE, UINT32, void*, void**);
static ACPI_STATUS get_ec_gpe_info(ACPI_HANDLE, ACPI_HANDLE*, UINT32*);
static ACPI_STATUS get_ec_ports(ACPI_HANDLE, uint16_t*, uint16_t*);

static int acpi_ec_thread(void* arg) {
    acpi_ec_device_t* dev = arg;

    while (1) {
        uint32_t pending;
        mx_status_t mx_status = mx_object_wait_one(dev->pending_sci_evt,
                                                   MX_EVENT_SIGNALED | EC_THREAD_SHUTDOWN,
                                                   MX_TIME_INFINITE,
                                                   &pending);
        if (mx_status != MX_OK) {
            printf("acpi-ec: thread wait failed: %d\n", mx_status);
            break;
        }

        if (pending & EC_THREAD_SHUTDOWN) {
            mx_object_signal(dev->pending_sci_evt, 0, EC_THREAD_SHUTDOWN_DONE);
            break;
        }

        mx_object_signal(dev->pending_sci_evt, MX_EVENT_SIGNALED, 0);

        UINT32 global_lock;
        while (AcpiAcquireGlobalLock(0xFFFF, &global_lock) != AE_OK)
            ;

        uint8_t status;
        do {
            status = inp(dev->cmd_port);
            /* Read the status out of the command/status port */
            if (!(status & EC_SC_SCI_EVT)) {
                break;
            }

            /* Query EC command */
            outp(dev->cmd_port, EC_CMD_QUERY);

            /* Wait for EC to read the command */
            while (!((status = inp(dev->cmd_port)) & EC_SC_IBF))
                ;

            /* Wait for EC to respond */
            while (!((status = inp(dev->cmd_port)) & EC_SC_OBF))
                ;

            while ((status = inp(dev->cmd_port)) & EC_SC_OBF) {
                /* Read until the output buffer is empty */
                uint8_t event_code = inp(dev->data_port);
                char method[5] = {0};
                snprintf(method, sizeof(method), "_Q%02x", event_code);
                xprintf("acpi-ec: Invoking method %s\n", method);
                AcpiEvaluateObject(dev->acpi_handle, method, NULL, NULL);
                xprintf("acpi-ec: Invoked method %s\n", method);
            }
        } while (status & EC_SC_SCI_EVT);

        AcpiReleaseGlobalLock(global_lock);
    }

    xprintf("acpi-ec: thread terminated\n");
    return 0;
}

static uint32_t raw_ec_event_gpe_handler(ACPI_HANDLE gpe_dev, uint32_t gpe_num, void* ctx) {
    acpi_ec_device_t* dev = ctx;
    mx_object_signal(dev->pending_sci_evt, 0, MX_EVENT_SIGNALED);
    return ACPI_REENABLE_GPE;
}

static ACPI_STATUS get_ec_handle(
    ACPI_HANDLE object,
    UINT32 nesting_level,
    void* context,
    void** ret) {

    *(ACPI_HANDLE*)context = object;
    return AE_OK;
}

static ACPI_STATUS get_ec_gpe_info(
    ACPI_HANDLE ec_handle, ACPI_HANDLE* gpe_block, UINT32* gpe) {
    ACPI_BUFFER buffer = {
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };
    ACPI_STATUS status = AcpiEvaluateObject(
        ec_handle, (char*)"_GPE", NULL, &buffer);
    if (status != AE_OK) {
        return status;
    }

    /* According to section 12.11 of ACPI v6.1, a _GPE object on this device
     * evaluates to either an integer specifying bit in the GPEx_STS blocks
     * to use, or a package specifying which GPE block and which bit inside
     * that block to use. */
    ACPI_OBJECT* gpe_obj = buffer.Pointer;
    if (gpe_obj->Type == ACPI_TYPE_INTEGER) {
        *gpe_block = NULL;
        *gpe = gpe_obj->Integer.Value;
    } else if (gpe_obj->Type == ACPI_TYPE_PACKAGE) {
        if (gpe_obj->Package.Count != 2) {
            goto bailout;
        }
        ACPI_OBJECT* block_obj = &gpe_obj->Package.Elements[0];
        ACPI_OBJECT* gpe_num_obj = &gpe_obj->Package.Elements[1];
        if (block_obj->Type != ACPI_TYPE_LOCAL_REFERENCE) {
            goto bailout;
        }
        if (gpe_num_obj->Type != ACPI_TYPE_INTEGER) {
            goto bailout;
        }
        *gpe_block = block_obj->Reference.Handle;
        *gpe = gpe_num_obj->Integer.Value;
    } else {
        goto bailout;
    }
    ACPI_FREE(buffer.Pointer);
    return AE_OK;

bailout:
    xprintf("Failed to intepret EC GPE number");
    ACPI_FREE(buffer.Pointer);
    return AE_BAD_DATA;
}

struct ec_ports_callback_ctx {
    uint16_t* data_port;
    uint16_t* cmd_port;
    unsigned int resource_num;
};

static ACPI_STATUS get_ec_ports_callback(
    ACPI_RESOURCE* Resource, void* Context) {
    struct ec_ports_callback_ctx* ctx = Context;

    if (Resource->Type == ACPI_RESOURCE_TYPE_END_TAG) {
        return AE_OK;
    }

    /* The spec says there will be at most 3 resources */
    if (ctx->resource_num >= 3) {
        return AE_BAD_DATA;
    }
    /* The third resource only exists on HW-Reduced platforms, which we don't
     * support at the moment. */
    if (ctx->resource_num == 2) {
        xprintf("RESOURCE TYPE %d\n", Resource->Type);
        return AE_NOT_IMPLEMENTED;
    }

    /* The two resources we're expecting are both address regions.  First the
     * data one, then the command one.  We assume they're single IO ports. */
    if (Resource->Type != ACPI_RESOURCE_TYPE_IO) {
        return AE_SUPPORT;
    }
    if (Resource->Data.Io.Maximum != Resource->Data.Io.Minimum) {
        return AE_SUPPORT;
    }

    uint16_t port = Resource->Data.Io.Minimum;
    if (ctx->resource_num == 0) {
        *ctx->data_port = port;
    } else {
        *ctx->cmd_port = port;
    }

    ctx->resource_num++;
    return AE_OK;
}

static ACPI_STATUS get_ec_ports(
    ACPI_HANDLE ec_handle, uint16_t* data_port, uint16_t* cmd_port) {
    struct ec_ports_callback_ctx ctx = {
        .data_port = data_port,
        .cmd_port = cmd_port,
        .resource_num = 0,
    };

    return AcpiWalkResources(ec_handle, (char*)"_CRS", get_ec_ports_callback, &ctx);
}

static void acpi_ec_release(void* ctx) {
    acpi_ec_device_t* dev = ctx;

    if (dev->pending_sci_evt != MX_HANDLE_INVALID) {
        if (dev->thread_setup) {
            /* Shutdown the EC thread */
            mx_object_signal(dev->pending_sci_evt, 0, EC_THREAD_SHUTDOWN);
            mx_object_wait_one(dev->pending_sci_evt, EC_THREAD_SHUTDOWN_DONE, MX_TIME_INFINITE, NULL);
            thrd_join(dev->sci_thread, NULL);
        }

        mx_handle_close(dev->pending_sci_evt);
    }

    if (dev->gpe_setup) {
        AcpiDisableGpe(dev->gpe_block, dev->gpe);
        AcpiRemoveGpeHandler(dev->gpe_block, dev->gpe, raw_ec_event_gpe_handler);
    }

    free(dev);
}

static mx_protocol_device_t acpi_ec_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = acpi_ec_release,
};

mx_status_t ec_init(mx_device_t* parent, ACPI_HANDLE acpi_handle) {
    xprintf("acpi-ec: init\n");

    acpi_ec_device_t* dev = calloc(1, sizeof(acpi_ec_device_t));
    if (!dev) {
        return MX_ERR_NO_MEMORY;
    }
    dev->acpi_handle = acpi_handle;

    mx_status_t err = mx_event_create(0, &dev->pending_sci_evt);
    if (err != MX_OK) {
        xprintf("acpi-ec: Failed to create event: %d\n", err);
        acpi_ec_release(dev);
        return err;
    }

    ACPI_STATUS status = get_ec_gpe_info(acpi_handle, &dev->gpe_block, &dev->gpe);
    if (status != AE_OK) {
        xprintf("acpi-ec: Failed to decode GPE info: %d\n", status);
        goto acpi_error;
    }

    status = get_ec_ports(
        acpi_handle, &dev->data_port, &dev->cmd_port);
    if (status != AE_OK) {
        xprintf("acpi-ec: Failed to decode comm info: %d\n", status);
        goto acpi_error;
    }

    /* Setup GPE handling */
    status = AcpiInstallGpeHandler(
        dev->gpe_block, dev->gpe, ACPI_GPE_EDGE_TRIGGERED,
        raw_ec_event_gpe_handler, dev);
    if (status != AE_OK) {
        xprintf("acpi-ec: Failed to install GPE %d: %x\n", dev->gpe, status);
        goto acpi_error;
    }
    status = AcpiEnableGpe(dev->gpe_block, dev->gpe);
    if (status != AE_OK) {
        xprintf("acpi-ec: Failed to enable GPE %d: %x\n", dev->gpe, status);
        AcpiRemoveGpeHandler(dev->gpe_block, dev->gpe, raw_ec_event_gpe_handler);
        goto acpi_error;
    }
    dev->gpe_setup = true;

    int ret = thrd_create_with_name(&dev->sci_thread, acpi_ec_thread, dev, "acpi-ec-sci");
    if (ret != thrd_success) {
        xprintf("acpi-ec: Failed to create thread\n");
        acpi_ec_release(dev);
        return MX_ERR_INTERNAL;
    }
    dev->thread_setup = true;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "acpi-ec",
        .ctx = dev,
        .ops = &acpi_ec_device_proto,
        .proto_id = MX_PROTOCOL_MISC,
    };

    status = device_add(parent, &args, &dev->mxdev);
    if (status != MX_OK) {
        xprintf("acpi-ec: could not add device! err=%d\n", status);
        acpi_ec_release(dev);
        return status;
    }

    printf("acpi-ec: initialized\n");
    return MX_OK;

acpi_error:
    acpi_ec_release(dev);
    return acpi_to_mx_status(status);
}
