// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <limits.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/intel-hda-dsp.h>
#include <zircon/process.h>

#include "errors.h"
#include "nhlt.h"

/**
 * Reference:
 *
 * Intel Smart Sound Technology Audio DSP Non-HD Audio ACPI High Level Design
 * Architecture Guide/Overview
 * Revision 0.7
 * November 2015
 *
 * 561555_SST Non-HD Audio ACPI HLD v0 7_DRAFT.pdf
 */

static const uint8_t NHLT_UUID[] = {
/* 0000 */ 0x6E, 0x88, 0x9F, 0xA6, 0xEB, 0x6C, 0x94, 0x45,
/* 0008 */ 0xA4, 0x1F, 0x7B, 0x5D, 0xCE, 0x24, 0xC5, 0x53
};

zx_status_t nhlt_publish_metadata(zx_device_t* dev, int bbn, uint64_t adr, ACPI_HANDLE object) {
    zx_status_t status = ZX_OK;

    // parameters
    ACPI_OBJECT objs[] = {
    {   // uuid
        .Buffer.Type = ACPI_TYPE_BUFFER,
        .Buffer.Length = sizeof(NHLT_UUID),
        .Buffer.Pointer = (void*)NHLT_UUID,
    },
    {   // revision id
        .Integer.Type = ACPI_TYPE_INTEGER,
        .Integer.Value = 1,
    },
    {   // function id
        .Integer.Type = ACPI_TYPE_INTEGER,
        .Integer.Value = 1,
    },
    };
    ACPI_OBJECT_LIST params = {
        .Count = countof(objs),
        .Pointer = objs,
    };

    // output buffer
    ACPI_BUFFER out = {
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = NULL,
    };

    // Fetch the NHLT resource
    ACPI_STATUS acpi_status = AcpiEvaluateObject(object, (char*)"_DSM", &params, &out);
    if (acpi_status != AE_OK) {
        zxlogf(TRACE, "acpi: failed to fetch NHLT blob (acpi_status %u)\n", acpi_status);
        return acpi_to_zx_status(acpi_status);
    }

    ACPI_OBJECT* out_obj = out.Pointer;
    if (out_obj->Type != ACPI_TYPE_BUFFER) {
        zxlogf(ERROR, "acpi: unexpected object type (%u) for NHLT blob\n", out_obj->Type);
        status = ZX_ERR_INTERNAL;
        goto out;
    }

    ACPI_RESOURCE* res = NULL;
    acpi_status = AcpiBufferToResource(out_obj->Buffer.Pointer, out_obj->Buffer.Length, &res);
    if (acpi_status != AE_OK) {
        zxlogf(ERROR, "acpi: failed to parse NHLT resource (acpi_status %u)\n", acpi_status);
        status = acpi_to_zx_status(acpi_status);
        goto out;
    }

    if (res->Type != ACPI_RESOURCE_TYPE_ADDRESS64) {
        zxlogf(ERROR, "acpi: unexpected NHLT resource type (%u)\n", res->Type);
        status = ZX_ERR_INTERNAL;
        goto out;
    }

    zx_paddr_t paddr = (zx_paddr_t)res->Data.Address64.Address.Minimum;
    size_t size = (size_t)res->Data.Address64.Address.AddressLength;

    // Read the blob
    zx_handle_t vmo;
    status = zx_vmo_create_physical(get_root_resource(),
                                    ROUNDDOWN(paddr, PAGE_SIZE),
                                    ROUNDUP(size, PAGE_SIZE),
                                    &vmo);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi: failed to create NHLT VMO (res %d)\n", status);
        goto out;
    }

    // We cannot read physical VMOs directly and must map it
    zx_vaddr_t vaddr = 0;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo, 0, ROUNDUP(size, PAGE_SIZE),
                     ZX_VM_FLAG_PERM_READ, &vaddr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi: failed to map NHLT blob (res %d)\n", status);
        goto out;
    }
    void* nhlt = (void*)(vaddr + (paddr & (PAGE_SIZE-1)));

    // Publish the NHLT as metadata on the future PCI device node...
    // The canonical path to the PCI device is /dev/sys/pci/<b:d.f>
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/dev/sys/pci/%02x:%02x.%01x", bbn,
                                 (unsigned)((adr >> 16) & 0xFFFF), (unsigned)(adr & 0xFFFF));
    status = device_publish_metadata(dev, path, MD_KEY_NHLT, nhlt, size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "acpi: failed to publish NHLT metadata (res %d)\n", status);
    }

    zxlogf(TRACE, "acpi: published NHLT metadata for device at %s\n", path);

    zx_vmar_unmap(zx_vmar_root_self(), vaddr, ROUNDUP(size, PAGE_SIZE));
out:
    ACPI_FREE(out.Pointer);
    return status;
}
