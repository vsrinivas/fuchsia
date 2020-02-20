// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftl.h"

#include <limits.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <algorithm>
#include <array>
#include <new>

#include <fbl/vector.h>

#include "ftl_internal.h"

namespace {

using internal::SpareArea;

int GetWearCount(const NandBroker& nand, uint32_t block, int page_multiplier) {
  if (nand.ftl() && nand.ftl()->IsBadBlock(block)) {
    return -1;
  }

  if (!nand.ReadPages(block * nand.Info().pages_per_block, page_multiplier)) {
    printf("Read failed for block %u\n", block);
    return -1;
  }

  SpareArea* oob = reinterpret_cast<SpareArea*>(nand.oob());
  return internal::IsFtlBlock(*oob) ? internal::DecodeWear(*oob) : -1;
}

class FtlData final : public FtlInfo {
 public:
  explicit FtlData(const NandBroker* nand) : nand_(nand) {}
  ~FtlData() final {}

  bool Initialize();
  const internal::NdmData& ndm() const { return ndm_; }

  // FtlInfo interface:
  void DumpInfo() const final { return ndm_.DumpInfo(); }
  bool IsBadBlock(uint32_t block) const final { return ndm_.IsBadBlock(block); }
  uint32_t LastFtlBlock() const final { return ndm_.LastFtlBlock(); }
  bool IsMapPage(uint32_t page) const final;

 private:
  const NandBroker* nand_;
  internal::NdmData ndm_;
};

bool FtlData::Initialize() { return ndm_.FindHeader(*nand_); }

bool FtlData::IsMapPage(uint32_t page) const {
  page /= ndm_.page_multiplier();
  const SpareArea* oob = reinterpret_cast<const SpareArea*>(nand_->oob());
  ZX_DEBUG_ASSERT(nand_->Info().oob_size <= sizeof(*oob));
  return IsMapBlock(oob[page]);
}

}  // namespace

// static
std::unique_ptr<FtlInfo> FtlInfo::Factory(const NandBroker* nand) {
  auto ftl = std::make_unique<FtlData>(nand);
  if (!ftl->Initialize()) {
    return nullptr;
  }
  return ftl;
}

bool WearCounts(const NandBroker& nand) {
  uint32_t num_blocks = nand.Info().num_blocks;
  int page_multiplier = 2;
  if (nand.ftl()) {
    const FtlData* ftl = reinterpret_cast<const FtlData*>(nand.ftl());
    num_blocks = ftl->LastFtlBlock();
    page_multiplier = ftl->ndm().page_multiplier();
  }

  int min = INT_MAX;
  int max = 0;
  int sum = 0;
  int count = 0;

  // Build a wear count histogram. The expected size is 255 so the first wear
  // count can be either 255 below the max or 255 above the min; in other words,
  // 512 buckets should be enough. 1000 provides a reasonable extra range.
  std::array<int, 1000> histogram = {};
  int offset = -1;
  for (uint32_t block = 0; block < num_blocks; block++) {
    int value = GetWearCount(nand, block, page_multiplier);
    if (value > 0) {
      min = std::min(min, value);
      max = std::max(max, value);
      sum += value;
      count++;

      if (offset < 0) {
        // Place the first found count at the center of the range.
        offset = std::max(0, value - 500);
      }
      int bucket = value - offset;
      if (bucket < 0 || bucket > 999) {
        printf("Out of range for histogram: %d (block %u) vs start: %d\n", value, block, offset);
        continue;
      }
      histogram[bucket] += 1;
    }
  }

  if (count) {
    sum /= count;
    printf("Wear counts: min %d, max %d, delta %d, average %d, count %d\n", min, max, max - min,
           sum, count);
    for (int i = min; i <= max; i++) {
      int bucket = i - offset;
      if (bucket >= 0 && bucket < 1000) {
        printf("[%06d] %d\n", i, histogram[bucket]);
      }
    }
  } else {
    printf("No wear count found\n");
  }
  return true;
}
