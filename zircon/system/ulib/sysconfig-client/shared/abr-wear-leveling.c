// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sysconfig/abr-wear-leveling.h>

#if defined(ABR_WEAR_LEVELING_DEBUG)
#define abrwlP(fmt...) printf("[abr-wear-leveling]%s:%d:", __func__, __LINE__), printf(fmt)
#else
#define abrwlP(fmt...)
#endif

void set_abr_metadata_ext_magic(struct abr_metadata_ext *data) {
  data->magic[0] = ABR_WEAR_LEVELING_MAGIC_BYTE_0;
  data->magic[1] = ABR_WEAR_LEVELING_MAGIC_BYTE_1;
  data->magic[2] = ABR_WEAR_LEVELING_MAGIC_BYTE_2;
  data->magic[3] = ABR_WEAR_LEVELING_MAGIC_BYTE_3;
}

bool abr_metadata_ext_valid(const void *abr_data) {
  const uint8_t *start = (const uint8_t *)abr_data;
  return start[ABR_WEAR_LEVELING_MAGIC_OFFSET] == ABR_WEAR_LEVELING_MAGIC_BYTE_0 &&
         start[ABR_WEAR_LEVELING_MAGIC_OFFSET + 1] == ABR_WEAR_LEVELING_MAGIC_BYTE_1 &&
         start[ABR_WEAR_LEVELING_MAGIC_OFFSET + 2] == ABR_WEAR_LEVELING_MAGIC_BYTE_2 &&
         start[ABR_WEAR_LEVELING_MAGIC_OFFSET + 3] == ABR_WEAR_LEVELING_MAGIC_BYTE_3;
}

bool layout_support_wear_leveling(const struct sysconfig_header *header, size_t page_size) {
  // Abr metadata sub-partition needs to be at the end.
  return header->abr_metadata.size > page_size &&
         header->abr_metadata.offset >= header->sysconfig_data.offset &&
         header->abr_metadata.offset >= header->vb_metadata_a.offset &&
         header->abr_metadata.offset >= header->vb_metadata_b.offset &&
         header->abr_metadata.offset >= header->vb_metadata_r.offset;
}

void find_latest_abr_metadata_page(const struct sysconfig_header *header, const void *abr_subpart,
                                   uint64_t page_size, struct abr_metadata_ext *out) {
  // Abr metadatas are appended from the first to the last page in the
  // sub-partition. Thus, we scan backward and find the first valid
  // page.
  int i, num_pages = header->abr_metadata.size / page_size;
  const uint8_t *start = (const uint8_t *)abr_subpart;
  abrwlP("Finding page with latest abr metadata\n");
  for (i = num_pages - 1; i >= 0; i--) {
    if (abr_metadata_ext_valid(start + i * page_size)) {
      abrwlP("page %d has valid abr metadata\n", i);
      memcpy(out, start + i * page_size, sizeof(struct abr_metadata_ext));
      return;
    }
  }
  // Default to the first page if there is no page with valid magic,
  abrwlP("no page with valid magic found. use page 0 as default\n");
  memcpy(out, start, sizeof(struct abr_metadata_ext));
}

bool find_empty_page_for_wear_leveling(const struct sysconfig_header *header,
                                       const uint8_t *abr_subpart, uint64_t page_size,
                                       int64_t *out) {
  // NAND page programming has to be consecutive from the first to last
  // within a block. Thus we find the first empty page such that all pages
  // behind it are also empty, or in other work, the immediate empty page
  // after the last non-empty page in the sub-partition;
  int64_t i, j, num_pages = header->abr_metadata.size / page_size;

  const uint8_t *page;
  abrwlP("Finding empty page, total pages: %ld\n", num_pages);
  for (i = 1; i <= num_pages; i++) {
    page = abr_subpart + (num_pages - i) * page_size;
    bool page_empty = true;
    // Check whether the page is empty.
    for (j = 0; j < (int64_t)page_size; j++) {
      if (page[j] != 0xff) {
        abrwlP("page %ld, offset %ld = 0x%x != 0xff\n", i, j, page[j]);
        page_empty = false;
        break;
      }
    }

    if (!page_empty) {
      abrwlP("page %ld non-empty\n", i);
      break;
    }

    abrwlP("page %ld empty\n", i);
  }
  *out = num_pages - i + 1;
  abrwlP("using empty page %ld\n", *out);
  // If no page is empty, *out will be equal to num_pages.
  return *out < num_pages;
}
