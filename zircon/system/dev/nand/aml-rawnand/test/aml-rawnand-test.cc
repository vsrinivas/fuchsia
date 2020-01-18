// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-rawnand.h"

#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>

#include <map>
#include <memory>
#include <queue>

#include <ddk/io-buffer.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "onfi.h"

namespace amlrawnand {

namespace {

// Amlogic NAND register info.
constexpr size_t kNandRegSize = sizeof(uint32_t);
constexpr size_t kNandRegTotalBytes = 0x3C;
constexpr size_t kClockRegSize = sizeof(uint32_t);
constexpr size_t kClockRegTotalBytes = 4;

// Toshiba TC58NVG2S0F NAND settings (taken from Astro).
constexpr uint8_t kTestNandManufacturerId = 0x98;
constexpr uint8_t kTestNandDeviceId = 0xDC;
constexpr uint8_t kTestNandExtendedId = 0x26;

// Special BL2 page0 contents (taken from Astro).
constexpr uint16_t kPage0OobValue = 0xAA55;
// clang-format off
constexpr uint8_t kPage0Data[] = {
  0x04, 0x00, 0xE3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x40, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
  0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
  0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
// clang-format on

// Derived constants.
constexpr uint32_t kTestNandWriteSize = 4 * 1024;  // Derived from extended ID.
constexpr int kTestNandEccPages = 4;               // 4KiB NAND page / 1 KiB ECC page.
constexpr int kTestNandUserBytes = 8;              // 4 ECC pages * 2 user bytes per page.

// A test NAND page we can feed into AmlRawNand reads.
struct NandPage {
  std::vector<uint8_t> data;
  std::vector<AmlInfoFormat> info;

  // Initializes in a valid state to allow successful reads.
  NandPage() : data(kTestNandWriteSize), info(kTestNandEccPages) {
    for (auto& info_block : info) {
      info_block.ecc.completed = 1;
    }
  }
};

// Returns a NandPage that looks like a 0-page.
NandPage NandPage0() {
  NandPage page0;
  memcpy(&page0.data[0], kPage0Data, sizeof(kPage0Data));
  for (AmlInfoFormat& info_block : page0.info) {
    info_block.info_bytes = kPage0OobValue;
  }
  return page0;
}

// A stub Onfi implementation that just tracks the most recent command.
class StubOnfi : public Onfi {
 public:
  void OnfiCommand(uint32_t command, int32_t column, int32_t page_addr, uint32_t capacity_mb,
                   uint32_t chip_delay_us, int buswidth_16) override {
    last_command_ = command;
    last_page_address_ = page_addr;
  }

  zx_status_t OnfiWait(zx::duration timeout, zx::duration first_interval,
                       zx::duration polling_interval) override {
    return ZX_OK;
  }

  uint32_t last_command() const { return last_command_; }
  int32_t last_page_address() const { return last_page_address_; }

 private:
  uint32_t last_command_ = 0;
  int32_t last_page_address_ = 0;
};

// Provides the necessary support to make AmlRawNand testable.
class FakeAmlRawNand : public AmlRawNand {
 public:
  // Factory method so we can indicate failure by returning nullptr.
  static std::unique_ptr<FakeAmlRawNand> Create() {
    // Zircon objects required by AmlRawNand.
    zx::bti bti;
    EXPECT_OK(fake_bti_create(bti.reset_and_get_address()));
    if (!bti.is_valid()) {
      return nullptr;
    }
    zx::interrupt interrupt;
    EXPECT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &interrupt));
    if (!interrupt.is_valid()) {
      return nullptr;
    }

    // We need to create these before the AmlRawNand object but also ensure that
    // the buffers don't move around so put them on the heap.
    auto mock_nand_regs = std::make_unique<ddk_mock::MockMmioReg[]>(kNandRegTotalBytes);
    auto mock_nand_reg_region = std::make_unique<ddk_mock::MockMmioRegRegion>(
        mock_nand_regs.get(), kNandRegSize, kNandRegTotalBytes);
    auto mock_clock_regs = std::make_unique<ddk_mock::MockMmioReg[]>(kClockRegTotalBytes);
    auto mock_clock_reg_region = std::make_unique<ddk_mock::MockMmioRegRegion>(
        mock_clock_regs.get(), kClockRegSize, kClockRegTotalBytes);

    // The AmlRawNand object owns the Onfi, but we retain a raw pointer to it
    // so we can interact with it during tests.
    auto stub_onfi = std::make_unique<StubOnfi>();
    auto stub_onfi_raw = stub_onfi.get();

    auto nand = std::unique_ptr<FakeAmlRawNand>(
        new FakeAmlRawNand(std::move(bti), std::move(interrupt), std::move(mock_nand_regs),
                           std::move(mock_nand_reg_region), std::move(mock_clock_regs),
                           std::move(mock_clock_reg_region), std::move(stub_onfi)));
    nand->stub_onfi_ = stub_onfi_raw;

    // Initialize the AmlRawNand with some parameters taken from a real device.
    nand->PrepareForInit();
    zx_status_t status = nand->Init();
    EXPECT_OK(status);
    if (status != ZX_OK) {
      return nullptr;
    }

    return nand;
  }

  // On test exit, make sure we sent all the bytes we expected to.
  ~FakeAmlRawNand() override { EXPECT_TRUE(fake_read_bytes_.empty()); }

  // Sets a fake NAND page for RawNandReadPageHwecc(), overwriting any page
  // data current at this index.
  void SetFakeNandPageRead(uint32_t index, const NandPage page) {
    fake_read_page_map_[index] = std::move(page);
  }

  // Queues a fake NAND byte for AmlReadByte().
  void QueueFakeNandByteRead(uint8_t byte) { fake_read_bytes_.push(byte); }

  // Sets up the necessary fake page and byte reads to successfully initialize
  // the AmlRawNand object.
  void PrepareForInit() {
    // First we read the first 2 ID bytes.
    QueueFakeNandByteRead(kTestNandManufacturerId);
    QueueFakeNandByteRead(kTestNandDeviceId);

    // Next we read the full 8-byte ID string, of which we only care about
    // a few bytes.
    QueueFakeNandByteRead(kTestNandManufacturerId);
    QueueFakeNandByteRead(kTestNandDeviceId);
    QueueFakeNandByteRead(0x00);
    QueueFakeNandByteRead(kTestNandExtendedId);
    QueueFakeNandByteRead(0x00);
    QueueFakeNandByteRead(0x00);
    QueueFakeNandByteRead(0x00);
    QueueFakeNandByteRead(0x00);

    // Next we read the page0 metadata.
    SetFakeNandPageRead(0, NandPage0());
  }

 protected:
  zx_status_t AmlQueueRB() override { return ZX_OK; }

  zx_status_t AmlWaitDmaFinish() override {
    switch (stub_onfi_->last_command()) {
      case NAND_CMD_READ0:
        return PerformFakeRead(stub_onfi_->last_page_address());
      case NAND_CMD_SEQIN:
        return PerformFakeWrite(stub_onfi_->last_page_address());
    }

    ADD_FAILURE("AmlWaitDmaFinish() called with unknown Onfi command 0x%02X",
                stub_onfi_->last_command());
    return ZX_ERR_INTERNAL;
  }

  uint8_t AmlReadByte() override {
    if (fake_read_bytes_.empty()) {
      ADD_FAILURE("AmlReadByte() called with no fake bytes ready");
      return 0x00;
    }
    uint8_t byte = fake_read_bytes_.front();
    fake_read_bytes_.pop();
    return byte;
  }

 private:
  FakeAmlRawNand(zx::bti bti, zx::interrupt interrupt,
                 std::unique_ptr<ddk_mock::MockMmioReg[]> mock_nand_regs,
                 std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_nand_reg_region,
                 std::unique_ptr<ddk_mock::MockMmioReg[]> mock_clock_regs,
                 std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_clock_reg_region,
                 std::unique_ptr<Onfi> onfi)
      : AmlRawNand(fake_ddk::kFakeParent, ddk::MmioBuffer(mock_nand_reg_region->GetMmioBuffer()),
                   ddk::MmioBuffer(mock_clock_reg_region->GetMmioBuffer()), std::move(bti),
                   std::move(interrupt), std::move(onfi)),
        mock_nand_regs_(std::move(mock_nand_regs)),
        mock_nand_reg_region_(std::move(mock_nand_reg_region)),
        mock_clock_regs_(std::move(mock_clock_regs)),
        mock_clock_reg_region_(std::move(mock_clock_reg_region)) {}

  zx_status_t PerformFakeRead(uint32_t page_index) {
    auto iter = fake_read_page_map_.find(page_index);
    if (iter == fake_read_page_map_.end()) {
      ADD_FAILURE("PerformFakeRead(): page %u hasn't been set", page_index);
      return ZX_ERR_INTERNAL;
    }

    const NandPage& page = iter->second;

    const size_t data_bytes = page.data.size() * sizeof(page.data[0]);
    const size_t info_bytes = page.info.size() * sizeof(page.info[0]);

    if (data_buffer().size() < data_bytes) {
      ADD_FAILURE("Fake page data size is larger than the buffer");
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    if (info_buffer().size() < info_bytes) {
      ADD_FAILURE("Fake page info size is larger than the buffer");
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(data_buffer().virt(), &page.data[0], data_bytes);
    memcpy(info_buffer().virt(), &page.info[0], info_bytes);

    return ZX_OK;
  }

  zx_status_t PerformFakeWrite(uint32_t) {
    // No tests need this yet.
    return ZX_OK;
  }

  std::unique_ptr<ddk_mock::MockMmioReg[]> mock_nand_regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_nand_reg_region_;

  std::unique_ptr<ddk_mock::MockMmioReg[]> mock_clock_regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_clock_reg_region_;

  StubOnfi* stub_onfi_;

  std::map<uint32_t, NandPage> fake_read_page_map_;
  std::queue<uint8_t> fake_read_bytes_;
};

TEST(AmlRawnand, FakeNandCreate) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);
}

TEST(AmlRawnand, ReadPage) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  NandPage page;
  page.data.front() = 0x55;
  page.data.back() = 0xAA;
  page.info.front().info_bytes = 0x1234;
  page.info.back().info_bytes = 0xABCD;
  ASSERT_NO_FATAL_FAILURES(nand->SetFakeNandPageRead(5, page));

  std::vector<uint8_t> data(kTestNandWriteSize);
  std::vector<uint16_t> oob(kTestNandUserBytes / 2);  // /2 for 16-bit values.
  size_t data_bytes_read = 0;
  size_t oob_bytes_read = 0;
  uint32_t ecc_correct = -1;
  ASSERT_OK(nand->RawNandReadPageHwecc(5, &data[0], kTestNandWriteSize, &data_bytes_read, &oob[0],
                                       kTestNandUserBytes, &oob_bytes_read, &ecc_correct));

  EXPECT_EQ(kTestNandWriteSize, data_bytes_read);
  EXPECT_EQ(kTestNandUserBytes, oob_bytes_read);
  EXPECT_EQ(0, ecc_correct);
  EXPECT_EQ(0x55, data.front());
  EXPECT_EQ(0xAA, data.back());
  EXPECT_EQ(0x1234, oob.front());
  EXPECT_EQ(0xABCD, oob.back());
}

TEST(AmlRawnand, ReadPageDataOnly) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  NandPage page;
  page.data.front() = 0x55;
  page.data.back() = 0xAA;
  ASSERT_NO_FATAL_FAILURES(nand->SetFakeNandPageRead(5, page));

  std::vector<uint8_t> data(kTestNandWriteSize);
  size_t data_bytes_read = 0;
  uint32_t ecc_correct = -1;
  ASSERT_OK(nand->RawNandReadPageHwecc(5, &data[0], kTestNandWriteSize, &data_bytes_read, nullptr,
                                       0, nullptr, &ecc_correct));

  EXPECT_EQ(kTestNandWriteSize, data_bytes_read);
  EXPECT_EQ(0, ecc_correct);
  EXPECT_EQ(0x55, data.front());
  EXPECT_EQ(0xAA, data.back());
}

TEST(AmlRawnand, ReadPageOobOnly) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  NandPage page;
  page.info.front().info_bytes = 0x1234;
  page.info.back().info_bytes = 0xABCD;
  ASSERT_NO_FATAL_FAILURES(nand->SetFakeNandPageRead(5, page));

  std::vector<uint16_t> oob(kTestNandUserBytes / 2);
  size_t oob_bytes_read = 0;
  uint32_t ecc_correct = -1;
  ASSERT_OK(nand->RawNandReadPageHwecc(5, nullptr, 0, nullptr, &oob[0], kTestNandUserBytes,
                                       &oob_bytes_read, &ecc_correct));

  EXPECT_EQ(kTestNandUserBytes, oob_bytes_read);
  EXPECT_EQ(0, ecc_correct);
  EXPECT_EQ(0x1234, oob.front());
  EXPECT_EQ(0xABCD, oob.back());
}

}  // namespace

}  // namespace amlrawnand
