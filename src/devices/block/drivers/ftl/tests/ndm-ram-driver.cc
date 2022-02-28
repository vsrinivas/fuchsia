// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ndm-ram-driver.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr uint8_t kWrittenFlag = 1 << 0;
constexpr uint8_t kFailEccFlag = 1 << 1;
constexpr uint8_t kBadBlockFlag = 1 << 2;
constexpr uint8_t kUnsafeEccFlag = 1 << 3;

// Technically, can be used to set / reset more than one flag at a time.
void SetFlag(uint8_t flag, uint8_t* where) { *where = *where | flag; }

void ClearFlag(uint8_t flag, uint8_t* where) { *where = *where & static_cast<uint8_t>(~flag); }

bool IsFlagSet(uint8_t flag, const uint8_t* where) { return (*where & flag) == flag; }

}  // namespace

bool NdmRamDriver::DoubleSize() {
  ZX_ASSERT(test_options_.use_half_size);

  // This mimics the code of NandDriverImpl::HandleAlternateConfig with the
  // exceptions of not having to confirm the existence of a small device, and
  // leaving final re-initialization to FtlShell::Reattach (controlled by the
  // test code).

  if (!IsNdmDataPresent(options_)) {
    return false;
  }

  if (!SaveBadBlockData()) {
    return false;
  }
  RemoveNdmVolume();

  options_.num_blocks *= 2;
  test_options_.use_half_size = false;
  if (!IsNdmDataPresent(options_)) {
    return false;
  }
  if (!RestoreBadBlockData()) {
    return false;
  }

  return true;
}

const char* NdmRamDriver::Init() {
  size_t num_pages = PagesPerBlock() * options_.num_blocks;
  size_t volume_size = num_pages * (options_.page_size + options_.eb_size);
  volume_ = fbl::Array<uint8_t>(new uint8_t[volume_size], volume_size);
  flags_ = fbl::Array<uint8_t>(new uint8_t[num_pages], num_pages);
  memset(volume_.data(), 0xff, volume_size);
  memset(flags_.data(), 0, num_pages);
  if (test_options_.use_half_size) {
    options_.num_blocks /= 2;
  }

  return nullptr;
}

const char* NdmRamDriver::Attach(const ftl::Volume* ftl_volume) {
  const char* error = CreateNdmVolume(ftl_volume, options_, test_options_.save_config_data);
  if (!error) {
    // Follow the logic of the real driver.
    if (test_options_.save_config_data && !volume_data_saved()) {
      if (!WriteVolumeData()) {
        error = "Write volume failed";
      }
    }
  }
  return error;
}

bool NdmRamDriver::Detach() { return RemoveNdmVolume(); }

// Returns kNdmOk, kNdmUncorrectableEcc, kNdmFatalError or kNdmUnsafeEcc.
int NdmRamDriver::NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer,
                           void* oob_buffer) {
  bool unsafe = false;
  uint8_t* data = reinterpret_cast<uint8_t*>(page_buffer);
  uint8_t* spare = reinterpret_cast<uint8_t*>(oob_buffer);
  for (; page_count; page_count--, start_page++) {
    int result = ReadPage(start_page, data, spare);
    if (result == ftl::kNdmUnsafeEcc) {
      unsafe = true;
    } else if (result != ftl::kNdmOk) {
      return result;
    }

    if (data) {
      data += options_.page_size;
    }
    if (spare) {
      spare += options_.eb_size;
    }
  }

  return unsafe ? ftl::kNdmUnsafeEcc : ftl::kNdmOk;
}

// Returns kNdmOk, kNdmError or kNdmFatalError. kNdmError triggers marking the block as bad.
int NdmRamDriver::NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                            const void* oob_buffer) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(page_buffer);
  const uint8_t* spare = reinterpret_cast<const uint8_t*>(oob_buffer);
  ZX_ASSERT(data);
  ZX_ASSERT(spare);
  for (; page_count; page_count--, start_page++) {
    int result = WritePage(start_page, data, spare);
    if (result != ftl::kNdmOk) {
      return result;
    }
    data += options_.page_size;
    spare += options_.eb_size;
  }
  return ftl::kNdmOk;
}

// Returns kNdmOk or kNdmError. kNdmError triggers marking the block as bad.
int NdmRamDriver::NandErase(uint32_t page_num) {
  if (power_failure_triggered_) {
    return ftl::kNdmFatalError;
  }
  ZX_ASSERT(page_num < options_.block_size * options_.num_blocks);

  if (ShouldTriggerPowerFailure()) {
    OnErasePowerFailure(page_num);
    return ftl::kNdmFatalError;
  }

  if (BadBlock(page_num)) {
    ZX_ASSERT(false);
  }
  if (SimulateBadBlock(page_num)) {
    return ftl::kNdmError;
  }

  // Reset block data and spare area.
  ZX_ASSERT(page_num % PagesPerBlock() == 0);
  uint32_t end = page_num + PagesPerBlock();
  do {
    memset(MainData(page_num), 0xFF, options_.page_size);
    memset(SpareData(page_num), 0xFF, options_.eb_size);
    SetWritten(page_num, false);
    SetFailEcc(page_num, false);
    SetUnsafeEcc(page_num, false);
  } while (++page_num < end);

  return ftl::kNdmOk;
}

// Returns kTrue, kFalse or kNdmError.
int NdmRamDriver::IsBadBlock(uint32_t page_num) {
  ZX_ASSERT(page_num < options_.block_size * options_.num_blocks);
  ZX_ASSERT(page_num % PagesPerBlock() == 0);

  if (power_failure_triggered_) {
    return ftl::kNdmError;
  }

  // If first byte on first page is not all 0xFF, block is bad.
  // This is a common (although not unique) factory marking used by real NAND
  // chips. This code enables a test to simulate factory-bad blocks.
  if (SpareData(page_num)[0] != 0xFF) {
    SetBadBlock(page_num, true);
    return ftl::kTrue;
  }

  return ftl::kFalse;
}

bool NdmRamDriver::IsEmptyPage(uint32_t page_num, const uint8_t* data, const uint8_t* spare) const {
  ZX_ASSERT(page_num < options_.block_size * options_.num_blocks);
  if (!Written(page_num)) {
    return true;
  }
  return IsEmptyPageImpl(data, options_.page_size, spare, options_.eb_size);
}

int NdmRamDriver::ReadPage(uint32_t page_num, uint8_t* data, uint8_t* spare) {
  ZX_ASSERT(page_num < options_.block_size * options_.num_blocks);
  if (power_failure_triggered_) {
    return ftl::kNdmFatalError;
  }

  // Fail ECC if page never written or was failed before.
  if (data && !Written(page_num)) {
    // Reading FF is definitely OK at least for spare data.
    return ftl::kNdmUncorrectableEcc;
  }

  if (FailEcc(page_num)) {
    return ftl::kNdmUncorrectableEcc;
  }

  if (data) {
    // Read page main data.
    memcpy(data, MainData(page_num), options_.page_size);
  }

  if (spare) {
    // Read page main data.
    memcpy(spare, SpareData(page_num), options_.eb_size);
  }

  // Return an occasional kNdmUnsafeEcc.
  if (ecc_error_interval_++ == test_options_.ecc_error_interval || UnsafeEcc(page_num)) {
    ecc_error_interval_ = 0;
    return ftl::kNdmUnsafeEcc;
  }

  return ftl::kNdmOk;
}

int NdmRamDriver::WritePage(uint32_t page_num, const uint8_t* data, const uint8_t* spare) {
  ZX_ASSERT(page_num < options_.block_size * options_.num_blocks);

  if (power_failure_triggered_) {
    return ftl::kNdmFatalError;
  }

  if (ShouldTriggerPowerFailure()) {
    OnWritePowerFailure(page_num, data, spare);
    return ftl::kNdmFatalError;
  }

  if (BadBlock(page_num)) {
    ZX_ASSERT(false);
  }

  ZX_ASSERT(!Written(page_num));

  if (SimulateBadBlock(page_num)) {
    return ftl::kNdmError;
  }

  // Write data and spare bytes to 'flash'.
  memcpy(MainData(page_num), data, options_.page_size);
  memcpy(SpareData(page_num), spare, options_.eb_size);
  SetWritten(page_num, true);

  return ftl::kNdmOk;
}

uint32_t NdmRamDriver::PageSize() const { return options_.page_size; }

uint8_t NdmRamDriver::SpareSize() const { return options_.eb_size; }

bool NdmRamDriver::SimulateBadBlock(uint32_t page_num) {
  if (num_bad_blocks_ < options_.max_bad_blocks) {
    bad_block_interval_++;
    if (bad_block_interval_ > test_options_.bad_block_interval) {
      SetBadBlock(page_num, true);
      num_bad_blocks_++;
      if (bad_block_interval_ == test_options_.bad_block_interval + test_options_.bad_block_burst) {
        bad_block_interval_ = 0;
      }
      return true;
    }
  }
  return false;
}

uint8_t* NdmRamDriver::MainData(uint32_t page_num) {
  size_t offset = page_num * (options_.page_size + options_.eb_size);
  ZX_ASSERT(offset < volume_.size());
  return &volume_[offset];
}

uint8_t* NdmRamDriver::SpareData(uint32_t page_num) {
  return MainData(page_num) + options_.page_size;
}

bool NdmRamDriver::Written(uint32_t page_num) const {
  return IsFlagSet(kWrittenFlag, &flags_[page_num]);
}

bool NdmRamDriver::FailEcc(uint32_t page_num) const {
  return IsFlagSet(kFailEccFlag, &flags_[page_num]);
}

bool NdmRamDriver::UnsafeEcc(uint32_t page_num) const {
  return IsFlagSet(kUnsafeEccFlag, &flags_[page_num]);
}

bool NdmRamDriver::BadBlock(uint32_t page_num) const {
  return IsFlagSet(kBadBlockFlag, &flags_[page_num / PagesPerBlock()]);
}

void NdmRamDriver::SetWritten(uint32_t page_num, bool value) {
  if (value) {
    SetFlag(kWrittenFlag, &flags_[page_num]);
  } else {
    ClearFlag(kWrittenFlag, &flags_[page_num]);
  }
}

void NdmRamDriver::SetFailEcc(uint32_t page_num, bool value) {
  if (value) {
    SetFlag(kFailEccFlag, &flags_[page_num]);
  } else {
    ClearFlag(kFailEccFlag, &flags_[page_num]);
  }
}

void NdmRamDriver::SetUnsafeEcc(uint32_t page_num, bool value) {
  if (value) {
    SetFlag(kUnsafeEccFlag, &flags_[page_num]);
  } else {
    ClearFlag(kUnsafeEccFlag, &flags_[page_num]);
  }
}

void NdmRamDriver::SetBadBlock(uint32_t page_num, bool value) {
  // It doesn't really matter where the flag is stored.
  if (value) {
    SetFlag(kBadBlockFlag, &flags_[page_num / PagesPerBlock()]);
  } else {
    ClearFlag(kBadBlockFlag, &flags_[page_num / PagesPerBlock()]);
  }
}

uint32_t NdmRamDriver::PagesPerBlock() const { return options_.block_size / options_.page_size; }

bool NdmRamDriver::ShouldTriggerPowerFailure() {
  ZX_ASSERT(!power_failure_triggered_);
  if (test_options_.power_failure_delay < 0) {
    return false;
  }
  power_failure_delay_++;
  return (power_failure_triggered_ = (power_failure_delay_ > test_options_.power_failure_delay));
}

void NdmRamDriver::OnWritePowerFailure(uint64_t page_number, const uint8_t* data,
                                       const uint8_t* spare) {
  SetWritten(page_number, true);
  if (test_options_.emulate_half_write_on_power_failure) {
    // Write the first half of the page and oob.
    memcpy(MainData(page_number), data, options_.page_size / 2);
    memcpy(SpareData(page_number), spare, options_.eb_size / 2);
  }
}

void NdmRamDriver::OnErasePowerFailure(uint64_t page_number) {
  for (uint32_t offset = 0; offset < PagesPerBlock(); ++offset) {
    uint64_t page = page_number + offset;
    // If ECC is always calculated, this should not matter.
    memset(MainData(page), 0xFF, options_.page_size);
    memset(SpareData(page), 0xFF, options_.eb_size);
    SetWritten(page, true);
    SetFailEcc(page, false);
    SetUnsafeEcc(page, false);
  }
}
