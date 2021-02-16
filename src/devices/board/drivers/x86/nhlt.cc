// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nhlt.h"

#include <inttypes.h>
#include <lib/zircon-internal/align.h>
#include <limits.h>
#include <stdio.h>
#include <zircon/process.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>

#include "acpi-private.h"
#include "errors.h"

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
    /* 0008 */ 0xA4, 0x1F, 0x7B, 0x5D, 0xCE, 0x24, 0xC5, 0x53};

zx_status_t nhlt_publish_metadata(zx_device_t* dev, uint8_t bbn, uint64_t adr, ACPI_HANDLE object) {
  zx_status_t status = ZX_OK;

  // parameters
  ACPI_OBJECT objs[] = {
      {
          // uuid
          .Buffer =
              {
                  .Type = ACPI_TYPE_BUFFER,
                  .Length = sizeof(NHLT_UUID),
                  .Pointer = (uint8_t*)NHLT_UUID,
              },
      },
      {
          // revision id
          .Integer =
              {
                  .Type = ACPI_TYPE_INTEGER,
                  .Value = 1,
              },
      },
      {
          // function id
          .Integer =
              {
                  .Type = ACPI_TYPE_INTEGER,
                  .Value = 1,
              },
      },
  };
  ACPI_OBJECT_LIST params = {
      .Count = countof(objs),
      .Pointer = objs,
  };

  acpi::UniquePtr<ACPI_OBJECT> out_obj;
  {
    // output buffer
    ACPI_BUFFER out = {
        .Length = ACPI_ALLOCATE_BUFFER,
        .Pointer = nullptr,
    };

    // Fetch the NHLT resource
    ACPI_STATUS acpi_status = AcpiEvaluateObject(object, (char*)"_DSM", &params, &out);
    out_obj.reset(static_cast<ACPI_OBJECT*>(out.Pointer));
    if (acpi_status != AE_OK) {
      zxlogf(ERROR, "acpi: failed to fetch NHLT blob (acpi_status 0x%x)", acpi_status);
      return acpi_to_zx_status(acpi_status);
    }
  }

  if (out_obj->Type != ACPI_TYPE_BUFFER) {
    zxlogf(ERROR, "acpi: unexpected object type (%u) for NHLT blob", out_obj->Type);
    return ZX_ERR_INTERNAL;
  }

  ACPI_RESOURCE* res = NULL;
  ACPI_STATUS acpi_status = AcpiBufferToResource(
      out_obj->Buffer.Pointer, static_cast<uint16_t>(out_obj->Buffer.Length), &res);
  if (acpi_status != AE_OK) {
    zxlogf(ERROR, "acpi: failed to parse NHLT resource (acpi_status 0x%x)", acpi_status);
    return acpi_to_zx_status(acpi_status);
  }

  if (res->Type != ACPI_RESOURCE_TYPE_ADDRESS64) {
    zxlogf(ERROR, "acpi: unexpected NHLT resource type (%u)", res->Type);
    return ZX_ERR_INTERNAL;
  }

  zx_paddr_t paddr = (zx_paddr_t)res->Data.Address64.Address.Minimum;
  size_t size = (size_t)res->Data.Address64.Address.AddressLength;

  // Read the blob
  zx_handle_t vmo;
  zx_paddr_t page_start = ZX_ROUNDDOWN(paddr, zx_system_get_page_size());
  size_t page_offset = (paddr & (zx_system_get_page_size() - 1));
  size_t page_size = ZX_ROUNDUP(page_offset + size, zx_system_get_page_size());
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  status = zx_vmo_create_physical(get_root_resource(), page_start, page_size, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi: failed to create NHLT VMO (res %d)", status);
    return status;
  }

  // We cannot read physical VMOs directly and must map it
  zx_vaddr_t vaddr = 0;
  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, vmo, 0, page_size, &vaddr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi: failed to map NHLT blob (res %d)", status);
    return status;
  }
  void* nhlt = (void*)(vaddr + page_offset);

  // Publish the NHLT as metadata on the future PCI device node...
  // The canonical path to the PCI device is /dev/sys/pci/<b:d.f>
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/dev/sys/pci/%02x:%02x.%01x", bbn, (unsigned)((adr >> 16) & 0xFFFF),
           (unsigned)(adr & 0xFFFF));
  status = device_publish_metadata(dev, path, DEVICE_METADATA_ACPI_HDA_NHLT, nhlt, size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "acpi: failed to publish NHLT metadata (res %d)", status);
  }

  zxlogf(DEBUG, "acpi: published NHLT metadata for device at %s", path);

  zx_vmar_unmap(zx_vmar_root_self(), vaddr, ZX_ROUNDUP(size, zx_system_get_page_size()));

  return status;
}
