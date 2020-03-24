// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_FTL_TEST_NDM_RAM_DRIVER_H_
#define SRC_STORAGE_BLOCK_DRIVERS_FTL_TEST_NDM_RAM_DRIVER_H_

#include <inttypes.h>
#include <lib/ftl/ndm-driver.h>
#include <zircon/types.h>

#include <fbl/array.h>

struct TestOptions {
  int ecc_error_interval;  // Controls simulation of ECC errors.
  int bad_block_interval;  // Controls simulation of bad block sequence.
  int bad_block_burst;     // Controls the size of the sequence of operations that will run
                           // into a bad block.
  bool use_half_size;      // Makes only half of the space visible.
  bool save_config_data;   // Save options on the partition info.
};
constexpr TestOptions kDefaultTestOptions = {900, 50, 1, false, true};

// Ram-backed driver for testing purposes.
class NdmRamDriver final : public ftl::NdmBaseDriver {
 public:
  NdmRamDriver(const ftl::VolumeOptions& options) : NdmRamDriver(options, kDefaultTestOptions) {}
  NdmRamDriver(const ftl::VolumeOptions& options, const TestOptions& test_options)
      : options_(options), test_options_(test_options) {}
  ~NdmRamDriver() final {}

  // Extends the visible volume to the whole size of the storage.
  bool DoubleSize();

  void save_config_data(bool value) { test_options_.save_config_data = value; }

  void set_options(const ftl::VolumeOptions& options) { options_ = options; }
  void set_max_bad_blocks(uint32_t value) { options_.max_bad_blocks = value; }
  uint32_t num_bad_blocks() const { return num_bad_blocks_; }

  // NdmDriver interface:
  const char* Init() final;
  const char* Attach(const ftl::Volume* ftl_volume) final;
  bool Detach() final;
  int NandRead(uint32_t start_page, uint32_t page_count, void* page_buffer, void* oob_buffer) final;
  int NandWrite(uint32_t start_page, uint32_t page_count, const void* page_buffer,
                const void* oob_buffer) final;
  int NandErase(uint32_t page_num) final;
  int IsBadBlock(uint32_t page_num) final;
  bool IsEmptyPage(uint32_t page_num, const uint8_t* data, const uint8_t* spare) final;

 private:
  // Reads or Writes a single page.
  int ReadPage(uint32_t page_num, uint8_t* data, uint8_t* spare);
  int WritePage(uint32_t page_num, const uint8_t* data, const uint8_t* spare);

  // Returns true for a freshly minted bad block.
  bool SimulateBadBlock(uint32_t page_num);

  // Access the main data and spare area for a given page.
  uint8_t* MainData(uint32_t page_num);
  uint8_t* SpareData(uint32_t page_num);

  // Access flags for a given page.
  bool Written(uint32_t page_num);
  bool FailEcc(uint32_t page_num);
  bool BadBlock(uint32_t page_num);
  void SetWritten(uint32_t page_num, bool value);
  void SetFailEcc(uint32_t page_num, bool value);
  void SetBadBlock(uint32_t page_num, bool value);

  uint32_t PagesPerBlock() const;

  fbl::Array<uint8_t> volume_;
  fbl::Array<uint8_t> flags_;
  ftl::VolumeOptions options_;
  TestOptions test_options_;
  int ecc_error_interval_ = 0;  // Controls simulation of ECC errors.
  int bad_block_interval_ = 0;  // Controls simulation of bad blocks.
  uint32_t num_bad_blocks_ = 0;
};

#endif  // SRC_STORAGE_BLOCK_DRIVERS_FTL_TEST_NDM_RAM_DRIVER_H_
