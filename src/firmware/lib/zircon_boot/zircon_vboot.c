/** Copyright 2020 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef ZIRCON_BOOT_CUSTOM_SYSDEPS_HEADER
#include <zircon_boot_sysdeps.h>
#else
#include <assert.h>
#include <string.h>
#endif

#include <lib/zbi/zbi.h>

#include <libavb/libavb.h>
#include <libavb_atx/libavb_atx.h>

#include "utils.h"
#include "zircon_vboot.h"

#define AVB_ATX_NUM_KEY_VERSIONS 2
#define ROLLBACK_INDEX_NOT_USED 0

typedef struct VBootContext {
  struct {
    size_t location;
    uint64_t value;
  } key_versions[AVB_ATX_NUM_KEY_VERSIONS];
  size_t next_key_version_index;
  zbi_header_t* preloaded_image;
  ZirconBootOps* ops;
} VBootContext;

static AvbIOResult GetPreloadedPartition(AvbOps* ops, const char* partition, size_t num_bytes,
                                         uint8_t** out_pointer, size_t* out_num_bytes_preloaded) {
  VBootContext* context = (VBootContext*)ops->user_data;

  *out_pointer = NULL;
  *out_num_bytes_preloaded = 0;

  if (!strncmp(partition, "zircon", strlen("zircon"))) {
    *out_pointer = (uint8_t*)context->preloaded_image;
    size_t preloaded_size = context->preloaded_image->length + sizeof(zbi_header_t);
    if (num_bytes <= preloaded_size) {
      *out_num_bytes_preloaded = num_bytes;
    } else {
      *out_num_bytes_preloaded = preloaded_size;
    }
  }
  return AVB_IO_RESULT_OK;
}

// If a negative offset is given, computes the unsigned offset.
static inline int64_t calc_offset(uint64_t size, int64_t offset) {
  if (offset < 0) {
    return size + offset;
  }
  return offset;
}

static AvbIOResult ReadFromPartition(AvbOps* ops, const char* partition, int64_t offset,
                                     size_t num_bytes, void* buffer, size_t* out_num_read) {
  VBootContext* context = (VBootContext*)ops->user_data;
  ZirconBootOps* zb_ops = (ZirconBootOps*)context->ops;
  size_t part_size;
  if (!ZIRCON_BOOT_OPS_CALL(zb_ops, verified_boot_get_partition_size, partition, &part_size)) {
    zircon_boot_dlog("Failed to find partition %s\n", partition);
    return AVB_IO_RESULT_ERROR_NO_SUCH_PARTITION;
  }

  int64_t abs_offset;
  abs_offset = calc_offset(part_size, offset);
  if (((size_t)abs_offset > part_size) || (abs_offset < 0)) {
    return AVB_IO_RESULT_ERROR_RANGE_OUTSIDE_PARTITION;
  }
  if ((abs_offset + num_bytes) > part_size) {
    num_bytes = part_size - abs_offset;
  }

  bool res = ZIRCON_BOOT_OPS_CALL(zb_ops, read_from_partition, partition, abs_offset, num_bytes,
                                  buffer, out_num_read);
  if (!res || *out_num_read != num_bytes) {
    return AVB_IO_RESULT_ERROR_IO;
  }
  return AVB_IO_RESULT_OK;
}

static AvbIOResult WriteToPartition(AvbOps* ops, const char* partition, int64_t offset,
                                    size_t num_bytes, const void* buffer) {
  // Our usage of libavb should never be writing to a partition - this is only
  // used by the (deprecated) libavb_ab extension.
  zircon_boot_dlog("Errors: libavb write_to_partition() unimplemented\n");
  return AVB_IO_RESULT_ERROR_IO;
}

static void SetKeyVersion(AvbAtxOps* atx_ops, size_t rollback_index_location,
                          uint64_t key_version) {
  VBootContext* context = (VBootContext*)atx_ops->ops->user_data;
  size_t index = context->next_key_version_index++;
  if (index < AVB_ATX_NUM_KEY_VERSIONS) {
    context->key_versions[index].location = rollback_index_location;
    context->key_versions[index].value = key_version;
  } else {
    zircon_boot_dlog("ERROR: set_key_version index out of bounds: %lu\n", index);
    avb_abort();
  }
}

// avb_slot_verify uses this call to check that a partition exists.
// Checks for existence but ignores GUID because it's unused.
static AvbIOResult GetUniqueGuidForPartition(AvbOps* ops, const char* partition, char* guid_buf,
                                             size_t guid_buf_size) {
  VBootContext* context = (VBootContext*)ops->user_data;
  ZirconBootOps* zb_ops = (ZirconBootOps*)context->ops;
  size_t part_size;
  bool res = ZIRCON_BOOT_OPS_CALL(zb_ops, verified_boot_get_partition_size, partition, &part_size);
  return res ? AVB_IO_RESULT_OK : AVB_IO_RESULT_ERROR_NO_SUCH_PARTITION;
}

static AvbIOResult GetSizeOfPartition(AvbOps* ops, const char* partition,
                                      uint64_t* out_size_num_bytes) {
  VBootContext* context = (VBootContext*)ops->user_data;
  ZirconBootOps* zb_ops = (ZirconBootOps*)context->ops;
  size_t out;
  if (!ZIRCON_BOOT_OPS_CALL(zb_ops, verified_boot_get_partition_size, partition, &out)) {
    zircon_boot_dlog("Fail to find partition %s\n", partition);
    return AVB_IO_RESULT_ERROR_NO_SUCH_PARTITION;
  }
  *out_size_num_bytes = out;
  return AVB_IO_RESULT_OK;
}

static AvbIOResult ReadIsDeviceUnlocked(AvbOps* ops, bool* out_is_unlocked) {
  VBootContext* context = (VBootContext*)ops->user_data;
  ZirconBootOps* zb_ops = (ZirconBootOps*)context->ops;
  bool status;
  if (!ZIRCON_BOOT_OPS_CALL(zb_ops, verified_boot_read_is_device_locked, &status)) {
    zircon_boot_dlog("Failed to read is device unlock\n");
    return AVB_IO_RESULT_ERROR_IO;
  }
  *out_is_unlocked = !status;
  return AVB_IO_RESULT_OK;
}

static AvbIOResult AvbReadRollbackIndex(AvbOps* ops, size_t rollback_index_location,
                                        uint64_t* out_rollback_index) {
  VBootContext* context = (VBootContext*)ops->user_data;
  ZirconBootOps* zb_ops = (ZirconBootOps*)context->ops;
  if (!ZIRCON_BOOT_OPS_CALL(zb_ops, verified_boot_read_rollback_index, rollback_index_location,
                            out_rollback_index)) {
    zircon_boot_dlog("Failed to read rollback index %zu\n", rollback_index_location);
    return AVB_IO_RESULT_ERROR_IO;
  }
  return AVB_IO_RESULT_OK;
}

static AvbIOResult AvbWriteRollbackIndex(AvbOps* ops, size_t rollback_index_location,
                                         uint64_t rollback_index) {
  VBootContext* context = (VBootContext*)ops->user_data;
  ZirconBootOps* zb_ops = (ZirconBootOps*)context->ops;
  if (!ZIRCON_BOOT_OPS_CALL(zb_ops, verified_boot_write_rollback_index, rollback_index_location,
                            rollback_index)) {
    zircon_boot_dlog("Failed to write rollback index %zu\n", rollback_index_location);
    return AVB_IO_RESULT_ERROR_IO;
  }
  return AVB_IO_RESULT_OK;
}

static AvbIOResult ReadPermanentAttributes(AvbAtxOps* atx_ops,
                                           AvbAtxPermanentAttributes* attributes) {
  VBootContext* context = (VBootContext*)atx_ops->ops->user_data;
  ZirconBootOps* zb_ops = (ZirconBootOps*)context->ops;
  if (!ZIRCON_BOOT_OPS_CALL(zb_ops, verified_boot_read_permanent_attributes, attributes)) {
    zircon_boot_dlog("Failed to read permanent attributes\n");
    return AVB_IO_RESULT_ERROR_IO;
  }
  return AVB_IO_RESULT_OK;
}

static AvbIOResult ReadPermanentAttributesHash(AvbAtxOps* atx_ops,
                                               uint8_t hash[AVB_SHA256_DIGEST_SIZE]) {
  VBootContext* context = (VBootContext*)atx_ops->ops->user_data;
  ZirconBootOps* zb_ops = (ZirconBootOps*)context->ops;
  if (!ZIRCON_BOOT_OPS_CALL(zb_ops, verified_boot_read_permanent_attributes_hash, hash)) {
    zircon_boot_dlog("Failed to read permanent attribute hash\n");
    return AVB_IO_RESULT_ERROR_IO;
  }
  return AVB_IO_RESULT_OK;
}

static void CreateAvbAndAvbAtxOps(ZirconBootOps* zb_ops, VBootContext* ctx, AvbOps* avb_ops,
                                  AvbAtxOps* atx_ops) {
  ctx->ops = zb_ops;
  avb_ops->user_data = ctx;
  avb_ops->atx_ops = atx_ops;
  avb_ops->read_from_partition = ReadFromPartition;
  avb_ops->get_preloaded_partition = GetPreloadedPartition;
  avb_ops->write_to_partition = WriteToPartition;
  avb_ops->validate_vbmeta_public_key = avb_atx_validate_vbmeta_public_key;
  avb_ops->read_rollback_index = AvbReadRollbackIndex;
  avb_ops->write_rollback_index = AvbWriteRollbackIndex;
  avb_ops->read_is_device_unlocked = ReadIsDeviceUnlocked;
  avb_ops->get_unique_guid_for_partition = GetUniqueGuidForPartition;
  avb_ops->get_size_of_partition = GetSizeOfPartition;
  // As of now, persistent value are not needed yet for our use.
  avb_ops->read_persistent_value = NULL;
  avb_ops->write_persistent_value = NULL;

  atx_ops->ops = avb_ops;
  atx_ops->read_permanent_attributes = ReadPermanentAttributes;
  atx_ops->read_permanent_attributes_hash = ReadPermanentAttributesHash;
  atx_ops->set_key_version = SetKeyVersion;
  atx_ops->get_random = NULL;
}

struct property_lookup_user_data {
  zbi_header_t* zbi;
  size_t capacity;
};

static bool PropertyLookupDescForeach(const AvbDescriptor* header, void* user_data);

static bool ZirconVBootSlotVerifyInternal(ZirconBootOps* zb_ops, zbi_header_t* image,
                                          size_t capacity, const char* ab_suffix,
                                          bool has_successfully_booted,
                                          AvbSlotVerifyData** ptr_verify_data) {
  AvbOps avb_ops;
  AvbAtxOps atx_ops;
  VBootContext context = {
      .next_key_version_index = 0,
      .preloaded_image = image,
  };
  CreateAvbAndAvbAtxOps(zb_ops, &context, &avb_ops, &atx_ops);

  bool unlocked;
  if (avb_ops.read_is_device_unlocked(&avb_ops, &unlocked)) {
    zircon_boot_dlog("Failed to read lock state.\n");
    return false;
  }

  const char* const requested_partitions[] = {"zircon", NULL};

  AvbSlotVerifyFlags flag =
      unlocked ? AVB_SLOT_VERIFY_FLAGS_ALLOW_VERIFICATION_ERROR : AVB_SLOT_VERIFY_FLAGS_NONE;
  AvbSlotVerifyResult result = avb_slot_verify(&avb_ops, requested_partitions, ab_suffix, flag,
                                               AVB_HASHTREE_ERROR_MODE_EIO, ptr_verify_data);

  AvbSlotVerifyData* verify_data = *ptr_verify_data;
  // Copy zbi items within vbmeta regardless of lock state.
  if (result == AVB_SLOT_VERIFY_RESULT_OK) {
    struct property_lookup_user_data lookup_data = {.zbi = image, .capacity = capacity};

    for (size_t i = 0; i < verify_data->num_vbmeta_images; ++i) {
      AvbVBMetaData* vb = &verify_data->vbmeta_images[i];
      // load properties into KV store.
      if (!avb_descriptor_foreach(vb->vbmeta_data, vb->vbmeta_size, PropertyLookupDescForeach,
                                  &lookup_data)) {
        zircon_boot_dlog("Fail to parse VBMETA properties\n");
        return false;
      }
    }
  }

  if (unlocked) {
    zircon_boot_dlog("Device unlocked: not checking verification result.\n");
    return true;
  }

  if (result != AVB_SLOT_VERIFY_RESULT_OK) {
    zircon_boot_dlog("Failed to verify slot: %s, err_code: %s\n", ab_suffix,
                     avb_slot_verify_result_to_string(result));
    return false;
  }

  // Increase rollback index values to match the verified slot only if
  // it has already successfully booted.
  if (has_successfully_booted) {
    for (size_t i = 0; i < AVB_MAX_NUMBER_OF_ROLLBACK_INDEX_LOCATIONS; i++) {
      uint64_t rollback_index_value = verify_data->rollback_indexes[i];

      if (rollback_index_value == ROLLBACK_INDEX_NOT_USED) {
        continue;
      }

      AvbIOResult result = avb_ops.write_rollback_index(&avb_ops, i, rollback_index_value);
      if (result != AVB_IO_RESULT_OK) {
        zircon_boot_dlog("Failed to write rollback index: %zu\n", i);
        return false;
      }
    }

    // Also increase rollback index values for Fuchsia key version locations.
    for (size_t i = 0; i < AVB_ATX_NUM_KEY_VERSIONS; i++) {
      AvbIOResult result = avb_ops.write_rollback_index(&avb_ops, context.key_versions[i].location,
                                                        context.key_versions[i].value);
      if (result != AVB_IO_RESULT_OK) {
        zircon_boot_dlog("Failed to write rollback index: %zu\n", context.key_versions[i].location);
        return false;
      }
    }
  }
  zircon_boot_dlog("slot: %s successfully verified.\n", ab_suffix);
  return true;
}

bool ZirconVBootSlotVerify(ZirconBootOps* zb_ops, zbi_header_t* image, size_t capacity,
                           const char* ab_suffix, bool has_successfully_booted) {
  AvbSlotVerifyData* verify_data = NULL;
  bool res = ZirconVBootSlotVerifyInternal(zb_ops, image, capacity, ab_suffix,
                                           has_successfully_booted, &verify_data);
  if (verify_data) {
    avb_slot_verify_data_free(verify_data);
  }
  return res;
}

// If the given property holds a ZBI container, appends its contents to the ZBI
// container in |lookup_data|.
static void ProcessProperty(const AvbPropertyDescriptor prop_desc, uint8_t* start,
                            const struct property_lookup_user_data* lookup_data) {
  const char* key = (const char*)start;
  if (key[prop_desc.key_num_bytes] != 0) {
    zircon_boot_dlog(
        "No terminating NUL byte in the property key."
        "Skipping this property descriptor.\n");
    return;
  }
  // Only look at properties whose keys start with the 'zbi' prefix.
  if (strncmp(key, "zbi", strlen("zbi"))) {
    return;
  }
  zircon_boot_dlog("Found vbmeta ZBI property '%s' (%lu bytes)\n", key, prop_desc.value_num_bytes);

  // We don't care about the key. Move value data to the start address to make sure
  // that the zbi item starts from an aligned address.
  uint64_t value_offset, value_size;
  if (!avb_safe_add(&value_offset, prop_desc.key_num_bytes, 1) ||
      !avb_safe_add(&value_size, prop_desc.value_num_bytes, 1)) {
    zircon_boot_dlog(
        "Overflow while computing offset and size for value."
        "Skipping this property descriptor.\n");
    return;
  }
  memmove(start, start + value_offset, value_size);

  const zbi_header_t* vbmeta_zbi = (const zbi_header_t*)start;

  const uint64_t zbi_size = sizeof(zbi_header_t) + vbmeta_zbi->length;
  if (zbi_size > prop_desc.value_num_bytes) {
    zircon_boot_dlog("vbmeta ZBI length exceeds property size (%lu > %lu)\n", zbi_size,
                     prop_desc.value_num_bytes);
    return;
  }

  zbi_result_t result = zbi_check(vbmeta_zbi, NULL);
  if (result != ZBI_RESULT_OK) {
    zircon_boot_dlog("Mal-formed vbmeta ZBI: error %d\n", result);
    return;
  }

  result = zbi_extend(lookup_data->zbi, lookup_data->capacity, vbmeta_zbi);
  if (result != ZBI_RESULT_OK) {
    zircon_boot_dlog("Failed to add vbmeta ZBI: error %d\n", result);
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
