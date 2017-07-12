// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <acpica/acpi.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>

#include "init.h"
#include "ec.h"
#include "pci.h"
#include "powerbtn.h"
#include "processor.h"

mx_handle_t root_resource_handle;

int main(int argc, char** argv) {
    root_resource_handle = mx_get_startup_handle(PA_HND(PA_USER0, 0));
    if (root_resource_handle <= 0) {
        printf("Failed to find root resource handle\n");
        return 1;
    }

    // Get handle from devmgr to serve as the ACPI root handle
    mx_handle_t acpi_root = mx_get_startup_handle(PA_HND(PA_USER1, 0));
    if (acpi_root <= 0) {
        printf("Failed to find acpi root handle\n");
        return 1;
    }

    ACPI_STATUS status = init();
    if (status != MX_OK) {
        printf("Failed to initialize ACPI\n");
        return 3;
    }
    printf("Initialized ACPI\n");

    ec_init();

    mx_status_t mx_status = install_powerbtn_handlers();
    if (mx_status != MX_OK) {
        printf("Failed to install powerbtn handler\n");
    }

    mx_status = pci_report_current_resources(root_resource_handle);
    if (mx_status != MX_OK) {
        printf("WARNING: ACPI failed to report all current resources!\n");
    }

    return begin_processing(acpi_root);
}
