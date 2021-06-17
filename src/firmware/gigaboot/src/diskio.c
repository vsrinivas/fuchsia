// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <diskio.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/hw/gpt.h>
#include <zircon/types.h>

#include <efi/boot-services.h>
#include <efi/protocol/block-io.h>
#include <efi/protocol/device-path-to-text.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/disk-io.h>
#include <efi/protocol/loaded-image.h>
#include <efi/protocol/simple-file-system.h>

#ifdef GIGABOOT_HOST
#include <host/stubs.h>
#endif

#include "osboot.h"
#include "utf_conversion.h"

static bool path_node_match(efi_device_path_protocol* a, efi_device_path_protocol* b) {
  size_t alen = a->Length[0] | (a->Length[1] << 8);
  size_t blen = b->Length[0] | (b->Length[1] << 8);
  if (alen != blen) {
    return false;
  }
  if (memcmp(a, b, alen)) {
    return false;
  }
  return true;
}

static efi_device_path_protocol* path_node_next(efi_device_path_protocol* node) {
  if (node->Type == DEVICE_PATH_END) {
    return NULL;
  }
  return ((void*)node) + (node->Length[0] | (node->Length[1] << 8));
}

static bool path_prefix_match(efi_device_path_protocol* path, efi_device_path_protocol* prefix) {
  if ((path == NULL) || (prefix == NULL)) {
    return false;
  }
  for (;;) {
    if (prefix->Type == DEVICE_PATH_END) {
      return true;
    }
    if (!path_node_match(path, prefix)) {
      return false;
    }
    if ((path = path_node_next(path)) == NULL) {
      return false;
    }
    prefix = path_node_next(prefix);
  }
}

static void print_path(efi_boot_services* bs, efi_device_path_protocol* path) {
  efi_device_path_to_text_protocol* ptt;
  efi_status status = bs->LocateProtocol(&DevicePathToTextProtocol, NULL, (void**)&ptt);
  if (status != EFI_SUCCESS) {
    printf("<cannot print path>");
    return;
  }
  char16_t* txt = ptt->ConvertDevicePathToText(path, false, false);
  if (txt == NULL) {
    printf("<cannot print path>");
    return;
  }
  puts16(txt);
  printf("\n");
  bs->FreePool(txt);
}

efi_status disk_read(const disk_t* disk, size_t offset, void* data, size_t length) {
  if (disk->first > disk->last) {
    return EFI_VOLUME_CORRUPTED;
  }

  uint64_t size = (disk->last - disk->first) * disk->blksz;
  if ((offset > size) || ((size - offset) < length)) {
    printf("ERROR: Disk read invalid params. offset:%zu length:%zu disk: [%" PRIu64 " to %" PRIu64
           "] size:%" PRIu64 " blksz:%d\n",
           offset, length, disk->first, disk->last, size, disk->blksz);
    return EFI_INVALID_PARAMETER;
  }

  return disk->io->ReadDisk(disk->io, disk->id, (disk->first * disk->blksz) + offset, length, data);
}

efi_status disk_write(disk_t* disk, size_t offset, void* data, size_t length) {
  if (disk->first > disk->last) {
    return EFI_VOLUME_CORRUPTED;
  }

  uint64_t size = (disk->last - disk->first) * disk->blksz;
  if ((offset > size) || ((size - offset) < length)) {
    return EFI_INVALID_PARAMETER;
  }

  return disk->io->WriteDisk(disk->io, disk->id, (disk->first * disk->blksz) + offset, length,
                             data);
}

static void disk_close(disk_t* disk) {
  disk->bs->CloseProtocol(disk->h, &DiskIoProtocol, disk->img, NULL);
}

bool is_booting_from_usb(efi_handle img, efi_system_table* sys) {
  bool result = false;
  efi_boot_services* bs = sys->BootServices;
  efi_status status;
  efi_loaded_image_protocol* li;
  status = bs->OpenProtocol(img, &LoadedImageProtocol, (void**)&li, img, NULL,
                            EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  if (status != EFI_SUCCESS) {
    return -1;
  }

  efi_device_path_protocol* imgdevpath;
  status = bs->OpenProtocol(li->DeviceHandle, &DevicePathProtocol, (void**)&imgdevpath, img, NULL,
                            EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  if (status != EFI_SUCCESS) {
    goto fail_open_devpath;
  }

  while (imgdevpath != NULL) {
    if (imgdevpath->Type == DEVICE_PATH_MESSAGING) {
      switch (imgdevpath->SubType) {
        case DEVICE_PATH_MESSAGING_USB:
        case DEVICE_PATH_MESSAGING_USB_LUN:
        case DEVICE_PATH_MESSAGING_USB_WWID:
        case DEVICE_PATH_MESSAGING_USB_CLASS:
          result = true;
          break;
      }
    }

    imgdevpath = path_node_next(imgdevpath);
  }

  bs->CloseProtocol(li->DeviceHandle, &DevicePathProtocol, img, NULL);

fail_open_devpath:
  bs->CloseProtocol(img, &LoadedImageProtocol, img, NULL);

  return result;
}

int disk_find_boot(efi_handle img, efi_system_table* sys, bool verbose, disk_t* disk) {
  bool found = false;
  efi_boot_services* bs = sys->BootServices;
  efi_handle* list;
  size_t count;
  efi_status status;
  efi_loaded_image_protocol* li;

  status = bs->OpenProtocol(img, &LoadedImageProtocol, (void**)&li, img, NULL,
                            EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  if (status != EFI_SUCCESS) {
    return -1;
  }

  efi_device_path_protocol* imgdevpath;
  status = bs->OpenProtocol(li->DeviceHandle, &DevicePathProtocol, (void**)&imgdevpath, img, NULL,
                            EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
  if (status != EFI_SUCCESS) {
    goto fail_open_devpath;
  }

  if (verbose) {
    printf("BootLoader Path: ");
    print_path(bs, li->FilePath);
    printf("BootLoader Device: ");
    print_path(bs, imgdevpath);
  }

  status = bs->LocateHandleBuffer(ByProtocol, &BlockIoProtocol, NULL, &count, &list);
  if (status != EFI_SUCCESS) {
    printf("find_boot_disk() - no block io devices found\n");
    goto fail_get_list;
  }

  for (size_t n = 0; n < count; n++) {
    efi_block_io_protocol* bio;
    status = bs->OpenProtocol(list[n], &BlockIoProtocol, (void**)&bio, img, NULL,
                              EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (status != EFI_SUCCESS) {
      continue;
    }

    efi_device_path_protocol* path;
    status = bs->OpenProtocol(list[n], &DevicePathProtocol, (void**)&path, img, NULL,
                              EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (status != EFI_SUCCESS) {
      bs->CloseProtocol(list[n], &BlockIoProtocol, img, NULL);
      continue;
    }

    bool match = false;

    // if a non-logical partition, check for match
    if (!bio->Media->LogicalPartition && bio->Media->MediaPresent) {
      match = path_prefix_match(imgdevpath, path);
    }

    if (verbose) {
      printf("BlockIO Device: ");
      print_path(bs, path);
      printf("              : #%zu, %" PRIu64 "MB%s%s%s%s%s%s\n", n,
             bio->Media->LastBlock * bio->Media->BlockSize / 1024 / 1024,
             bio->Media->RemovableMedia ? " Removable" : "",
             bio->Media->MediaPresent ? " Present" : "",
             bio->Media->LogicalPartition ? " Logical" : "", bio->Media->ReadOnly ? " RO" : "",
             bio->Media->WriteCaching ? " WC" : "", match ? " BootDevice" : "");
    }

    if (match && !found) {
      status = bs->OpenProtocol(list[n], &DiskIoProtocol, (void**)&disk->io, img, NULL,
                                EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
      if (status != EFI_SUCCESS) {
        printf("find_boot_disk() - cannot get disk io protocol\n");
      } else {
        disk->first = 0;
        disk->last = bio->Media->LastBlock;
        disk->id = bio->Media->MediaId;
        disk->blksz = bio->Media->BlockSize;
        disk->h = list[n];
        disk->img = img;
        disk->bs = bs;
        found = true;
      }
    }

    bs->CloseProtocol(list[n], &BlockIoProtocol, img, NULL);
    bs->CloseProtocol(list[n], &DevicePathProtocol, img, NULL);
  }

  bs->FreePool(list);

fail_get_list:
  bs->CloseProtocol(li->DeviceHandle, &DevicePathProtocol, img, NULL);

fail_open_devpath:
  bs->CloseProtocol(img, &LoadedImageProtocol, img, NULL);

  return found ? 0 : -1;
}

int disk_scan_partitions(const disk_t* disk, bool verbose, partition_matcher_cb matcher,
                         void* matcher_ctx) {
  // Block 0 is MBR, read from block 1 to get GPT header.
  gpt_header_t gpt;
  efi_status status = disk_read(disk, disk->blksz, &gpt, sizeof(gpt));
  if (status != EFI_SUCCESS) {
    return -1;
  }

  if (verbose) {
    printf("gpt: size:    %u\n", gpt.size);
    printf("gpt: current: %" PRIu64 "\n", gpt.current);
    printf("gpt: backup:  %" PRIu64 "\n", gpt.backup);
    printf("gpt: first:   %" PRIu64 "\n", gpt.first);
    printf("gpt: last:    %" PRIu64 "\n", gpt.last);
    printf("gpt: entries: %" PRIu64 "\n", gpt.entries);
    printf("gpt: e.count: %u\n", gpt.entries_count);
    printf("gpt: e.size:  %u\n", gpt.entries_size);
  }

  // TODO(69527): checksum validation and backup GPT support.
  if ((gpt.magic != GPT_MAGIC) || (gpt.size != GPT_HEADER_SIZE) ||
      (gpt.entries_size != GPT_ENTRY_SIZE) || (gpt.entries_count > 256)) {
    printf("gpt - malformed header\n");
    return -1;
  }

  // Allocate memory to hold the partition entry table and read it from disk.
  gpt_entry_t* table;
  size_t tsize = gpt.entries_count * gpt.entries_size;
  status = disk->bs->AllocatePool(EfiLoaderData, tsize, (void**)&table);
  if (status != EFI_SUCCESS) {
    printf("gpt - allocation failure\n");
    return -1;
  }

  status = disk_read(disk, disk->blksz * gpt.entries, table, tsize);
  if (status != EFI_SUCCESS) {
    disk->bs->FreePool(table);
    printf("gpt - io error\n");
    return -1;
  }

  for (unsigned n = 0; n < gpt.entries_count; n++) {
    if ((table[n].first == 0) || (table[n].last == 0) || (table[n].last < table[n].first)) {
      // Ignore empty or bogus entries.
      continue;
    }

    if (verbose) {
      // Convert UTF-16 partition name to UTF-8 for printing. This assumes
      // the name will actually be basic ASCII and might truncate if not,
      // but it's fine for debug purposes.
      char gpt_name[GPT_NAME_LEN / 2] = "<unknown>";
      size_t gpt_name_length = sizeof(gpt_name);
      utf16_to_utf8((const uint16_t*)table[n].name, GPT_NAME_LEN_U16, (uint8_t*)gpt_name,
                    &gpt_name_length);
      gpt_name[GPT_NAME_LEN / 2 - 1] = '\0';

      printf("#%03d %" PRIu64 "..%" PRIu64 " %16s %" PRIx64 "\n", n, table[n].first, table[n].last,
             gpt_name, table[n].flags);
    }
    if (!matcher(matcher_ctx, &table[n])) {
      break;
    }
  }

  disk->bs->FreePool(table);
  return 0;
}

struct disk_find_partition_ctx {
  const uint8_t* type_guid;
  const uint8_t* part_guid;
  const uint16_t* part_name;
  const size_t part_name_len;
  gpt_entry_t* partition_out;
  size_t matches;
};

static bool disk_find_partition_cb(void* ctx, const gpt_entry_t* partition) {
  struct disk_find_partition_ctx* params = (struct disk_find_partition_ctx*)ctx;
  if ((!params->type_guid || memcmp(partition->type, params->type_guid, GPT_GUID_LEN) == 0) &&
      (!params->part_guid || memcmp(partition->guid, params->part_guid, GPT_GUID_LEN) == 0) &&
      (!params->part_name ||
       memcmp(partition->name, params->part_name, params->part_name_len) == 0)) {
    memcpy(params->partition_out, partition, sizeof(*partition));
    params->matches++;
    return true;
  }

  return true;
}

int disk_find_partition(const disk_t* disk, bool verbose, const uint8_t* type, const uint8_t* guid,
                        const char* name, gpt_entry_t* partition) {
  // If the user gave a name, convert to UTF-16 so we can compare it to
  // the GPT entry directly with memcmp().
  uint16_t name_utf16[GPT_NAME_LEN_U16];
  size_t name_utf16_len = sizeof(name_utf16);
  if (name) {
    zx_status_t status =
        utf8_to_utf16((const uint8_t*)name, strlen(name) + 1, name_utf16, &name_utf16_len);
    if (status != ZX_OK) {
      printf("gpt - failed to convert name '%s' to UTF-16: %d\n", name, status);
      return -1;
    }
  }

  struct disk_find_partition_ctx ctx = {
      .type_guid = type,
      .part_guid = guid,
      .part_name = name ? name_utf16 : NULL,
      .part_name_len = name_utf16_len,
      .partition_out = partition,
      .matches = 0,
  };

  int ret = disk_scan_partitions(disk, verbose, disk_find_partition_cb, &ctx);
  if (ret == -1) {
    return -1;
  }
  return (ctx.matches == 1) ? 0 : -1;
}

void* image_load_from_disk(efi_handle img, efi_system_table* sys, size_t extra_space, size_t* _sz,
                           const uint8_t* guid_value, const char* guid_name) {
  static bool verbose = false;
  uint8_t sector[512];
  efi_boot_services* bs = sys->BootServices;
  disk_t disk;

  if (disk_find_boot(img, sys, verbose, &disk) < 0) {
    printf("Cannot find bootloader disk.\n");
    return NULL;
  }

  gpt_entry_t partition;
  if (disk_find_partition(&disk, verbose, guid_value, NULL, NULL, &partition)) {
    printf("Cannot find %s partition on bootloader disk.\n", guid_name);
    goto fail0;
  }
  const uint64_t partition_offset = partition.first * disk.blksz;

  efi_status status = disk_read(&disk, partition_offset, sector, 512);
  if (status != EFI_SUCCESS) {
    printf("Failed to read disk: %zu\n", status);
    goto fail0;
  }

  size_t sz = image_getsize(sector, 512);
  if (sz == 0) {
    printf("%s partition has no valid header\n", guid_name);
    goto fail0;
  }

  size_t pages = (sz + 4095) / 4096;
  pages += (extra_space + 4095) / 4096;
  void* image;
  status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, (efi_physical_addr*)&image);
  if (status != EFI_SUCCESS) {
    printf("Failed to allocate %zu bytes to load %s image\n", sz, guid_name);
    goto fail0;
  }

  status = disk_read(&disk, partition_offset, image, sz);
  if (status != EFI_SUCCESS) {
    printf("Failed to read image from %s partition\n", guid_name);
    goto fail1;
  }

  if (!image_is_valid(image, sz)) {
    printf("%s partition has no valid image\n", guid_name);
    goto fail1;
  }

  *_sz = sz + extra_space;
  return image;

fail1:
  bs->FreePages((efi_physical_addr)image, pages);
fail0:
  disk_close(&disk);
  return NULL;
}

efi_status read_partition(efi_handle img, efi_system_table* sys, const uint8_t* guid_value,
                          const char* guid_name, uint64_t offset, unsigned char* data,
                          size_t size) {
  static bool verbose = false;
  disk_t disk;

  if (disk_find_boot(img, sys, verbose, &disk) < 0) {
    printf("Cannot find bootloader disk.\n");
    return EFI_NOT_FOUND;
  }

  gpt_entry_t partition;
  if (disk_find_partition(&disk, verbose, guid_value, NULL, NULL, &partition)) {
    printf("Cannot find %s partition on bootloader disk.\n", guid_name);
    disk_close(&disk);
    return EFI_NOT_FOUND;
  }
  const uint64_t partition_offset = partition.first * disk.blksz;

  efi_status status = disk_read(&disk, offset + partition_offset, data, size);
  disk_close(&disk);
  return status;
}

efi_status write_partition(efi_handle img, efi_system_table* sys, const uint8_t* guid_value,
                           const char* guid_name, uint64_t offset, const unsigned char* data,
                           size_t size) {
  static bool verbose = false;
  disk_t disk;

  if (disk_find_boot(img, sys, verbose, &disk) < 0) {
    printf("Cannot find bootloader disk.\n");
    return EFI_NOT_FOUND;
  }

  gpt_entry_t partition;
  if (disk_find_partition(&disk, verbose, guid_value, NULL, NULL, &partition)) {
    printf("Cannot find %s partition on bootloader disk.\n", guid_name);
    disk_close(&disk);
    return EFI_NOT_FOUND;
  }
  const uint64_t partition_offset = partition.first * disk.blksz;

  efi_status status = disk_write(&disk, offset + partition_offset, (void*)data, size);
  disk_close(&disk);
  return status;
}

// Mapping from either legacy or new partition naming scheme to the expected
// on-disk type GUID.
static const struct {
  const char* legacy_name;
  const char* name;
  const uint8_t type_guid[GPT_GUID_LEN];
} partition_map[] = {
    {
        .legacy_name = GUID_ZIRCON_A_NAME,
        .name = GPT_ZIRCON_A_NAME,
        .type_guid = GUID_ZIRCON_A_VALUE,
    },
    {
        .legacy_name = GUID_ZIRCON_B_NAME,
        .name = GPT_ZIRCON_B_NAME,
        .type_guid = GUID_ZIRCON_B_VALUE,
    },
    {
        .legacy_name = GUID_ZIRCON_R_NAME,
        .name = GPT_ZIRCON_R_NAME,
        .type_guid = GUID_ZIRCON_R_VALUE,
    },
    // Note: even though both vbmeta names are actually the same, still check
    // both constants here to avoid depending on this always being true.
    {
        .legacy_name = GUID_VBMETA_A_NAME,
        .name = GPT_VBMETA_A_NAME,
        .type_guid = GUID_VBMETA_A_VALUE,
    },
    {
        .legacy_name = GUID_VBMETA_B_NAME,
        .name = GPT_VBMETA_B_NAME,
        .type_guid = GUID_VBMETA_B_VALUE,
    },
    {
        .legacy_name = GUID_VBMETA_R_NAME,
        .name = GPT_VBMETA_R_NAME,
        .type_guid = GUID_VBMETA_R_VALUE,
    },
    {
        .legacy_name = GUID_FVM_NAME,
        .name = GPT_FVM_NAME,
        .type_guid = GUID_FVM_VALUE,
    },
    {
        .legacy_name = GUID_EFI_NAME,
        // No bootloader_{a,b,r} support, just use standard "bootloader".
        .name = "bootloader",
        .type_guid = GUID_EFI_VALUE,
    },
};

const uint8_t* partition_type_guid(const char* name) {
  for (size_t i = 0; i < countof(partition_map); ++i) {
    if (strcmp(partition_map[i].legacy_name, name) == 0 ||
        strcmp(partition_map[i].name, name) == 0) {
      return partition_map[i].type_guid;
    }
  }

  return NULL;
}
