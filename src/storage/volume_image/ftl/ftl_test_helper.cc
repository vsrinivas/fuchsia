// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/ftl/ftl_test_helper.h"

#include <lib/stdcompat/span.h>

#include <iostream>

#include <fbl/algorithm.h>

namespace storage::volume_image {

// |ftl_volume| (if provided) will be notified with the volume details.
// Returns an error string, or nullptr on success.
const char* InMemoryNdm::Attach(const ftl::Volume* ftl_volume) {
  ftl::VolumeOptions options;
  options.block_size =
      static_cast<uint32_t>(raw_nand_->options.page_size * raw_nand_->options.pages_per_block);
  options.eb_size = raw_nand_->options.oob_bytes_size;
  options.max_bad_blocks = max_bad_blocks_;
  if (raw_nand_->options.page_count % raw_nand_->options.pages_per_block != 0) {
    return "InMemoryNdm::Attach page_count not divisble by pages_per_block.";
  }
  options.num_blocks = raw_nand_->options.page_count / raw_nand_->options.pages_per_block;
  options.page_size = static_cast<uint32_t>(raw_nand_->options.page_size);
  options.flags = 0;
  return CreateNdmVolume(ftl_volume, options);
}

// Reads |page_count| pages starting at |start_page|, placing the results on
// |page_buffer| and |oob_buffer|. Either pointer can be nullptr if that
// part is not desired.
// Returns kNdmOk, kNdmUncorrectableEcc, kNdmFatalError or kNdmUnsafeEcc.
int InMemoryNdm::NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer,
                          void* oob_buffer) {
  for (uint32_t i = 0; i < page_count; ++i) {
    uint32_t page_number = start_page + i;
    size_t page_offset = i * page_size_;
    size_t oob_offset = i * oob_size_;

    if (raw_nand_->page_data.find(page_number) == raw_nand_->page_data.end()) {
      if (page_buffer != nullptr) {
        auto page_view =
            cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(page_buffer) + page_offset, page_size_);
        std::fill(page_view.begin(), page_view.end(), 0xFF);
      }
      if (oob_buffer != nullptr) {
        auto oob_view =
            cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(oob_buffer) + oob_offset, oob_size_);
        std::fill(oob_view.begin(), oob_view.end(), 0xFF);
      }
    } else {
      if (page_buffer != nullptr) {
        auto page_view =
            cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(page_buffer) + page_offset, page_size_);
        memcpy(page_view.data(), raw_nand_->page_data.at(page_number).data(), page_view.size());
      }

      if (oob_buffer != nullptr) {
        auto oob_view =
            cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(oob_buffer) + oob_offset, oob_size_);
        memcpy(oob_view.data(), raw_nand_->page_oob.at(page_number).data(), oob_view.size());
      }
    }
  }
  return ftl::kNdmOk;
}

// Writes |page_count| pages starting at |start_page|, using the data from
// |page_buffer| and |oob_buffer|.
// Returns kNdmOk, kNdmError or kNdmFatalError. kNdmError triggers marking
// the block as bad.
int InMemoryNdm::NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                           const void* oob_buffer) {
  for (uint32_t i = 0; i < page_count; ++i) {
    uint32_t page_number = start_page + i;
    size_t page_offset = i * page_size_;
    size_t oob_offset = i * oob_size_;
    auto page_view = cpp20::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(page_buffer) + page_offset, raw_nand_->options.page_size);
    auto oob_view =
        cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(oob_buffer) + oob_offset,
                                   raw_nand_->options.oob_bytes_size);
    if (page_buffer != nullptr) {
      raw_nand_->page_data[page_number] = std::vector<uint8_t>(page_view.begin(), page_view.end());
    }

    if (oob_buffer != nullptr) {
      raw_nand_->page_oob[page_number] = std::vector<uint8_t>(oob_view.begin(), oob_view.end());
    }
  }

  return ftl::kNdmOk;
}

// Erases the block containing |page_num|.
// Returns kNdmOk or kNdmError. kNdmError triggers marking the block as bad.
int InMemoryNdm::NandErase(uint32_t page_num) {
  uint32_t page_start = fbl::round_down(page_num, raw_nand_->options.pages_per_block);
  for (uint32_t i = 0; i < raw_nand_->options.pages_per_block; ++i) {
    raw_nand_->page_data.erase(page_start + i);
    raw_nand_->page_oob.erase(page_start + i);
  }
  return ftl::kNdmOk;
}

// Returns whether a given page is empty or not. |data| and |spare| store
// the contents of the page.
bool InMemoryNdm::IsEmptyPage(uint32_t page_num, const uint8_t* data, const uint8_t* spare) {
  auto page_view = cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data), page_size_);
  auto oob_view = cpp20::span<const uint8_t>(reinterpret_cast<const uint8_t*>(spare), oob_size_);

  return std::all_of(oob_view.begin(), oob_view.end(), [](const auto& b) { return b == 0xFF; }) &&
         std::all_of(page_view.begin(), page_view.end(), [](const auto& b) { return b == 0xFF; });
}

}  // namespace storage::volume_image
