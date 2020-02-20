// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "commands.h"

#include <fcntl.h>
#include <lib/cksum.h>
#include <stdio.h>

#include <new>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>

#include "aml.h"

bool FindBadBlocks(const NandBroker& nand) {
  if (!nand.ReadPages(0, 1)) {
    return false;
  }

  uint32_t first_block;
  uint32_t num_blocks;
  GetBbtLocation(nand.data(), &first_block, &num_blocks);
  bool found = false;
  for (uint32_t block = 0; block < num_blocks; block++) {
    uint32_t start = (first_block + block) * nand.Info().pages_per_block;
    if (!nand.ReadPages(start, nand.Info().pages_per_block)) {
      return false;
    }
    if (!DumpBbt(nand.data(), nand.oob(), nand.Info())) {
      break;
    }
    found = true;
  }
  if (!found) {
    printf("Unable to find any table\n");
  }
  return found;
}

bool ReadCheck(const NandBroker& nand, uint32_t first_block, uint32_t count) {
  constexpr int kNumReads = 10;
  uint32_t last_block = fbl::min(nand.Info().num_blocks, first_block + count);
  size_t size = (nand.Info().page_size + nand.Info().oob_size) * nand.Info().pages_per_block;
  for (uint32_t block = first_block; block < last_block; block++) {
    uint32_t first_crc;
    for (int i = 0; i < kNumReads; i++) {
      const uint32_t start = block * nand.Info().pages_per_block;
      if (!nand.ReadPages(start, nand.Info().pages_per_block)) {
        printf("\nRead failed for block %u\n", block);
        return false;
      }
      const uint32_t crc = crc32(0, reinterpret_cast<const uint8_t*>(nand.data()), size);
      if (!i) {
        first_crc = crc;
      } else if (first_crc != crc) {
        printf("\nMismatched reads on block %u\n", block);
        return false;
      }
    }
    printf("Block %u\r", block);
  }
  printf("\ndone\n");
  return true;
}

void ReadBlockByPage(const NandBroker& nand, uint32_t block_num) {
  const auto& info = nand.Info();
  std::unique_ptr<char[]> first_page(new char[info.page_size + info.oob_size]);

  uint32_t last_page_num = (block_num + 1) * info.pages_per_block;
  char* data = nand.data();
  char* oob = nand.oob();
  for (uint32_t page = block_num * info.pages_per_block; page < last_page_num; page++) {
    if (!nand.ReadPages(page, 1)) {
      printf("\tRead failed for page %u\n", page);
    }
    if (page == block_num * info.pages_per_block) {
      // ReadPages always places data at the beginning of the buffer, so
      // the second read will overwrite this. Save the data for later.
      memcpy(first_page.get(), nand.data(), info.page_size);
      memcpy(first_page.get() + info.page_size, nand.oob(), info.oob_size);
    } else {
      memcpy(data, nand.data(), info.page_size);
      memcpy(oob, nand.oob(), info.oob_size);
    }
    data += info.page_size;
    oob += info.oob_size;
  }
  memcpy(nand.data(), first_page.get(), info.page_size);
  memcpy(nand.oob(), first_page.get() + info.page_size, info.oob_size);
}

bool Save(const NandBroker& nand, uint32_t first_block, uint32_t count, const char* path) {
  fbl::unique_fd out(open(path, O_WRONLY | O_CREAT | O_TRUNC));
  if (!out) {
    printf("Unable to open destination\n");
    return false;
  }

  // Attempt to save everything by default.
  count = count ? count : nand.Info().num_blocks;

  uint32_t block_oob_size = nand.Info().pages_per_block * nand.Info().oob_size;
  uint32_t oob_size = count * block_oob_size;
  std::unique_ptr<uint8_t[]> oob(new uint8_t[oob_size]);

  uint32_t last_block = fbl::min(nand.Info().num_blocks, first_block + count);
  size_t data_size = nand.Info().page_size * nand.Info().pages_per_block;
  for (uint32_t block = first_block; block < last_block; block++) {
    const uint32_t start = block * nand.Info().pages_per_block;
    if (nand.ftl() && nand.ftl()->IsBadBlock(block)) {
      memset(nand.data(), 0xff, data_size + nand.Info().oob_size * nand.Info().pages_per_block);
    } else if (!nand.ReadPages(start, nand.Info().pages_per_block)) {
      printf("\nRead failed for block %u\n", block);
      memset(nand.data(), 0xff, data_size + nand.Info().oob_size * nand.Info().pages_per_block);
      ReadBlockByPage(nand, block);
    }

    if (write(out.get(), nand.data(), data_size) != static_cast<ssize_t>(data_size)) {
      printf("\nFailed to write data for block %u\n", block);
      return false;
    }
    memcpy(oob.get() + block_oob_size * block, nand.oob(), block_oob_size);
    printf("Block %u\r", block);
  }

  if (write(out.get(), oob.get(), oob_size) != static_cast<ssize_t>(oob_size)) {
    printf("\nFailed to write oob\n");
    return false;
  }

  printf("\ndone\n");
  return true;
}

bool Erase(const NandBroker& nand, uint32_t first_block, uint32_t count) {
  uint32_t last_block = fbl::min(nand.Info().num_blocks, first_block + count);
  for (uint32_t block = first_block; block < last_block; block++) {
    if (!nand.ftl() || !nand.ftl()->IsBadBlock(block)) {
      // Ignore failures, move on to the next one.
      nand.EraseBlock(block);
    }
  }
  printf("\ndone\n");
  return true;
}
