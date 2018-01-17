// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <limits.h>

#include "dev.h"
#include "init.h"

#define ACPI_MAX_INIT_TABLES 32

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

static int is_gpe_device(ACPI_HANDLE object) {
    ACPI_DEVICE_INFO* info = NULL;
    ACPI_STATUS acpi_status = AcpiGetObjectInfo(object, &info);
    if (acpi_status == AE_OK) {
        // These length fields count the trailing NUL.
        if ((info->Valid & ACPI_VALID_HID) && info->HardwareId.Length <= HID_LENGTH + 1) {
            if (!strncmp(info->HardwareId.String, GPE_HID_STRING, HID_LENGTH)) {
                return 1;
            }
        }
        if ((info->Valid & ACPI_VALID_CID) && info->CompatibleIdList.Count > 0) {
            ACPI_PNP_DEVICE_ID* id = &info->CompatibleIdList.Ids[0];
            if (!strncmp(id->String, GPE_CID_STRING, CID_LENGTH)) {
                return 1;
            }
        }
        ACPI_FREE(info);
    }
    return 0;
}

static ACPI_STATUS acpi_prw_walk(ACPI_HANDLE obj, UINT32 level, void* context, void** out_value) {
    ACPI_BUFFER buffer = {
        // Request that the ACPI subsystem allocate the buffer
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };
    ACPI_STATUS status = AcpiEvaluateObject(obj, (char*) "_PRW", NULL, &buffer);
    if (status != AE_OK) {
        return AE_OK; // Keep walking the tree
    }
    ACPI_OBJECT* prw_res = buffer.Pointer;

    // _PRW returns a package with >= 2 entries. The first entry indicates what type of
    // event it is. If it's a GPE event, the first entry is either an integer indicating
    // the bit within the FADT GPE enable register or it is a package containing a handle
    // to a GPE block device and the bit index on that device. There are other event
    // types with (handle, int) packages, so check that the handle is a GPE device by
    // checking against the CID/HID required by the ACPI spec.
    if (prw_res->Type != ACPI_TYPE_PACKAGE || prw_res->Package.Count < 2) {
        return AE_OK; // Keep walking the tree
    }

    ACPI_HANDLE gpe_block;
    UINT32 gpe_bit;
    ACPI_OBJECT* event_info = &prw_res->Package.Elements[0];
    if (event_info->Type == ACPI_TYPE_INTEGER) {
        gpe_block = NULL;
        gpe_bit = prw_res->Package.Elements[0].Integer.Value;
    } else if (event_info->Type == ACPI_TYPE_PACKAGE) {
        if (event_info->Package.Count != 2) {
            goto bailout;
        }
        ACPI_OBJECT* handle_obj = &event_info->Package.Elements[0];
        ACPI_OBJECT* gpe_num_obj = &event_info->Package.Elements[1];
        if (handle_obj->Type != ACPI_TYPE_LOCAL_REFERENCE
                || !is_gpe_device(handle_obj->Reference.Handle)) {
            goto bailout;
        }
        if (gpe_num_obj->Type != ACPI_TYPE_INTEGER) {
            goto bailout;
        }
        gpe_block = handle_obj->Reference.Handle;
        gpe_bit = gpe_num_obj->Integer.Value;
    } else {
        goto bailout;
    }
    if (AcpiSetupGpeForWake(obj, gpe_block, gpe_bit) != AE_OK) {
        printf("INFO: Acpi failed to setup wake GPE\n");
    }

bailout:
    ACPI_FREE(buffer.Pointer);

    return AE_OK; // We want to keep going even if we bailed out
}

ACPI_STATUS init(void) {
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

    AcpiWalkNamespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, INT_MAX,
                      acpi_prw_walk, NULL, NULL, NULL);

    status = AcpiUpdateAllGpes();
    if (status != AE_OK) {
        printf("WARNING: could not initialize ACPI GPEs\n");
        return status;
    }

    // TODO(teisenbe): Maybe back out of ACPI mode on failure, but we rely on
    // ACPI for some critical things right now, so failure will likely prevent
    // successful boot anyway.
    return AE_OK;
}

