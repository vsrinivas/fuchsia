// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "avb.h"

#include <lib/zbi/zbi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/boot/image.h>
#include <zircon/hw/gpt.h>

#include <libavb/libavb.h>

#include "diskio.h"
#include "utf_conversion.h"

struct property_lookup_user_data {
  void* zbi;
  size_t zbi_size;
};

struct avb_user_ctx {
  disk_t bootloader_disk;
  efi_system_table* sys;
};

static bool PropertyLookupDescForeach(const AvbDescriptor* header, void* user_data);

struct find_partition_ctx {
  uint16_t* name_utf16;
  size_t name_utf16_len;
  uint16_t* dash_utf16;
  gpt_entry_t* partition_entry;
  size_t matches;
};

static bool find_partition_cb(void* ctx, const gpt_entry_t* partition) {
  struct find_partition_ctx* params = ctx;
  int result = memcmp(params->name_utf16, partition->name, params->name_utf16_len);
  if (result != 0) {
    // Try substituting '-' for '_'. The UTF-16 encoding of these is identical to their ASCII
    // counterpart.
    // This is necessary because workstation uses 'vbmeta_a' and 'zircon-a', but libavb expects
    // the A/B suffix to consistently use '_' or '-'.
    if (params->dash_utf16) {
      *params->dash_utf16 = L'_';
      result = memcmp(params->name_utf16, partition->name, params->name_utf16_len);
      *params->dash_utf16 = L'-';
    }
    if (result != 0) {
      return true;
    }
  }

  params->matches++;
  memcpy(params->partition_entry, partition, sizeof(*partition));
  return true;
}

// Find information about partition with name |partition|.
// This function will also look for a partition with the first '-' replace with '_', e.g.
// it would check for 'vbmeta-a', and if that doesn't exist, 'vbmeta_a'.
static int find_partition(struct avb_user_ctx* ctx, const char* partition,
                          gpt_entry_t* partition_entry) {
  if (strlen(partition) > GPT_NAME_LEN) {
    printf("Partition name %s is too long!\n", partition);
    return -1;
  }
  uint16_t name_utf16[GPT_NAME_LEN_U16];
  size_t name_utf16_len = sizeof(name_utf16);
  zx_status_t status =
      utf8_to_utf16((uint8_t*)partition, strlen(partition) + 1, name_utf16, &name_utf16_len);
  if (status != ZX_OK) {
    printf("%s: failed to convert name '%s' to UTF-16: %d\n", __func__, partition, status);
    return -1;
  }

  uint16_t* dash = NULL;
  for (size_t i = 0; i < name_utf16_len; i++) {
    if (name_utf16[i] == '-') {
      dash = &name_utf16[i];
      break;
    }
  }

  struct find_partition_ctx fp_ctx = {
      .name_utf16 = name_utf16,
      .name_utf16_len = name_utf16_len,
      .dash_utf16 = dash,
      .partition_entry = partition_entry,
      .matches = 0,
  };

  int ret = disk_scan_partitions(&ctx->bootloader_disk, false, find_partition_cb, &fp_ctx);
  if (ret == -1) {
    return -1;
  }
  return (fp_ctx.matches == 1) ? 0 : -1;
}

static AvbIOResult ReadFromPartition(AvbOps* ops, const char* partition, int64_t offset,
                                     size_t num_bytes, void* buffer, size_t* num_out_read) {
  struct avb_user_ctx* ctx = (struct avb_user_ctx*)ops->user_data;
  gpt_entry_t partition_entry;
  int ret = find_partition(ctx, partition, &partition_entry);
  if (ret == -1) {
    printf("%s: Failed to find partition %s\n", __func__, partition);
    return AVB_IO_RESULT_ERROR_NO_SUCH_PARTITION;
  }

  size_t partition_size =
      (partition_entry.last - partition_entry.first + 1) * ctx->bootloader_disk.blksz;
  if (offset + num_bytes > partition_size) {
    printf("%s: Tried to access range %" PRIu64 "-%" PRIu64 " of %s which is out of bounds \n",
           __func__, offset, offset + num_bytes, partition);
    return AVB_IO_RESULT_ERROR_RANGE_OUTSIDE_PARTITION;
  }

  // Convert partition-relative offset to a disk-relative offset for disk_read().
  uint64_t offs = partition_entry.first * ctx->bootloader_disk.blksz + offset;
  efi_status status = disk_read(&ctx->bootloader_disk, offs, buffer, num_bytes);
  if (status != EFI_SUCCESS) {
    return AVB_IO_RESULT_ERROR_IO;
  }

  *num_out_read = num_bytes;
  return AVB_IO_RESULT_OK;
}

static AvbIOResult WriteToPartition(AvbOps* ops, const char* partition, int64_t offset,
                                    size_t num_bytes, const void* buffer) {
  // Our usage of libavb should never be writing to a partition - this is only
  // used by the (deprecated) libavb_ab extension.
  printf("Errors: libavb write_to_partition() unimplemented\n");
  return AVB_IO_RESULT_ERROR_IO;
}

AvbIOResult ValidateVbmetaPublicKey(AvbOps* ops, const uint8_t* public_key_data,
                                    size_t public_key_length, const uint8_t* public_key_metadata,
                                    size_t public_key_metadata_length, bool* out_is_trusted) {
  // Stub - we trust all public keys.
  *out_is_trusted = true;
  return AVB_IO_RESULT_OK;
}

static AvbIOResult AvbReadRollbackIndex(AvbOps* ops, size_t rollback_index_location,
                                        uint64_t* out_rollback_index) {
  // Stub - we don't support rollback indexes.
  *out_rollback_index = 0;
  return AVB_IO_RESULT_OK;
}

static AvbIOResult AvbWriteRollbackIndex(AvbOps* ops, size_t rollback_index_location,
                                         uint64_t rollback_index) {
  // Stub - we don't support rollback indexes.
  return AVB_IO_RESULT_OK;
}

static AvbIOResult ReadIsDeviceUnlocked(AvbOps* ops, bool* out_is_unlocked) {
  *out_is_unlocked = true;
  return AVB_IO_RESULT_OK;
}

// avb_slot_verify uses this call to check that a partition exists.
// Checks for existence but ignores GUID because it's unused.
static AvbIOResult GetUniqueGuidForPartition(AvbOps* ops, const char* partition, char* guid_buf,
                                             size_t guid_buf_size) {
  struct avb_user_ctx* ctx = (struct avb_user_ctx*)ops->user_data;

  gpt_entry_t partition_entry;
  int ret = find_partition(ctx, partition, &partition_entry);
  if (ret == -1) {
    printf("%s: Failed to find partition %s\n", __func__, partition);
    return AVB_IO_RESULT_ERROR_NO_SUCH_PARTITION;
  }

  return AVB_IO_RESULT_OK;
}

static AvbIOResult GetSizeOfPartition(AvbOps* ops, const char* partition,
                                      uint64_t* out_size_num_bytes) {
  struct avb_user_ctx* ctx = (struct avb_user_ctx*)ops->user_data;

  gpt_entry_t partition_entry;
  int ret = find_partition(ctx, partition, &partition_entry);
  if (ret == -1) {
    printf("%s: Failed to find partition %s\n", __func__, partition);
    return AVB_IO_RESULT_ERROR_NO_SUCH_PARTITION;
  }

  *out_size_num_bytes =
      (partition_entry.last - partition_entry.first + 1) * ctx->bootloader_disk.blksz;
  return AVB_IO_RESULT_OK;
}

static void CreateAvbOps(AvbOps* avb_ops, struct avb_user_ctx* ctx) {
  avb_ops->user_data = ctx;
  avb_ops->atx_ops = NULL;  // We don't need ATX.
  avb_ops->read_from_partition = ReadFromPartition;
  avb_ops->get_preloaded_partition = NULL;
  avb_ops->write_to_partition = WriteToPartition;
  avb_ops->validate_vbmeta_public_key = ValidateVbmetaPublicKey;
  avb_ops->read_rollback_index = AvbReadRollbackIndex;
  avb_ops->write_rollback_index = AvbWriteRollbackIndex;
  avb_ops->read_is_device_unlocked = ReadIsDeviceUnlocked;
  avb_ops->get_unique_guid_for_partition = GetUniqueGuidForPartition;
  avb_ops->get_size_of_partition = GetSizeOfPartition;
  // As of now, persistent value are not needed yet for our use.
  avb_ops->read_persistent_value = NULL;
  avb_ops->write_persistent_value = NULL;
}

// Append ZBI items found in vbmeta to |zbi|.
void append_avb_zbi_items(efi_handle img, efi_system_table* sys, void* zbi, size_t zbi_size,
                          const char* ab_suffix) {
  AvbOps ops;
  disk_t disk;
  if (disk_find_boot(img, sys, false, &disk) != 0) {
    printf("Failed to find boot disk");
    return;
  }
  struct avb_user_ctx ctx = {
      .bootloader_disk = disk,
      .sys = sys,
  };
  CreateAvbOps(&ops, &ctx);
  struct property_lookup_user_data lookup_data = {
      .zbi = zbi,
      .zbi_size = zbi_size,
  };

  const char* const requested_partitions[] = {"zircon", NULL};

  AvbSlotVerifyData* verify_data;
  AvbSlotVerifyResult result = avb_slot_verify(&ops, requested_partitions, ab_suffix,
                                               AVB_SLOT_VERIFY_FLAGS_ALLOW_VERIFICATION_ERROR,
                                               AVB_HASHTREE_ERROR_MODE_LOGGING, &verify_data);
  if (result != AVB_SLOT_VERIFY_RESULT_OK) {
    printf("Failed to verify slot %s: %s\n", ab_suffix, avb_slot_verify_result_to_string(result));
    // We don't attempt to verify the vbmeta is valid.
  }

  if (verify_data == NULL) {
    // Don't fail boot if loading vbmeta failed for some reason.
    return;
  }

  for (size_t i = 0; i < verify_data->num_vbmeta_images; i++) {
    AvbVBMetaData* vb = &verify_data->vbmeta_images[i];
    if (!avb_descriptor_foreach(vb->vbmeta_data, vb->vbmeta_size, PropertyLookupDescForeach,
                                &lookup_data)) {
      printf("Fail to parse VBMETA properties\n");
      break;
    }
  }
  avb_slot_verify_data_free(verify_data);
}

// If the given property holds a ZBI container, appends its contents to the ZBI
// container in |lookup_data|.
static void ProcessProperty(const AvbPropertyDescriptor prop_desc, uint8_t* start,
                            const struct property_lookup_user_data* lookup_data) {
  const char* key = (const char*)start;
  if (key[prop_desc.key_num_bytes] != 0) {
    printf(
        "No terminating NUL byte in the property key."
        "Skipping this property descriptor.\n");
    return;
  }
  // Only look at properties whose keys start with the 'zbi' prefix.
  if (strncmp(key, "zbi", strlen("zbi"))) {
    return;
  }
  printf("Found vbmeta ZBI property '%s' (%" PRIu64 " bytes)\n", key, prop_desc.value_num_bytes);

  // We don't care about the key. Move value data to the start address to make sure
  // that the zbi item starts from an aligned address.
  uint64_t value_offset, value_size;
  if (!avb_safe_add(&value_offset, prop_desc.key_num_bytes, 1) ||
      !avb_safe_add(&value_size, prop_desc.value_num_bytes, 1)) {
    printf(
        "Overflow while computing offset and size for value."
        "Skipping this property descriptor.\n");
    return;
  }
  memmove(start, start + value_offset, value_size);

  const zbi_header_t* vbmeta_zbi = (const zbi_header_t*)start;

  const uint64_t zbi_size = sizeof(zbi_header_t) + vbmeta_zbi->length;
  if (zbi_size > prop_desc.value_num_bytes) {
    printf("vbmeta ZBI length exceeds property size (%" PRIu64 " > %" PRIu64 ")\n", zbi_size,
           prop_desc.value_num_bytes);
    return;
  }

  zbi_result_t result = zbi_check(vbmeta_zbi, NULL);
  if (result != ZBI_RESULT_OK) {
    printf("Mal-formed vbmeta ZBI: error %d\n", result);
    return;
  }

  result = zbi_extend(lookup_data->zbi, lookup_data->zbi_size, vbmeta_zbi);
  if (result != ZBI_RESULT_OK) {
    printf("Failed to add vbmeta ZBI: error %d\n", result);
    return;
  }
}

// Callback for vbmeta property iteration. |user_data| must be a pointer to a
// property_lookup_user_data struct.
static bool PropertyLookupDescForeach(const AvbDescriptor* header, void* user_data) {
  AvbPropertyDescriptor prop_desc;
  if (header->tag == AVB_DESCRIPTOR_TAG_PROPERTY &&
      avb_property_descriptor_validate_and_byteswap((const AvbPropertyDescriptor*)header,
                                                    &prop_desc)) {
    ProcessProperty(prop_desc, (uint8_t*)header + sizeof(AvbPropertyDescriptor),
                    (struct property_lookup_user_data*)user_data);
  }
  return true;
}
