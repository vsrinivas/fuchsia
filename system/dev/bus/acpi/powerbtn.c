// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "powerbtn.h"

#include <stdio.h>
#include <threads.h>

#include <acpica/acpi.h>
#include <zircon/syscalls.h>

#include "power.h"

/**
 * @brief  Handle the Power Button Fixed Event
 *
 * We simply write to a well known port. A user-mode driver should pick
 * this event and take action.
 */
static uint32_t power_button_object_handler(void* ctx) {
    zx_handle_t event = (zx_handle_t)(uintptr_t)ctx;
    zx_object_signal(event, 0, ZX_EVENT_SIGNALED);
    // Note that the spec indicates to return 0. The code in the
    // Intel implementation (AcpiEvFixedEventDetect) reads differently.
    return ACPI_INTERRUPT_HANDLED;
}

static void notify_object_handler(ACPI_HANDLE Device, UINT32 Value, void* Context) {
    ACPI_DEVICE_INFO* info = NULL;
    ACPI_STATUS status = AcpiGetObjectInfo(Device, &info);
    if (status != AE_OK) {
        if (info) {
            ACPI_FREE(info);
        }
        return;
    }

    zx_handle_t event = (zx_handle_t)(uintptr_t)Context;

    // Handle powerbutton events via the notify interface
    bool power_btn = false;
    if (info->Valid & ACPI_VALID_HID) {
        if (Value == 128 &&
            !strncmp(info->HardwareId.String, "PNP0C0C", info->HardwareId.Length)) {

            power_btn = true;
        } else if (Value == 199 &&
                   (!strncmp(info->HardwareId.String, "MSHW0028", info->HardwareId.Length) ||
                    !strncmp(info->HardwareId.String, "MSHW0040", info->HardwareId.Length))) {
            power_btn = true;
        }
    }

    if (power_btn) {
        zx_object_signal(event, 0, ZX_EVENT_SIGNALED);
    }

    ACPI_FREE(info);
}

static int power_button_thread(void* arg) {
    zx_handle_t event = (zx_handle_t)(uintptr_t)arg;

    for (;;) {
        zx_status_t status = zx_object_wait_one(event,
                                                ZX_EVENT_SIGNALED,
                                                ZX_TIME_INFINITE,
                                                NULL);
        if (status != ZX_OK) {
            break;
        }

        poweroff();
    }

    printf("acpi power button thread terminated\n");
    return 0;
}

zx_status_t install_powerbtn_handlers(void) {
    // Hacks to make the power button power off the machine

    zx_handle_t power_button_event;
    zx_status_t err = zx_event_create(0, &power_button_event);
    if (err < 0) {
        return err;
    }

    ACPI_STATUS status = AcpiInstallFixedEventHandler(ACPI_EVENT_POWER_BUTTON,
                                                      power_button_object_handler,
                                                      (void*)(uintptr_t)power_button_event);
    if (status != AE_OK) {
        return ZX_ERR_INTERNAL;
    }

    AcpiInstallNotifyHandler(ACPI_ROOT_OBJECT,
                             ACPI_SYSTEM_NOTIFY | ACPI_DEVICE_NOTIFY,
                             notify_object_handler,
                             (void*)(uintptr_t)power_button_event);

    thrd_t thread;
    int ret = thrd_create(&thread, power_button_thread, (void*)(uintptr_t)power_button_event);
    if (ret != thrd_success) {
        return ZX_ERR_NO_RESOURCES;
    }
    thrd_detach(thread);
    return ZX_OK;
}
