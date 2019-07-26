// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml.h"

#include <stdio.h>
#include <string.h>

namespace {

// Simplified Amlogic data structures.
struct ExtInfo {
  uint32_t read_info;
  uint32_t new_type;
  uint32_t pages_per_block;
  uint32_t xlc;  // slc=1, mlc=2, tlc=3.
  uint32_t ce_mask;
  uint32_t boot_num;
  uint32_t each_boot_pages;
  uint32_t bbt_occupy_pages;
  uint32_t bbt_start_block;
};

struct Page0 {
  uint32_t config;
  uint16_t id;
  uint16_t max;
  uint8_t page_list[16];
  uint16_t retry_usr[32];
  ExtInfo ext_info;
};

// The number of pages on a single table.
uint32_t g_bbt_size = 0;

// Returns the number of valid bad block tables found.
int GetNumTables(const char* oob, const fuchsia_hardware_nand_Info& info) {
  int found = 0;
  for (uint32_t page = 0; page < info.pages_per_block; page++) {
    if (memcmp(oob + page * info.oob_size, "nbbt", 4) != 0) {
      break;
    }
    found++;
  }
  return found / g_bbt_size;
}

}  // namespace

void DumpPage0(const void* data) {
  const Page0* page0 = reinterpret_cast<const Page0*>(data);

  printf("Config: 0x%x\n", page0->config);
  printf("ECC step: %u\n", page0->config & 0x3f);
  printf("Page size (encoded): %u\n", (page0->config >> 6) & 0x7f);
  printf("Pages per block: %u\n", page0->ext_info.pages_per_block);
  printf("Boot type: %u\n", page0->ext_info.boot_num);
  printf("Boot pages: %u\n", page0->ext_info.each_boot_pages);
  printf("BBT size (pages): %u\n", page0->ext_info.bbt_occupy_pages);
  printf("BBT block start: %u\n", page0->ext_info.bbt_start_block);
}

void GetBbtLocation(const void* data, uint32_t* first_block, uint32_t* num_blocks) {
  const Page0* page0 = reinterpret_cast<const Page0*>(data);
  g_bbt_size = page0->ext_info.bbt_occupy_pages;
  *first_block = page0->ext_info.bbt_start_block;
  *num_blocks = 4;
}

int DumpBbt(const void* data, const void* oob, const fuchsia_hardware_nand_Info& info) {
  if (g_bbt_size * info.page_size < info.num_blocks) {
    printf("BBT too small\n");
    return 0;
  }

  const char* table = reinterpret_cast<const char*>(data);
  const char* oob_data = reinterpret_cast<const char*>(oob);
  int num_tables = GetNumTables(oob_data, info);

  for (int cur_table = 0; cur_table < num_tables; cur_table++) {
    printf("BBT Table %d\n", cur_table);
    for (uint32_t block = 0; block < info.num_blocks; block++) {
      if (table[block]) {
        printf("Block %d marked bad\n", block);
      }
    }
    oob_data += info.oob_size * g_bbt_size;
    table += info.page_size * g_bbt_size;
  }
  return num_tables;
}
