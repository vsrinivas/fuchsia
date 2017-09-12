// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <acpica/acpi.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include "init.h"
#include "ec.h"
#include "pci.h"
#include "powerbtn.h"
#include "processor.h"

zx_handle_t root_resource_handle;

int main(int argc, char** argv) {
    root_resource_handle = zx_get_startup_handle(PA_HND(PA_USER0, 0));
    if (root_resource_handle <= 0) {
        printf("Failed to find root resource handle\n");
        return 1;
    }

    // Get handle from devmgr to serve as the ACPI root handle
    zx_handle_t acpi_root = zx_get_startup_handle(PA_HND(PA_USER1, 0));
    if (acpi_root <= 0) {
        printf("Failed to find acpi root handle\n");
        return 1;
    }

    ACPI_STATUS status = init();
    if (status != ZX_OK) {
        printf("Failed to initialize ACPI\n");
        return 3;
    }
    printf("Initialized ACPI\n");

    ec_init();

    zx_status_t zx_status = install_powerbtn_handlers();
    if (zx_status != ZX_OK) {
        printf("Failed to install powerbtn handler\n");
    }

    zx_status = pci_report_current_resources(root_resource_handle);
    if (zx_status != ZX_OK) {
        printf("WARNING: ACPI failed to report all current resources!\n");
    }

    return begin_processing(acpi_root);
}
