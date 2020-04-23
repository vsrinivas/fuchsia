// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/sysconfig/sysconfig-header.h>

static bool subpartition_page_aligned(const struct sysconfig_subpartition *part,
                                      uint64_t page_size) {
  // It shall be page-aligned.
  if (part->size % page_size || part->offset % page_size) {
    return false;
  }

  return true;
}

static bool subpartition_not_on_page0(const struct sysconfig_subpartition *part,
                                      uint64_t page_size) {
  return part->offset >= page_size;
}

static bool subpartition_out_of_range(const struct sysconfig_subpartition *part, uint64_t page_size,
                                      uint64_t partition_size) {
  // It shall fit into sysconfig partition size.
  if (part->size > partition_size || part->offset > partition_size - part->size) {
    return false;
  }

  return true;
}

static bool subpartition_disjoint(const struct sysconfig_subpartition *part_a,
                                  const struct sysconfig_subpartition *part_b) {
  return part_a->offset + part_a->size <= part_b->offset ||
         part_b->offset + part_b->size <= part_a->offset;
}

bool sysconfig_header_valid(const struct sysconfig_header *header, uint64_t page_size,
                            uint64_t partition_size) {
  // Conditions:
  // 1. valid magic.
  // 2. valid crc.
  // 3. sub-partition fits into partition.
  // 4. sub-partitions do not overlap.
  // 5. the first page is reserved for header.

  if (memcmp(header->magic, SYSCONFIG_HEADER_MAGIC_STR, sizeof(header->magic))) {
    syshdrP("Header has invalid magic.\n");
    return false;
  }

  if (header->crc_value != sysconfig_header_crc32(0, (const uint8_t *)header,
                                                  offsetof(struct sysconfig_header, crc_value))) {
    syshdrP("Header has invalid crc.\n");
    return false;
  }

  struct sysconfig_subpartition all_parts[] = {header->abr_metadata, header->sysconfig_data,
                                               header->vb_metadata_a, header->vb_metadata_b,
                                               header->vb_metadata_r};

  int i, j;
  int num_subparts = sizeof(all_parts) / sizeof(struct sysconfig_subpartition);
  for (i = 0; i < num_subparts; i++) {
    if (!subpartition_page_aligned(&all_parts[i], page_size)) {
      syshdrP("sub-partition %d is not page-aligned\n", i);
      return false;
    }

    if (!subpartition_out_of_range(&all_parts[i], page_size, partition_size)) {
      syshdrP("sub-partition %d is out-of-range\n", i);
      return false;
    }

    if (!subpartition_not_on_page0(&all_parts[i], page_size)) {
      syshdrP("sub-partition %d occupies page0 reserved for header\n", i);
      return false;
    }

    for (j = i + 1; j < num_subparts; j++) {
      if (!subpartition_disjoint(&all_parts[i], &all_parts[j])) {
        syshdrP("sub-partition %d and %d overlap\n", i, j);
        return false;
      }
    }
  }

  return true;
}

bool sysconfig_header_equal(const struct sysconfig_header *lhs,
                            const struct sysconfig_header *rhs) {
  return memcmp(lhs, rhs, offsetof(struct sysconfig_header, crc_value)) == 0;
}

void update_sysconfig_header_magic_and_crc(struct sysconfig_header *header) {
  memcpy(header->magic, SYSCONFIG_HEADER_MAGIC_STR, sizeof(header->magic));
  memset(header->reserved, 0, 4);
  header->crc_value = sysconfig_header_crc32(0, (const uint8_t *)header,
                                             offsetof(struct sysconfig_header, crc_value));
}
