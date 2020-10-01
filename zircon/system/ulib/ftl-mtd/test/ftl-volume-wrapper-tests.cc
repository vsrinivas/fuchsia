// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/ftl-mtd/ftl-volume-wrapper.h>
#include <string.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

#include <zxtest/zxtest.h>

#include "zircon/types.h"

using namespace ftl_mtd;

namespace {

constexpr uint32_t kPageSize = 4 * 1024;  // 4 KiB
constexpr uint32_t kNumPages = 128;
constexpr uint32_t kSize = kNumPages * kPageSize;

class FakeVolume final : public ftl::Volume {
 public:
  explicit FakeVolume() {}
  ~FakeVolume() final {}

  bool written() const { return written_; }
  bool initialized() const { return initialized_; }
  bool flushed() const { return flushed_; }
  uint32_t first_page() const { return first_page_; }
  int num_pages() const { return num_pages_; }

  void set_ftl_instance(ftl::FtlInstance* instance) { ftl_instance_ = instance; }
  void set_read_buffer(uint8_t* buffer) { read_buffer_ = buffer; }
  void set_write_buffer(uint8_t* buffer) { write_buffer_ = buffer; }
  void set_fail_read(bool fail_read) { fail_read_ = fail_read; }

  // Volume interface.
  const char* Init(std::unique_ptr<ftl::NdmDriver> driver) final {
    initialized_ = ftl_instance_->OnVolumeAdded(kPageSize, kNumPages);
    return nullptr;
  }
  const char* ReAttach() final { return nullptr; }
  zx_status_t Read(uint32_t first_page, int num_pages, void* buffer) final {
    if (fail_read_) {
      return ZX_ERR_IO;
    }

    first_page_ = first_page;
    num_pages_ = num_pages;
    memcpy(buffer, read_buffer_, num_pages * kPageSize);
    return ZX_OK;
  }
  zx_status_t Write(uint32_t first_page, int num_pages, const void* buffer) final {
    first_page_ = first_page;
    num_pages_ = num_pages;
    if (memcmp(write_buffer_, buffer, num_pages * kPageSize)) {
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    written_ = true;
    return ZX_OK;
  }
  zx_status_t Format() final { return ZX_OK; }
  zx_status_t FormatAndLevel() final { return ZX_OK; }
  zx_status_t Mount() final { return ZX_OK; }
  zx_status_t Unmount() final { return ZX_OK; }
  zx_status_t Flush() final {
    flushed_ = true;
    return ZX_OK;
  }
  zx_status_t Trim(uint32_t first_page, uint32_t num_pages) final { return ZX_OK; }
  zx_status_t GarbageCollect() final { return ZX_OK; }
  zx_status_t GetStats(Stats* stats) final { return ZX_OK; }
  zx_status_t GetCounters(Counters* counters) final { return ZX_OK; }

 private:
  ftl::FtlInstance* ftl_instance_;
  uint8_t* read_buffer_;
  uint8_t* write_buffer_;

  uint32_t first_page_ = 0;
  int num_pages_ = 0;
  bool initialized_ = false;
  bool written_ = false;
  bool flushed_ = false;
  bool fail_read_ = false;
};

class FtlVolumeWrapperTest : public zxtest::Test {
 protected:
  void SetUp() override {
    auto volume = std::make_unique<FakeVolume>();
    volume_ = volume.get();

    ftl_volume_wrapper_ = std::make_unique<FtlVolumeWrapper>(std::move(volume));
    volume_->set_ftl_instance(ftl_volume_wrapper_.get());
    ASSERT_OK(ftl_volume_wrapper_->Init(std::unique_ptr<ftl::NdmBaseDriver>()));
    ASSERT_TRUE(volume_->initialized());
  }

  FakeVolume* volume_;
  std::unique_ptr<FtlVolumeWrapper> ftl_volume_wrapper_;
};

TEST_F(FtlVolumeWrapperTest, SeekSucceeds) {
  int offset = 3 * kPageSize;

  ASSERT_EQ(kSize - offset, ftl_volume_wrapper_->Seek(offset, SEEK_END));
  ASSERT_EQ(kSize - offset, ftl_volume_wrapper_->Tell());

  ASSERT_EQ(offset, ftl_volume_wrapper_->Seek(offset, SEEK_SET));
  ASSERT_EQ(offset, ftl_volume_wrapper_->Tell());

  ASSERT_EQ(2 * offset, ftl_volume_wrapper_->Seek(offset, SEEK_CUR));
  ASSERT_EQ(2 * offset, ftl_volume_wrapper_->Tell());
  ASSERT_EQ(0, ftl_volume_wrapper_->Seek(0, SEEK_SET));

  // Negative values should also work
  offset *= -1;

  ASSERT_EQ(kSize - offset, ftl_volume_wrapper_->Seek(offset, SEEK_END));
  ASSERT_EQ(kSize - offset, ftl_volume_wrapper_->Tell());

  ASSERT_EQ(-2 * offset, ftl_volume_wrapper_->Seek(-2 * offset, SEEK_SET));
  ASSERT_EQ(-offset, ftl_volume_wrapper_->Seek(offset, SEEK_CUR));
  ASSERT_EQ(-offset, ftl_volume_wrapper_->Tell());
}

TEST_F(FtlVolumeWrapperTest, SeekFails) {
  // Seek offset must be an integer multiple of page size;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ftl_volume_wrapper_->Seek(kPageSize / 2, SEEK_END));
  ASSERT_EQ(0, ftl_volume_wrapper_->Tell());

  // Unknown whence returns error.
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ftl_volume_wrapper_->Seek(0, -1));
  ASSERT_EQ(0, ftl_volume_wrapper_->Tell());

  // An offset that would cause overflow of page index returns out of range error.
  off_t large_offset = std::numeric_limits<off_t>::max() / kPageSize * kPageSize;
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, ftl_volume_wrapper_->Seek(-large_offset, SEEK_END));
  ASSERT_EQ(0, ftl_volume_wrapper_->Tell());

  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, ftl_volume_wrapper_->Seek(large_offset, SEEK_CUR));
  ASSERT_EQ(0, ftl_volume_wrapper_->Tell());
}

TEST_F(FtlVolumeWrapperTest, WriteSucceeds) {
  uint32_t first_page = 8;
  uint32_t num_pages = 3;
  uint32_t write_len = num_pages * kPageSize;

  auto buffer = std::make_unique<uint8_t[]>(write_len);
  memset(buffer.get(), 0x8A, write_len);
  volume_->set_write_buffer(buffer.get());

  uint32_t byte_offset = first_page * kPageSize;
  ASSERT_EQ(byte_offset, ftl_volume_wrapper_->Seek(byte_offset, SEEK_SET));
  ASSERT_EQ(write_len, ftl_volume_wrapper_->Write(buffer.get(), write_len));
  ASSERT_EQ(byte_offset + write_len, ftl_volume_wrapper_->Tell());
  ASSERT_OK(ftl_volume_wrapper_->Sync());

  ASSERT_TRUE(volume_->written());
  ASSERT_TRUE(volume_->flushed());
  ASSERT_EQ(first_page, volume_->first_page());
  ASSERT_EQ(num_pages, volume_->num_pages());
}

TEST_F(FtlVolumeWrapperTest, BadWriteReturnsError) {
  auto dropped_write_buffer = std::make_unique<uint8_t[]>(kPageSize);
  memset(dropped_write_buffer.get(), 0xAA, kPageSize);
  volume_->set_write_buffer(dropped_write_buffer.get());

  auto expected_write_buffer = std::make_unique<uint8_t[]>(kPageSize);
  memset(expected_write_buffer.get(), 0x11, kPageSize);

  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY,
            ftl_volume_wrapper_->Write(expected_write_buffer.get(), kPageSize));
  ASSERT_EQ(0, ftl_volume_wrapper_->Tell());
}

TEST_F(FtlVolumeWrapperTest, ReadSucceeds) {
  uint32_t first_page = 5;
  uint32_t num_pages = 2;
  uint32_t read_len = num_pages * kPageSize;

  auto read_buffer = std::make_unique<uint8_t[]>(read_len);
  memset(read_buffer.get(), 0x6C, read_len);
  volume_->set_read_buffer(read_buffer.get());

  auto test_buffer = std::make_unique<uint8_t[]>(read_len);

  uint32_t byte_offset = first_page * kPageSize;
  ASSERT_EQ(byte_offset, ftl_volume_wrapper_->Seek(byte_offset, SEEK_SET));
  ASSERT_EQ(read_len, ftl_volume_wrapper_->Read(test_buffer.get(), read_len));
  ASSERT_EQ(byte_offset + read_len, ftl_volume_wrapper_->Tell());

  ASSERT_BYTES_EQ(read_buffer.get(), test_buffer.get(), read_len);
  ASSERT_EQ(first_page, volume_->first_page());
  ASSERT_EQ(num_pages, volume_->num_pages());
}

TEST_F(FtlVolumeWrapperTest, BadReadReturnsError) {
  auto read_buffer = std::make_unique<uint8_t[]>(kPageSize);
  volume_->set_fail_read(true);
  ASSERT_EQ(ZX_ERR_IO, ftl_volume_wrapper_->Read(read_buffer.get(), kPageSize));
  ASSERT_EQ(0, ftl_volume_wrapper_->Tell());
}

TEST_F(FtlVolumeWrapperTest, OutOfRangeReadWriteReturnsZero) {
  auto buffer = std::make_unique<uint8_t[]>(kPageSize);

  ASSERT_EQ(ftl_volume_wrapper_->Size(), ftl_volume_wrapper_->Seek(0, SEEK_END));

  ASSERT_EQ(0, ftl_volume_wrapper_->Read(buffer.get(), kPageSize));
  ASSERT_EQ(ftl_volume_wrapper_->Size(), ftl_volume_wrapper_->Tell());
  ASSERT_EQ(0, ftl_volume_wrapper_->Write(buffer.get(), kPageSize));
  ASSERT_EQ(ftl_volume_wrapper_->Size(), ftl_volume_wrapper_->Tell());
}

TEST_F(FtlVolumeWrapperTest, NonAlignedReadWriteReturnsInvalidArgs) {
  std::unique_ptr<uint8_t[]> buffer = std::make_unique<uint8_t[]>(kPageSize + 1);

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ftl_volume_wrapper_->Read(buffer.get(), kPageSize + 1));
  ASSERT_EQ(0, ftl_volume_wrapper_->Tell());
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, ftl_volume_wrapper_->Write(buffer.get(), kPageSize + 1));
  ASSERT_EQ(0, ftl_volume_wrapper_->Tell());
}

}  // namespace
