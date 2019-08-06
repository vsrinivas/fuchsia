// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <xefi.h>

#include <efi/protocol/pci-root-bridge-io.h>
#include <efi/types.h>

typedef struct {
  uint8_t descriptor;
  uint16_t len;
  uint8_t res_type;
  uint8_t gen_flags;
  uint8_t specific_flags;
  uint64_t addrspace_granularity;
  uint64_t addrrange_minimum;
  uint64_t addrrange_maximum;
  uint64_t addr_tr_offset;
  uint64_t addr_len;
} __attribute__((packed)) acpi_addrspace_desc64_t;

#define ACPI_ADDRESS_SPACE_DESCRIPTOR 0x8A
#define ACPI_END_TAG_DESCRIPTOR 0x79

#define ACPI_ADDRESS_SPACE_TYPE_BUS 0x02

typedef struct {
  uint16_t vid;
  uint16_t did;
  uint16_t cmd;
  uint16_t status;
  uint8_t rev_id;
  uint8_t class_code[3];
  uint8_t cache_line_size;
  uint8_t primary_lat_timer;
  uint8_t hdr_type;
  uint8_t bist;
  uint32_t bar[6];
  uint32_t cardbus_cis;
  uint16_t subid;
  uint16_t subvid;
  uint32_t exprom_bar;
  uint8_t cap_ptr;
  uint8_t reserved[7];
  uint8_t irq_line;
  uint8_t irq_pin;
  uint8_t min_grant;
  uint8_t max_lat;
} __attribute__((packed)) pci_common_header_t;

#define PCI_MAX_DEVICES 32
#define PCI_MAX_FUNCS 8

efi_status xefi_find_pci_mmio(efi_boot_services* bs, uint8_t cls, uint8_t sub, uint8_t ifc,
                              uint64_t* mmio) {
  size_t num_handles;
  efi_handle* handles;
  efi_status status =
      bs->LocateHandleBuffer(ByProtocol, &PciRootBridgeIoProtocol, NULL, &num_handles, &handles);
  if (EFI_ERROR(status)) {
    printf("Could not find PCI root bridge IO protocol: %s\n", xefi_strerror(status));
    return status;
  }

  for (size_t i = 0; i < num_handles; i++) {
    printf("handle %zu\n", i);
    efi_pci_root_bridge_io_protocol* iodev;
    status = bs->HandleProtocol(handles[i], &PciRootBridgeIoProtocol, (void**)&iodev);
    if (EFI_ERROR(status)) {
      printf("Could not get protocol for handle %zu: %s\n", i, xefi_strerror(status));
      continue;
    }
    acpi_addrspace_desc64_t* descriptors;
    status = iodev->Configuration(iodev, (void**)&descriptors);
    if (EFI_ERROR(status)) {
      printf("Could not get configuration for handle %zu: %s\n", i, xefi_strerror(status));
      continue;
    }

    uint16_t min_bus, max_bus;
    while (descriptors->descriptor != ACPI_END_TAG_DESCRIPTOR) {
      min_bus = (uint16_t)descriptors->addrrange_minimum;
      max_bus = (uint16_t)descriptors->addrrange_maximum;

      if (descriptors->res_type != ACPI_ADDRESS_SPACE_TYPE_BUS) {
        descriptors++;
        continue;
      }

      for (int bus = min_bus; bus <= max_bus; bus++) {
        for (int dev = 0; dev < PCI_MAX_DEVICES; dev++) {
          for (int func = 0; func < PCI_MAX_FUNCS; func++) {
            pci_common_header_t pci_hdr;
            bs->SetMem(&pci_hdr, sizeof(pci_hdr), 0);
            uint64_t address = (uint64_t)((bus << 24) + (dev << 16) + (func << 8));
            status =
                iodev->Pci.Read(iodev, EfiPciWidthUint16, address, sizeof(pci_hdr) / 2, &pci_hdr);
            if (EFI_ERROR(status)) {
              printf("could not read pci configuration for bus %d dev %d func %d: %s\n", bus, dev,
                     func, xefi_strerror(status));
              continue;
            }
            if (pci_hdr.vid == 0xffff)
              break;
            if ((pci_hdr.class_code[2] == cls) && (pci_hdr.class_code[1] == sub) &&
                (pci_hdr.class_code[0] == ifc)) {
              uint64_t n = ((uint64_t)pci_hdr.bar[0]) | ((uint64_t)pci_hdr.bar[1]) << 32UL;
              *mmio = n & 0xFFFFFFFFFFFFFFF0UL;
              status = EFI_SUCCESS;
              goto found_it;
            }
#if 0
                        printf("bus %04x dev %02x func %02x: ", bus, dev, func);
                        printf("VID: 0x%04x  DID: 0x%04x  Class: 0x%02x  Subclass: 0x%02x  Intf: 0x%02x\n",
                                pci_hdr.vid, pci_hdr.did, pci_hdr.class_code[2], pci_hdr.class_code[1],
                                pci_hdr.class_code[0]);
                        printf("     hdr_type: %02x\n", pci_hdr.hdr_type);
                        if ((pci_hdr.hdr_type & 0x7f) == 0x00) {
                            for (int bar = 0; bar < 6; bar++) {
                                if (pci_hdr.bar[bar]) {
                                    printf("     bar[%d]: 0x%08x\n", bar, pci_hdr.bar[bar]);
                                }
                                bool is64bit = (pci_hdr.bar[bar] & 0x06) == 0x04;
                                if (is64bit) {
                                    printf("     bar[%d]: 0x%08x\n", bar+1, pci_hdr.bar[bar+1]);
                                    bar++;
                                }
                                // TODO: get the BAR size
                                //   - disable IO
                                //   - write 1s
                                //   - read it back
                                //   - reset or map to somewhere else(?)
                            }
                        }
#endif
            if (!(pci_hdr.hdr_type & 0x80) && func == 0) {
              break;
            }
          }
        }
      }
      descriptors++;
    }
  }

  status = EFI_NOT_FOUND;
found_it:
  bs->FreePool(handles);
  return status;
}
