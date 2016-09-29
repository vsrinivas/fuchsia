// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ec.h"

#include <threads.h>

#include <acpica/acpi.h>
#include <hw/inout.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mxio/debug.h>

#define MXDEBUG 0

/* EC commands */
#define EC_CMD_QUERY 0x84

/* EC status register bits */
#define EC_SC_SCI_EVT (1 << 5)
#define EC_SC_IBF (1 << 1)
#define EC_SC_OBF (1 << 0)

static mx_handle_t pending_sci_evt;

static struct {
    ACPI_HANDLE handle;
    uint16_t data_port;
    uint16_t cmd_port;
} ec_info;

static int acpi_ec_thread(void* arg) {
    while (1) {
        mx_status_t mx_status = mx_handle_wait_one(pending_sci_evt,
                                                   MX_SIGNAL_SIGNALED,
                                                   MX_TIME_INFINITE,
                                                   NULL);
        if (mx_status != NO_ERROR) {
            break;
        }
        mx_object_signal(pending_sci_evt, MX_SIGNAL_SIGNALED, 0);

        UINT32 global_lock;
        while (AcpiAcquireGlobalLock(0xFFFF, &global_lock) != AE_OK)
            ;

        uint8_t status;
        do {
            status = inp(ec_info.cmd_port);
            /* Read the status out of the command/status port */
            if (!(status & EC_SC_SCI_EVT)) {
                break;
            }

            /* Query EC command */
            outp(ec_info.cmd_port, EC_CMD_QUERY);

            /* Wait for EC to read the command */
            while (!((status = inp(ec_info.cmd_port)) & EC_SC_IBF))
                ;

            /* Wait for EC to respond */
            while (!((status = inp(ec_info.cmd_port)) & EC_SC_OBF))
                ;

            while ((status = inp(ec_info.cmd_port)) & EC_SC_OBF) {
                /* Read until the output buffer is empty */
                uint8_t event_code = inp(ec_info.data_port);
                char method[5] = {0};
                snprintf(method, sizeof(method), "_Q%02x", event_code);
                xprintf("Invoking method %s\n", method);
                AcpiEvaluateObject(ec_info.handle, method, NULL, NULL);
                xprintf("Invoked method %s\n", method);
            }
        } while (status & EC_SC_SCI_EVT);

        AcpiReleaseGlobalLock(global_lock);
    }

    printf("acpi ec thread terminated\n");
    return 0;
}

static uint32_t raw_ec_event_gpe_handler(ACPI_HANDLE gpe_dev, uint32_t gpe_num, void* ctx) {
    mx_object_signal(pending_sci_evt, 0, MX_SIGNAL_SIGNALED);
    return ACPI_REENABLE_GPE;
}

static ACPI_STATUS get_ec_handle(ACPI_HANDLE, UINT32, void*, void**);
static ACPI_STATUS get_ec_gpe_info(ACPI_HANDLE, ACPI_HANDLE*, UINT32*);
static ACPI_STATUS get_ec_ports(ACPI_HANDLE, uint16_t*, uint16_t*);

void ec_init(void) {
    pending_sci_evt = mx_event_create(0);
    if (pending_sci_evt <= 0) {
        xprintf("Failed to create event: %d\n", pending_sci_evt);
        return;
    }

    /* PNP0C09 devices are defined in section 12.11 of ACPI v6.1 */
    ACPI_STATUS status = AcpiGetDevices(
        (char*)"PNP0C09",
        get_ec_handle,
        &ec_info.handle,
        NULL);
    if (status != AE_OK || ec_info.handle == NULL) {
        xprintf("Failed to find EC: %d\n", status);
        return;
    }

    ACPI_HANDLE gpe_block = NULL;
    UINT32 gpe = 0;
    status = get_ec_gpe_info(ec_info.handle, &gpe_block, &gpe);
    if (status != AE_OK) {
        xprintf("Failed to decode EC GPE info: %d\n", status);
        return;
    }

    status = get_ec_ports(
        ec_info.handle, &ec_info.data_port, &ec_info.cmd_port);
    if (status != AE_OK) {
        xprintf("Failed to decode EC comm info: %d\n", status);
        return;
    }

    /* Setup GPE handling */
    status = AcpiInstallGpeHandler(
        gpe_block, gpe, ACPI_GPE_EDGE_TRIGGERED,
        raw_ec_event_gpe_handler, NULL);
    if (status != AE_OK) {
        xprintf("Failed to install GPE %d: %x\n", gpe, status);
        goto bailout;
    }
    status = AcpiEnableGpe(gpe_block, gpe);
    if (status != AE_OK) {
        xprintf("Failed to enable GPE %d: %x\n", gpe, status);
        goto bailout;
    }

    thrd_t thread;
    int ret = thrd_create(&thread, acpi_ec_thread, NULL);
    if (ret != thrd_success) {
        xprintf("Failed to create ACPI EC thread\n");
        goto bailout;
    }
    thrd_detach(thread);
    return;

bailout:
    AcpiDisableGpe(gpe_block, gpe);
    AcpiRemoveGpeHandler(gpe_block, gpe, raw_ec_event_gpe_handler);
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
