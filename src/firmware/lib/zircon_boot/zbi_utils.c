// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef ZIRCON_BOOT_CUSTOM_SYSDEPS_HEADER
#include <zircon_boot_sysdeps.h>
#else
#include <string.h>
#endif

#include <lib/zircon_boot/zbi_utils.h>
#include <lib/zircon_boot/zircon_boot.h>
#include <zircon/boot/bootfs.h>

#include "utils.h"

zbi_result_t AppendCurrentSlotZbiItem(zbi_header_t* image, size_t capacity, AbrSlotIndex slot) {
  char buffer[] = "zvb.current_slot=__";
  const char* suffix = AbrGetSlotSuffix(slot);
  if (strlen(suffix) != 2) {
    zircon_boot_dlog("unexpected suffix format %s\n", suffix);
    return ZBI_RESULT_ERROR;
  }
  buffer[sizeof(buffer) - 2] = suffix[1];
  return zbi_create_entry_with_payload(image, capacity, ZBI_TYPE_CMDLINE, 0, 0, buffer,
                                       sizeof(buffer));
}

zbi_result_t AppendZbiFile(zbi_header_t* zbi, size_t capacity, const char* name,
                           const void* file_data, size_t file_data_size) {
  size_t name_len = strlen(name);
  if (name_len > 0xFFU) {
    zircon_boot_dlog("ZBI filename too long");
    return ZBI_RESULT_ERROR;
  }

  size_t payload_length = 1 + name_len + file_data_size;
  if (payload_length < file_data_size) {
    zircon_boot_dlog("ZBI file data too large");
    return ZBI_RESULT_TOO_BIG;
  }

  uint8_t* payload = NULL;
  zbi_result_t result = zbi_create_entry(zbi, capacity, ZBI_TYPE_BOOTLOADER_FILE, 0, 0,
                                         payload_length, (void**)&payload);
  if (result != ZBI_RESULT_OK) {
    zircon_boot_dlog("Failed to create ZBI file entry: %d\n", result);
    return result;
  }
  payload[0] = (uint8_t)name_len;
  memcpy(&payload[1], name, name_len);
  memcpy(&payload[1 + name_len], file_data, file_data_size);

  return ZBI_RESULT_OK;
}

// AddFactoryFile is a helper function to AddBootfsFactoryFiles. It appends a directory entry
// and the payload in an initialized bootfs zbi item.
//
// @filename: Name of the file to add.
// @read_factory: A function pointer that returns the payload according to file name in `file_names`
// @read_factory_context: Caller data that `read_factory` will be called with.
// @data_off: Offset of the payload address for this file relative to bootfs header
// @direntry_cur: A double pointer, pointing to a pointer that stores the address of the directory
//   entry to append the entry for this file. It will be updated to the next entry for the next file
//   after the file is successfully appended.
// @payload_cur: A double pointer, pointing to a pointer that stores the address of the payload for
//   this file. It will be updated to the payload entry for the next file after the file is
//   successfully appended.
// @data_len: An output pointer storing the length of the payload data appended.
//
// Returns ZBI_RESULT_OK on success. Error code otherwise.
static zbi_result_t AddFactoryFile(const char* filename, read_factory_t read_factory,
                                   void* read_factory_context, uint32_t data_off,
                                   uint32_t max_bootfs_size, uint8_t** direntry_cur,
                                   uint8_t** payload_cur, uint32_t* data_len) {
  const uint32_t name_len = (uint32_t)strlen(filename) + 1;
  const uint32_t max_data_size = (max_bootfs_size - data_off) & ~(ZBI_BOOTFS_PAGE_SIZE - 1);

  size_t len_read;
  if (!read_factory(read_factory_context, filename, max_data_size, *payload_cur, &len_read)) {
    zircon_boot_dlog("Failed to read file %s \n", filename);
    return ZBI_RESULT_ERROR;
  }

  if (len_read > max_data_size) {
    zircon_boot_dlog("File size overloads max_data_size = %u\n", max_data_size);
    return ZBI_RESULT_TOO_BIG;
  }

  *data_len = (uint32_t)len_read;
  *payload_cur += *data_len;

  uint32_t data_pad_size = ZBI_BOOTFS_PAGE_ALIGN(*data_len) - *data_len;
  memset(*payload_cur, 0, data_pad_size);
  *payload_cur += data_pad_size;

  // The caller has already reserved space for the dirent structures and
  // filenames in the buffer, so we don't need to check for overflow here.
  zbi_bootfs_dirent_t* entry_hdr = (zbi_bootfs_dirent_t*)(*direntry_cur);
  entry_hdr->name_len = name_len;
  entry_hdr->data_len = *data_len;
  entry_hdr->data_off = data_off;

  *direntry_cur += offsetof(zbi_bootfs_dirent_t, name);
  memcpy(entry_hdr->name, filename, name_len);
  *direntry_cur += name_len;

  uint32_t full_dirent_size = ZBI_BOOTFS_DIRENT_SIZE(name_len);
  uint32_t after_name_offset = (uint32_t)offsetof(zbi_bootfs_dirent_t, name[name_len]);
  uint32_t dirent_pad_size = full_dirent_size - after_name_offset;
  memset(*direntry_cur, 0, dirent_pad_size);
  *direntry_cur += dirent_pad_size;

  return 0;
}

// AddBootfsFactoryFiles is a helper function to AppendBootfsFactoryFiles. It initializes the
// bootfsheader and calls add_factory_file for all files in the given file list.
//
// @bootfs_header: buffer pointer for creating the bootfs zbi item.
// @max_bootfs_size: Maximum size of the buffer.
// @file_names: Names of the files.
// @file_count: Size of `file_names`
// @read_factory: A function pointer that returns the payload according to file name in `file_names`
// @read_factory_context: Caller data that `read_factory` will be called with.
// @bootfs_size: Actual created bootfs size
//
// Returns ZBI_RESULT_OK on success. Error code otherwise.
static zbi_result_t AddBootfsFactoryFiles(zbi_bootfs_header_t* bootfs_header,
                                          uint32_t max_bootfs_size, const char** file_names,
                                          size_t file_count, read_factory_t read_factory,
                                          void* read_factory_context, uint32_t* bootfs_size) {
  if (sizeof(*bootfs_header) > max_bootfs_size) {
    zircon_boot_dlog("ERROR: can't fit bootfs header in ZBI payload (%zu > %u)\n",
                     sizeof(*bootfs_header), max_bootfs_size);
    return ZBI_RESULT_TOO_BIG;
  }

  // Determine how much space is needed to store all of the directory entries.
  uint32_t dirsize = 0;

  for (size_t i = 0; i < file_count; i++) {
    size_t file_name_size = strlen(file_names[i]) + 1;
    if (file_name_size == 1 || file_name_size > ZBI_BOOTFS_MAX_NAME_LEN) {
      zircon_boot_dlog("Invalid file name size: %s, must be between 1 and %d characters\n",
                       file_names[i], ZBI_BOOTFS_MAX_NAME_LEN - 1);
      return ZBI_RESULT_ERROR;
    }
    uint32_t new_dirsize = dirsize + (uint32_t)ZBI_BOOTFS_DIRENT_SIZE(file_name_size);
    if (new_dirsize < dirsize) {
      zircon_boot_dlog("directory entry size overflow\n");
      return ZBI_RESULT_TOO_BIG;
    }
    dirsize = new_dirsize;
  }

  bootfs_header->magic = ZBI_BOOTFS_MAGIC;
  bootfs_header->dirsize = dirsize;
  bootfs_header->reserved0 = 0;
  bootfs_header->reserved1 = 0;

  uint32_t dir_entries_end_offset = dirsize + sizeof(zbi_bootfs_header_t);
  uint32_t data_off = ZBI_BOOTFS_PAGE_ALIGN(dir_entries_end_offset);
  if (data_off < dirsize) {
    zircon_boot_dlog("ERROR: bootfs dirsize overflow (dirsize = %u, data_off = %u)\n", dirsize,
                     data_off);
    return ZBI_RESULT_TOO_BIG;
  }
  if (data_off > max_bootfs_size) {
    zircon_boot_dlog("ERROR: can't fit bootfs dir entries in ZBI payload (%u > %u)\n", data_off,
                     max_bootfs_size);
    return ZBI_RESULT_ERROR;
  }

  uint8_t* header_start = (uint8_t*)bootfs_header;
  uint8_t* entry_start_ptr = header_start + sizeof(zbi_bootfs_header_t);
  uint8_t* entry_cur_ptr = entry_start_ptr;
  uint8_t* payload_padding_start = header_start + dir_entries_end_offset;
  uint8_t* payload_cur = header_start + data_off;

  // Initialize dir entry part and payload padding part to 0.
  memset(entry_start_ptr, 0, (size_t)(payload_cur - entry_start_ptr));

  uint32_t i;
  for (i = 0; i < file_count; i++) {
    const char* const filename = file_names[i];
    uint32_t data_len = 0;

    payload_padding_start = payload_cur;
    int ret = AddFactoryFile(filename, read_factory, read_factory_context, data_off,
                             max_bootfs_size, &entry_cur_ptr, &payload_cur, &data_len);
    if (ret != 0) {
      // Log the error, but try to keep going and add the rest of the
      // factory files.
      zircon_boot_dlog("ERROR: failed to add factory file %s\n", filename);
    }
    payload_padding_start += data_len;

    data_off += ZBI_BOOTFS_PAGE_ALIGN(data_len);
    memset(payload_padding_start, 0, (size_t)(payload_cur - payload_padding_start));
  }

  // Use the real dir entry size. Because some files may have failed to load.
  bootfs_header->dirsize = (uint32_t)(entry_cur_ptr - entry_start_ptr);

  *bootfs_size = data_off;
  return ZBI_RESULT_OK;
}

// Appends a factory bootfs zbi items containing a list of factory file items.
zbi_result_t AppendBootfsFactoryFiles(zbi_header_t* zbi, size_t capacity, const char** file_names,
                                      size_t file_count, read_factory_t read_factory,
                                      void* read_factory_context) {
  // Add an empty ZBI header for factory data. Factory data is formatted as BOOTFS.
  // BOOTFS is a trivial "filesystem" format.
  //
  // It consists of a zbi_bootfs_header_t followed by a series of zbi_bootfs_dirent_t structs.
  // After the zbi_bootfs_dirent_t structs, file data is placed.
  // File data offsets are page aligned (multiple of 4096).
  // zbi_bootfs_dirent_t structs start on uint32 boundaries.
  void* payload = NULL;
  uint32_t max_payload_size = 0;
  zbi_result_t result = zbi_get_next_entry_payload(zbi, capacity, &payload, &max_payload_size);
  if (result != ZBI_RESULT_OK) {
    zircon_boot_dlog("zbi_get_next_entry_payload() failed: %d\n", result);
    return result;
  }

  // Mark the start of the zbi_bootfs_header_t.
  // This header is bootfs-specific and stores data related to the number of
  // directory entries.
  uint32_t bootfs_size = 0;

  result = AddBootfsFactoryFiles((zbi_bootfs_header_t*)payload, max_payload_size, file_names,
                                 file_count, read_factory, read_factory_context, &bootfs_size);
  if (result != ZBI_RESULT_OK) {
    zircon_boot_dlog("ERROR: add_bootfs_factory_files() failed\n");
    return result;
  }

  // Finally, add the ZBI item using the newly created payload.
  result =
      zbi_create_entry(zbi, capacity, ZBI_TYPE_STORAGE_BOOTFS_FACTORY, 0, 0, bootfs_size, NULL);
  if (result != ZBI_RESULT_OK) {
    zircon_boot_dlog("ERROR: zbi_create_entry() failed: %d\n", result);
    return result;
  }
  return ZBI_RESULT_OK;
}
