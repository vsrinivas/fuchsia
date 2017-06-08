// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <acpica/acpi.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/resource.h>

#include "ec.h"
#include "pci.h"
#include "powerbtn.h"
#include "processor.h"
#include "resource_tree.h"

#define ACPI_MAX_INIT_TABLES 32

static ACPI_STATUS set_apic_irq_mode(void);
static ACPI_STATUS init(void);

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

    mx_handle_t port;
    mx_status_t mx_status = mx_port_create(0, &port);
    if (mx_status != MX_OK) {
        printf("Failed to construct resource port\n");
        return 4;
    }

    // TODO(teisenbe): In the future, devmgr should create this and hand it to
    // us.
    mx_handle_t acpi_bus_resource;
    {
        mx_rrec_t records[1] = { { 0 } };
        records[0].self.type = MX_RREC_SELF;
        records[0].self.subtype = MX_RREC_SELF_GENERIC;
        records[0].self.options = 0;
        records[0].self.record_count = 1;
        strncpy(records[0].self.name, "ACPI-BUS", sizeof(records[0].self.name));
        mx_status = mx_resource_create(root_resource_handle, records, countof(records),
                                       &acpi_bus_resource);
        if (mx_status != MX_OK) {
            printf("Failed to create ACPI-BUS resource\n");
            return 6;
        }
    }

    mx_status = resource_tree_init(port, acpi_bus_resource);
    if (mx_status != MX_OK) {
        printf("Failed to initialize resource tree\n");
        return 5;
    }

    ec_init();

    mx_status = install_powerbtn_handlers();
    if (mx_status != MX_OK) {
        printf("Failed to install powerbtn handler\n");
    }

    mx_status = pci_report_current_resources(root_resource_handle);
    if (mx_status != MX_OK) {
        printf("WARNING: ACPI failed to report all current resources!\n");
    }

    return begin_processing(acpi_root);
}

static ACPI_STATUS init(void) {
    // This sequence is described in section 10.1.2.1 (Full ACPICA Initialization)
    // of the ACPICA developer's reference.
    ACPI_STATUS status = AcpiInitializeSubsystem();
    if (status != AE_OK) {
        printf("WARNING: could not initialize ACPI\n");
        return status;
    }

    status = AcpiInitializeTables(NULL, ACPI_MAX_INIT_TABLES, FALSE);
    if (status == AE_NOT_FOUND) {
        printf("WARNING: could not find ACPI tables\n");
        return status;
    } else if (status == AE_NO_MEMORY) {
        printf("WARNING: could not initialize ACPI tables\n");
        return status;
    } else if (status != AE_OK) {
        printf("WARNING: could not initialize ACPI tables for unknown reason\n");
        return status;
    }

    status = AcpiLoadTables();
    if (status != AE_OK) {
        printf("WARNING: could not load ACPI tables: %d\n", status);
        return status;
    }

    status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
    if (status != AE_OK) {
        printf("WARNING: could not enable ACPI\n");
        return status;
    }

    status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
    if (status != AE_OK) {
        printf("WARNING: could not initialize ACPI objects\n");
        return status;
    }

    status = set_apic_irq_mode();
    if (status == AE_NOT_FOUND) {
        printf("WARNING: Could not find ACPI IRQ mode switch\n");
    } else if (status != AE_OK) {
        printf("Failed to set APIC IRQ mode\n");
        return status;
    }

    // TODO(teisenbe): Maybe back out of ACPI mode on failure, but we rely on
    // ACPI for some critical things right now, so failure will likely prevent
    // successful boot anyway.
    return AE_OK;
}

/* @brief Switch interrupts to APIC model (controls IRQ routing) */
static ACPI_STATUS set_apic_irq_mode(void) {
    ACPI_OBJECT selector = {
        .Integer.Type = ACPI_TYPE_INTEGER,
        .Integer.Value = 1, // 1 means APIC mode according to ACPI v5 5.8.1
    };
    ACPI_OBJECT_LIST params = {
        .Count = 1,
        .Pointer = &selector,
    };
    return AcpiEvaluateObject(NULL, (char*)"\\_PIC", &params, NULL);
}
