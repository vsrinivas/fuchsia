// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/nand/drivers/aml-rawnand/aml-rawnand.h"

#include <lib/ddk/io-buffer.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/bti.h>
#include <lib/zx/interrupt.h>

#include <map>
#include <memory>
#include <queue>

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/aml-common/aml-rawnand.h>
#include <zxtest/zxtest.h>

namespace amlrawnand {

namespace {

// Amlogic NAND register info.
constexpr size_t kNandRegSize = sizeof(uint32_t);
constexpr size_t kNandRegCount = 14;
constexpr size_t kClockRegSize = sizeof(uint32_t);
constexpr size_t kClockRegCount = 1;

// Toshiba TC58NVG2S0F NAND settings (taken from Astro).
constexpr uint8_t kTestNandManufacturerId = 0x98;
constexpr uint8_t kTestNandDeviceId = 0xDC;
constexpr uint8_t kTestNandExtendedId = 0x26;
constexpr uint32_t kTestNandWriteSize = 4 * 1024;  // Derived from extended ID.

// Other configuration constants (Astro).
constexpr uint32_t kNumBl2Pages = 1024;                // Based on BL2 partition size.
constexpr uint32_t kFirstNonBl2Page = kNumBl2Pages;    // Redefined for test readability.
constexpr int kDefaultNumEccPages = 4;                 // 4KiB NAND page / 1 KiB ECC page.
constexpr int kDefaultNumUserBytes = 8;                // 4 ECC pages * 2 user bytes per page.
constexpr uint32_t kDefaultWriteCommand = 0x00210004;  // Match what the bootloader uses.
constexpr uint32_t kDefaultReadCommand = 0x00230004;   // Match what the bootloader uses.
constexpr uint32_t kRandomSeedOffset = 0xC2;           // Match what the bootloader uses.
static_assert(kTestNandWriteSize % kDefaultNumEccPages == 0);
constexpr int kDefaultEccPageSize = kTestNandWriteSize / kDefaultNumEccPages;

constexpr uint16_t kPage0OobValue = 0xAA55;
constexpr int kPage0NumEccPages = 8;                 // 8 ECC shortpages.
constexpr uint32_t kPage0WriteCommand = 0x0029EC08;  // Match what the bootloader uses.
constexpr uint32_t kPage0ReadCommand = 0x002BEC08;   // Match what the bootloader uses.
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

// A test NAND page we can feed into AmlRawNand reads.
struct NandPage {
  std::vector<uint8_t> data;
  std::vector<AmlInfoFormat> info;
  bool ecc_fail = false;

  // Initializes in a valid state to allow successful reads.
  NandPage(size_t ecc_pages) : data(kTestNandWriteSize), info(ecc_pages) {
    for (auto& info_block : info) {
      info_block.ecc.completed = 1;
    }
  }

  // Default constructor creates a default (non-Page0) page.
  NandPage() : NandPage(kDefaultNumEccPages) {}
};

// Returns a NandPage that looks like a 0-page. Optionally enable rand_mode.
NandPage NandPage0(bool rand_mode) {
  NandPage page0(kPage0NumEccPages);
  memcpy(&page0.data[0], kPage0Data, sizeof(kPage0Data));
  if (rand_mode) {
    page0.data[2] |= 0x08;
  }
  for (AmlInfoFormat& info_block : page0.info) {
    info_block.info_bytes = kPage0OobValue;
  }
  return page0;
}

NandPage NandPage0Invalid(bool rand_mode) {
  NandPage page0 = NandPage0(kPage0NumEccPages);
  page0.ecc_fail = true;
  return page0;
}

// A stub Onfi implementation that just tracks the most recent command.
class StubOnfi : public Onfi {
 public:
  void OnfiCommand(uint32_t command, int32_t column, int32_t page_addr, uint32_t capacity_mb,
                   uint32_t chip_delay_us, int buswidth_16) override {
    last_command_ = command;
    // The final command before reading page data is READ0 with no column/page address. Don't accept
    // -1 as an address in this case.
    if (command != NAND_CMD_READ0 || page_addr != -1) {
      last_page_address_ = page_addr;
    }

    command_callback_(command);
  }

  zx_status_t OnfiWait(zx::duration timeout, zx::duration polling_interval) override {
    return ZX_OK;
  }

  uint32_t last_command() const { return last_command_; }
  int32_t last_page_address() const { return last_page_address_; }

  void set_command_callback(fit::function<void(uint32_t)> command_callback) {
    command_callback_ = std::move(command_callback);
  }

 private:
  uint32_t last_command_ = 0;
  int32_t last_page_address_ = 0;
  fit::function<void(int32_t)> command_callback_;
};

// Provides the necessary support to make AmlRawNand testable.
//
// Contains a fake page map that holds read/write information. Reading requires
// first staging a fake page with SetFakePage(), but writing will just
// overwrite whatever data is there or create a new fake page.
class FakeAmlRawNand : public AmlRawNand {
 public:
  // Factory method so we can indicate failure by returning nullptr.
  static std::unique_ptr<FakeAmlRawNand> Create(const uint32_t page0_valid_copy = 0,
                                                bool rand_mode = false) {
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
    auto mock_nand_regs = std::make_unique<ddk_mock::MockMmioReg[]>(kNandRegCount);
    auto mock_nand_reg_region = std::make_unique<ddk_mock::MockMmioRegRegion>(
        mock_nand_regs.get(), kNandRegSize, kNandRegCount);
    auto mock_clock_regs = std::make_unique<ddk_mock::MockMmioReg[]>(kClockRegCount);
    auto mock_clock_reg_region = std::make_unique<ddk_mock::MockMmioRegRegion>(
        mock_clock_regs.get(), kClockRegSize, kClockRegCount);

    // The AmlRawNand object owns the Onfi, but we retain a raw pointer to it
    // so we can interact with it during tests.
    auto stub_onfi = std::make_unique<StubOnfi>();
    auto stub_onfi_raw = stub_onfi.get();

    auto nand = std::unique_ptr<FakeAmlRawNand>(
        new FakeAmlRawNand(std::move(bti), std::move(interrupt), std::move(mock_nand_regs),
                           std::move(mock_nand_reg_region), std::move(mock_clock_regs),
                           std::move(mock_clock_reg_region), std::move(stub_onfi), rand_mode));
    nand->stub_onfi_ = stub_onfi_raw;

    // Initialize the AmlRawNand with some parameters taken from a real device.
    nand->PrepareForInit(page0_valid_copy, *stub_onfi_raw);
    zx_status_t status = nand->Init();
    EXPECT_OK(status);
    if (status != ZX_OK) {
      return nullptr;
    }

    status = nand->Bind();
    EXPECT_OK(status);
    if (status != ZX_OK) {
      return nullptr;
    }

    // Clear any pages we needed to set for proper initialization so we start
    // with a blank slate for tests.
    nand->fake_page_map_.clear();

    return nand;
  }

  // On test exit, make sure we met all the expectations we had.
  ~FakeAmlRawNand() override {
    CleanUpIrq();
    EXPECT_TRUE(fake_read_bytes_.empty());
    mock_nand_reg_region_->VerifyAll();
    mock_clock_reg_region_->VerifyAll();
  }

  // Returns the current fake data at the given page.
  //
  // If the page hasn't been staged or written yet, returns an empty page
  // but marks a test failure.
  const NandPage& GetFakePage(uint32_t index) {
    auto iter = fake_page_map_.find(index);
    if (iter == fake_page_map_.end()) {
      ADD_FAILURE("NandPage %u hasn't been staged or written yet", index);
      return fake_page_map_[index] = NandPage();
    }
    return iter->second;
  }

  // Sets a fake NAND page for RawNandReadPageHwecc(), overwriting any page
  // data current at this index.
  void SetFakePage(uint32_t index, NandPage page) { fake_page_map_[index] = std::move(page); }

  // Returns true if the fake page has been set or written.
  bool FakePageExists(uint32_t index) const { return fake_page_map_.count(index) > 0; }

  // Queues a fake NAND byte for AmlReadByte().
  void QueueFakeNandByteRead(uint8_t byte) { fake_read_bytes_.push(byte); }

  // Sets up an expectation for the given read/write command and its
  // corresponding setup commands, including the "set seed" command if
  // |random_seed| is present.
  static constexpr auto kNoRandomSeed = std::nullopt;  // Make call sites more readable.
  void ExpectReadWriteCommand(uint32_t command, std::optional<uint32_t> random_seed) {
    ddk_mock::MockMmioReg& command_register = (*mock_nand_reg_region_)[P_NAND_CMD];

    // First we expect to configure the data and info buffer physical addresses.
    const auto data_addr = data_buffer().phys();
    const auto info_addr = info_buffer().phys();
    EXPECT_NE(0, data_addr);
    EXPECT_NE(0, info_addr);
    if (command == kDefaultReadCommand || command == kPage0ReadCommand) {
      command_register.ExpectWrite(AML_CMD_IDLE | NAND_CE0);
      command_register.ExpectWrite(AML_CMD_IDLE | NAND_CE0 | 4);
      command_register.ExpectWrite(NAND_CE0 | AML_CMD_CLE | NAND_CMD_STATUS);
      command_register.ExpectWrite(AML_CMD_IDLE | NAND_CE0 | 3);
      command_register.ExpectWrite(AML_CMD_RB | AML_CMD_IO6 | 0x18);
    }
    command_register.ExpectWrite(AML_CMD_ADL | (data_addr & 0xFFFF));
    command_register.ExpectWrite(AML_CMD_ADH | ((data_addr >> 16) & 0xFFFF));
    command_register.ExpectWrite(AML_CMD_AIL | (info_addr & 0xFFFF));
    command_register.ExpectWrite(AML_CMD_AIH | ((info_addr >> 16) & 0xFFFF));

    // Then we may set the random seed if randomization is used.
    if (random_seed) {
      // Note: we are intentionally masking *before* adding the offset. This
      // seems incorrect according to the docs, but is what the bootloader does,
      // and we have to match its behavior in order to make sure we're always
      // using matching seed values.
      command_register.ExpectWrite(AML_CMD_SEED |
                                   (kRandomSeedOffset + (random_seed.value() & 0x7FFF)));
    }

    // Finally we expect the actual read/write command.
    command_register.ExpectWrite(command);

    if (command == kDefaultWriteCommand || command == kPage0WriteCommand) {
      command_register.ExpectWrite(AML_CMD_IDLE | NAND_CE0);
      command_register.ExpectWrite(AML_CMD_IDLE | NAND_CE0);
    }
  }

  using AmlRawNand::bti;

 protected:
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
                 std::unique_ptr<Onfi> onfi, bool rand_mode)
      : AmlRawNand(fake_ddk::kFakeParent, ddk::MmioBuffer(mock_nand_reg_region->GetMmioBuffer()),
                   ddk::MmioBuffer(mock_clock_reg_region->GetMmioBuffer()), std::move(bti),
                   std::move(interrupt), std::move(onfi)),
        rand_mode_(rand_mode),
        mock_nand_regs_(std::move(mock_nand_regs)),
        mock_nand_reg_region_(std::move(mock_nand_reg_region)),
        mock_clock_regs_(std::move(mock_clock_regs)),
        mock_clock_reg_region_(std::move(mock_clock_reg_region)) {}

  // Sets up the necessary fake page and byte reads to successfully initialize
  // the AmlRawNand object.
  void PrepareForInit(uint32_t page0_valid_copy, StubOnfi& onfi) {
    onfi.set_command_callback([&](int32_t command) { NandCommand(command); });

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

    // Set a valid page0 to the specified copy and dummy pages to others.
    // This is to make sure that test does not panic when AmlRawNand is iterating through
    // the 8 copies.
    for (uint32_t i = 0; i < 8; i++) {
      SetFakePage(i * 128,
                  i == page0_valid_copy ? NandPage0(rand_mode_) : NandPage0Invalid(rand_mode_));
    }
  }

  zx_status_t PerformFakeRead(uint32_t page_index) {
    auto iter = fake_page_map_.find(page_index);
    if (iter == fake_page_map_.end()) {
      ADD_FAILURE("PerformFakeRead(): page %u hasn't been set", page_index);
      return ZX_ERR_INTERNAL;
    }

    NandPage& page = iter->second;

    const size_t data_bytes = page.data.size() * sizeof(page.data[0]);
    const size_t info_bytes = page.info.size() * sizeof(page.info[0]);

    // Make sure the buffers are big enough to hold the page.
    if (data_buffer().size() < data_bytes) {
      ADD_FAILURE("Fake page data size is larger than the buffer");
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    if (info_buffer().size() < info_bytes) {
      ADD_FAILURE("Fake page info size is larger than the buffer");
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    for (auto& info_block : page.info) {
      info_block.ecc.completed = 1;
      if (page.ecc_fail) {
        info_block.ecc.eccerr_cnt = 0x3f;
        info_block.zero_bits = 0x3f;
      }
    }

    memcpy(data_buffer().virt(), &page.data[0], data_bytes);
    memcpy(info_buffer().virt(), &page.info[0], info_bytes);

    return ZX_OK;
  }

  zx_status_t PerformFakeWrite(uint32_t page_index) {
    // We could calculate whether |page_index| is a page0 metadata page or not,
    // but it doesn't matter since we're just allocating buffers, so we always
    // make it a page0 which has the larger OOB buffer.
    NandPage& page = fake_page_map_[page_index] = NandPage0(rand_mode_);

    const size_t data_bytes = page.data.size() * sizeof(page.data[0]);
    const size_t info_bytes = page.info.size() * sizeof(page.info[0]);

    // Make sure the buffers are big enough to fill up the page.
    if (data_buffer().size() < data_bytes) {
      ADD_FAILURE("Fake page data size is larger than the buffer");
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    if (info_buffer().size() < info_bytes) {
      ADD_FAILURE("Fake page info size is larger than the buffer");
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(&page.data[0], data_buffer().virt(), data_bytes);
    // Since AmlInfoFormat isn't trivially copyable, we can't memcpy() directly
    // into it but have to copy each field individually instead.
    const uint8_t* buffer = reinterpret_cast<const uint8_t*>(info_buffer().virt());
    for (AmlInfoFormat& info : page.info) {
      static_assert(sizeof(info.info_bytes) == 2);
      memcpy(&info.info_bytes, buffer, 2);
      buffer += 2;

      static_assert(sizeof(info.zero_bits) == 1);
      info.zero_bits = *(buffer++);

      static_assert(sizeof(info.ecc.raw_value) == 1);
      info.ecc.raw_value = *(buffer++);

      static_assert(sizeof(info.reserved) == 4);
      memcpy(&info.reserved, buffer, 4);
      buffer += 4;
    }

    return ZX_OK;
  }

  void NandCommand(int32_t command) {
    switch (command) {
      case NAND_CMD_READ0:
        PerformFakeRead(stub_onfi_->last_page_address());
        break;
      case NAND_CMD_SEQIN:
        PerformFakeWrite(stub_onfi_->last_page_address());
        break;
    }
  }

  bool rand_mode_;

  std::unique_ptr<ddk_mock::MockMmioReg[]> mock_nand_regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_nand_reg_region_;

  std::unique_ptr<ddk_mock::MockMmioReg[]> mock_clock_regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_clock_reg_region_;

  StubOnfi* stub_onfi_;

  std::map<uint32_t, NandPage> fake_page_map_;
  std::queue<uint8_t> fake_read_bytes_;
};

TEST(AmlRawnand, FakeNandCreate) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);
}

TEST(AmlRawnand, FakeNandCreateWithPage0AtADifferentCopy) {
  auto nand = FakeAmlRawNand::Create(7);
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
  ASSERT_NO_FATAL_FAILURE(nand->SetFakePage(5, page));

  std::vector<uint8_t> data(kTestNandWriteSize);
  std::vector<uint16_t> oob(kDefaultNumUserBytes / 2);  // /2 for 16-bit values.
  size_t data_bytes_read = 0;
  size_t oob_bytes_read = 0;
  uint32_t ecc_correct = -1;
  ASSERT_OK(nand->RawNandReadPageHwecc(5, data.data(), kTestNandWriteSize, &data_bytes_read,
                                       reinterpret_cast<uint8_t*>(oob.data()), kDefaultNumUserBytes,
                                       &oob_bytes_read, &ecc_correct));

  EXPECT_EQ(kTestNandWriteSize, data_bytes_read);
  EXPECT_EQ(kDefaultNumUserBytes, oob_bytes_read);
  EXPECT_EQ(0, ecc_correct);
  EXPECT_EQ(0x55, data.front());
  EXPECT_EQ(0xAA, data.back());
  EXPECT_EQ(0x1234, oob.front());
  EXPECT_EQ(0xABCD, oob.back());
}

TEST(AmlRawnand, ReadPageNullEcc) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  NandPage page;
  page.data.front() = 0x55;
  page.data.back() = 0xAA;
  page.info.front().info_bytes = 0x1234;
  page.info.back().info_bytes = 0xABCD;
  ASSERT_NO_FATAL_FAILURE(nand->SetFakePage(5, page));

  std::vector<uint8_t> data(kTestNandWriteSize);
  std::vector<uint16_t> oob(kDefaultNumUserBytes / 2);  // /2 for 16-bit values.
  size_t data_bytes_read = 0;
  size_t oob_bytes_read = 0;
  ASSERT_OK(nand->RawNandReadPageHwecc(5, data.data(), kTestNandWriteSize, &data_bytes_read,
                                       reinterpret_cast<uint8_t*>(oob.data()), kDefaultNumUserBytes,
                                       &oob_bytes_read, nullptr));

  EXPECT_EQ(kTestNandWriteSize, data_bytes_read);
  EXPECT_EQ(kDefaultNumUserBytes, oob_bytes_read);
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
  ASSERT_NO_FATAL_FAILURE(nand->SetFakePage(5, page));

  std::vector<uint8_t> data(kTestNandWriteSize);
  size_t data_bytes_read = 0;
  uint32_t ecc_correct = -1;
  ASSERT_OK(nand->RawNandReadPageHwecc(5, data.data(), kTestNandWriteSize, &data_bytes_read,
                                       nullptr, 0, nullptr, &ecc_correct));

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
  ASSERT_NO_FATAL_FAILURE(nand->SetFakePage(5, page));

  std::vector<uint16_t> oob(kDefaultNumUserBytes / 2);
  size_t oob_bytes_read = 0;
  uint32_t ecc_correct = -1;
  ASSERT_OK(nand->RawNandReadPageHwecc(5, nullptr, 0, nullptr,
                                       reinterpret_cast<uint8_t*>(oob.data()), kDefaultNumUserBytes,
                                       &oob_bytes_read, &ecc_correct));

  EXPECT_EQ(kDefaultNumUserBytes, oob_bytes_read);
  EXPECT_EQ(0, ecc_correct);
  EXPECT_EQ(0x1234, oob.front());
  EXPECT_EQ(0xABCD, oob.back());
}

TEST(AmlRawnand, ReadErasedPage) {
  auto nand = FakeAmlRawNand::Create(0, true);
  ASSERT_NOT_NULL(nand);

  NandPage page;
  memset(&page.data[0], 0xff, kTestNandWriteSize);
  for (auto& info : page.info) {
    info.info_bytes = 0xffff;
    info.ecc.eccerr_cnt = AML_ECC_UNCORRECTABLE_CNT;
    info.zero_bits = 0;
  }
  ASSERT_NO_FATAL_FAILURE(nand->SetFakePage(5, page));

  std::vector<uint8_t> data(kTestNandWriteSize);
  std::vector<uint16_t> oob(kDefaultNumUserBytes / 2);  // /2 for 16-bit values.
  size_t data_bytes_read = 0;
  size_t oob_bytes_read = 0;
  uint32_t ecc_correct = -1;
  ASSERT_OK(nand->RawNandReadPageHwecc(5, data.data(), kTestNandWriteSize, &data_bytes_read,
                                       reinterpret_cast<uint8_t*>(oob.data()), kDefaultNumUserBytes,
                                       &oob_bytes_read, &ecc_correct));

  EXPECT_EQ(kTestNandWriteSize, data_bytes_read);
  EXPECT_EQ(kDefaultNumUserBytes, oob_bytes_read);
  EXPECT_EQ(0xff, data.front());
  EXPECT_EQ(0xff, data.back());
  EXPECT_EQ(0xffff, oob.front());
  EXPECT_EQ(0xffff, oob.back());

  // Repeat read with various nullptr combinations to ensure no crash.
  ASSERT_OK(nand->RawNandReadPageHwecc(5, nullptr, kTestNandWriteSize, &data_bytes_read,
                                       reinterpret_cast<uint8_t*>(oob.data()), kDefaultNumUserBytes,
                                       &oob_bytes_read, &ecc_correct));
  ASSERT_OK(nand->RawNandReadPageHwecc(5, data.data(), kTestNandWriteSize, &data_bytes_read,
                                       nullptr, kDefaultNumUserBytes, &oob_bytes_read,
                                       &ecc_correct));
  ASSERT_OK(nand->RawNandReadPageHwecc(5, nullptr, kTestNandWriteSize, &data_bytes_read, nullptr,
                                       kDefaultNumUserBytes, &oob_bytes_read, &ecc_correct));
  ASSERT_OK(nand->RawNandReadPageHwecc(5, nullptr, kTestNandWriteSize, nullptr,
                                       reinterpret_cast<uint8_t*>(oob.data()), kDefaultNumUserBytes,
                                       nullptr, &ecc_correct));
  ASSERT_OK(nand->RawNandReadPageHwecc(5, nullptr, kTestNandWriteSize, nullptr, nullptr,
                                       kDefaultNumUserBytes, nullptr, &ecc_correct));
}

TEST(AmlRawnand, PartialErasedPage) {
  auto nand = FakeAmlRawNand::Create(0, true);
  ASSERT_NOT_NULL(nand);

  NandPage page;
  memset(&page.data[0], 0xff, kTestNandWriteSize);
  for (auto& info : page.info) {
    info.info_bytes = 0xffff;
    info.ecc.eccerr_cnt = AML_ECC_UNCORRECTABLE_CNT;
    info.zero_bits = 0;
  }
  // Make the first page be not an erased page.
  memset(&page.data[0], 0xA5, kDefaultEccPageSize);
  page.info.front().info_bytes = 0x5A5A;
  page.info.front().ecc.eccerr_cnt = 0;
  page.info.front().zero_bits = AML_ECC_UNCORRECTABLE_CNT;

  ASSERT_NO_FATAL_FAILURE(nand->SetFakePage(5, page));

  std::vector<uint8_t> data(kTestNandWriteSize);
  std::vector<uint16_t> oob(kDefaultNumUserBytes / 2);  // /2 for 16-bit values.
  size_t data_bytes_read = 0;
  size_t oob_bytes_read = 0;
  uint32_t ecc_correct = -1;
  ASSERT_EQ(nand->RawNandReadPageHwecc(5, data.data(), kTestNandWriteSize, &data_bytes_read,
                                       reinterpret_cast<uint8_t*>(oob.data()), kDefaultNumUserBytes,
                                       &oob_bytes_read, &ecc_correct),
            ZX_ERR_IO_DATA_INTEGRITY);

  EXPECT_EQ(kTestNandWriteSize, data_bytes_read);
  EXPECT_EQ(kDefaultNumUserBytes, oob_bytes_read);
  EXPECT_EQ(0xA5, data.front());
  EXPECT_EQ(0xff, data.back());
  EXPECT_EQ(0x5A5A, oob.front());
  EXPECT_EQ(0xffff, oob.back());
}

TEST(AmlRawnand, ErasedPageAllOnes) {
  auto nand = FakeAmlRawNand::Create(0, true);
  ASSERT_NOT_NULL(nand);

  NandPage page;
  memset(&page.data[0], 0xff, kTestNandWriteSize);
  for (auto& info : page.info) {
    info.info_bytes = 0xffff;
    info.ecc.eccerr_cnt = AML_ECC_UNCORRECTABLE_CNT;
    info.zero_bits = 0;
  }
  // Make the first byte have a random bitflip.
  page.data[0] = 0xFE;
  page.info[0].zero_bits = 1;

  ASSERT_NO_FATAL_FAILURE(nand->SetFakePage(5, page));

  std::vector<uint8_t> data(kTestNandWriteSize);
  std::vector<uint16_t> oob(kDefaultNumUserBytes / 2);  // /2 for 16-bit values.
  size_t data_bytes_read = 0;
  size_t oob_bytes_read = 0;
  uint32_t ecc_correct = -1;
  ASSERT_OK(nand->RawNandReadPageHwecc(5, data.data(), kTestNandWriteSize, &data_bytes_read,
                                       reinterpret_cast<uint8_t*>(oob.data()), kDefaultNumUserBytes,
                                       &oob_bytes_read, &ecc_correct));

  EXPECT_EQ(kTestNandWriteSize, data_bytes_read);
  EXPECT_EQ(kDefaultNumUserBytes, oob_bytes_read);
  EXPECT_EQ(0xff, data.front());
  EXPECT_EQ(0xff, data.back());
  EXPECT_EQ(0xffff, oob.front());
  EXPECT_EQ(0xffff, oob.back());
}

TEST(AmlRawnand, WritePage) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  std::vector<uint8_t> data(kTestNandWriteSize);
  std::vector<uint16_t> oob(kDefaultNumEccPages);
  data[0] = 0x11;
  data[kTestNandWriteSize - 1] = 0x22;
  oob[0] = 0x5566;
  oob[kDefaultNumEccPages - 1] = 0xAABB;
  // We have to write to a page index outside of BL2 because all BL2 pages
  // require special OOB values.
  ASSERT_OK(nand->RawNandWritePageHwecc(data.data(), kTestNandWriteSize,
                                        reinterpret_cast<uint8_t*>(oob.data()),
                                        kDefaultNumUserBytes, kFirstNonBl2Page));

  const NandPage& page = nand->GetFakePage(kFirstNonBl2Page);
  EXPECT_EQ(0x11, page.data[0]);
  EXPECT_EQ(0x22, page.data[kTestNandWriteSize - 1]);
  EXPECT_EQ(0x5566, page.info[0].info_bytes);
  EXPECT_EQ(0xAABB, page.info[kDefaultNumEccPages - 1].info_bytes);
}

TEST(AmlRawnand, WritePageDataOnly) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  std::vector<uint8_t> data(kTestNandWriteSize);
  data[0] = 0x11;
  data[kTestNandWriteSize - 1] = 0x22;
  ASSERT_OK(
      nand->RawNandWritePageHwecc(data.data(), kTestNandWriteSize, nullptr, 0, kFirstNonBl2Page));

  const NandPage& page = nand->GetFakePage(kFirstNonBl2Page);
  EXPECT_EQ(0x11, page.data[0]);
  EXPECT_EQ(0x22, page.data[kTestNandWriteSize - 1]);
}

TEST(AmlRawnand, WritePageOobOnly) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  std::vector<uint16_t> oob(kDefaultNumEccPages);
  oob[0] = 0x5566;
  oob[kDefaultNumEccPages - 1] = 0xAABB;
  ASSERT_OK(nand->RawNandWritePageHwecc(nullptr, 0, reinterpret_cast<uint8_t*>(oob.data()),
                                        kDefaultNumUserBytes, kFirstNonBl2Page));

  const NandPage& page = nand->GetFakePage(kFirstNonBl2Page);
  EXPECT_EQ(0x5566, page.info[0].info_bytes);
  EXPECT_EQ(0xAABB, page.info[kDefaultNumEccPages - 1].info_bytes);
}

TEST(AmlRawnand, WritePageShortOob) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  std::vector<uint16_t> oob(kDefaultNumEccPages);
  oob[0] = 0x1234;
  oob[1] = 0x5678;
  ASSERT_OK(nand->RawNandWritePageHwecc(nullptr, 0, reinterpret_cast<uint8_t*>(oob.data()), 2,
                                        kFirstNonBl2Page));

  // The driver should pad with zeros since we only told it to use 2 OOB bytes.
  const NandPage& page = nand->GetFakePage(kFirstNonBl2Page);
  EXPECT_EQ(0x1234, page.info[0].info_bytes);
  EXPECT_EQ(0x0000, page.info[1].info_bytes);
  EXPECT_EQ(0x0000, page.info[kDefaultNumEccPages - 1].info_bytes);
}

TEST(AmlRawnand, WritePageShortOobOddBytes) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  std::vector<uint16_t> oob(kDefaultNumEccPages);
  oob[0] = 0x1234;
  oob[1] = 0x5678;
  ASSERT_OK(nand->RawNandWritePageHwecc(nullptr, 0, reinterpret_cast<uint8_t*>(oob.data()), 3,
                                        kFirstNonBl2Page));

  // The driver should pad with zeros since we only told it to use 3 OOB bytes.
  const NandPage& page = nand->GetFakePage(kFirstNonBl2Page);
  EXPECT_EQ(0x1234, page.info[0].info_bytes);
  EXPECT_EQ(0x0078, page.info[1].info_bytes);  // Little-endian: LSB comes first.
  EXPECT_EQ(0x0000, page.info[kDefaultNumEccPages - 1].info_bytes);
}

TEST(AmlRawnand, WritePageShortOobZeroBytes) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  std::vector<uint16_t> oob(kDefaultNumEccPages);
  oob[0] = 0x1234;
  ASSERT_OK(nand->RawNandWritePageHwecc(nullptr, 0, reinterpret_cast<uint8_t*>(oob.data()), 0,
                                        kFirstNonBl2Page));

  // The driver should pad with zeros since we told it to use 0 OOB bytes.
  const NandPage& page = nand->GetFakePage(kFirstNonBl2Page);
  EXPECT_EQ(0x0000, page.info[0].info_bytes);
  EXPECT_EQ(0x0000, page.info[kDefaultNumEccPages - 1].info_bytes);
}

TEST(AmlRawnand, WriteBl2Page) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  const uint32_t page_index = kNumBl2Pages - 1;
  std::vector<uint8_t> data(kTestNandWriteSize);
  data[0] = 0x11;
  data[kTestNandWriteSize - 1] = 0x22;
  ASSERT_OK(nand->RawNandWritePageHwecc(data.data(), kTestNandWriteSize, nullptr, 0, page_index));

  const NandPage& page = nand->GetFakePage(page_index);
  EXPECT_EQ(0x11, page.data[0]);
  EXPECT_EQ(0x22, page.data[kTestNandWriteSize - 1]);

  // We didn't supply any OOB bytes, but the driver should have automatically
  // used the magic BL2 values for each page.
  ASSERT_GE(page.info.size(), kPage0NumEccPages);
  for (int i = 0; i < kPage0NumEccPages; ++i) {
    EXPECT_EQ(kPage0OobValue, page.info[i].info_bytes);
  }
}

TEST(AmlRawnand, WriteBl2PageInvalidOobError) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  std::vector<uint8_t> data(kTestNandWriteSize);
  std::vector<uint16_t> oob(kDefaultNumEccPages);

  // The driver should refuse to write custom OOB bytes to BL2 pages.
  for (uint32_t page_index : {0u, kNumBl2Pages / 2, kNumBl2Pages - 1}) {
    ASSERT_EQ(nand->RawNandWritePageHwecc(data.data(), kTestNandWriteSize,
                                          reinterpret_cast<uint8_t*>(oob.data()),
                                          kDefaultNumUserBytes, page_index),
              ZX_ERR_INVALID_ARGS);

    // Make sure we never attempted to write.
    EXPECT_FALSE(nand->FakePageExists(page_index));
  }
}

// Read/write command tests - ensure we're sending the right commands to the
// NAND control registers.

TEST(AmlRawnand, WritePage0Command) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  // We expect randomization to always be on for page0 metadata pages.
  nand->ExpectReadWriteCommand(kPage0WriteCommand, 0);

  std::vector<uint8_t> data(kTestNandWriteSize);
  ASSERT_OK(nand->RawNandWritePageHwecc(data.data(), kTestNandWriteSize, nullptr, 0, 0));
}

TEST(AmlRawnand, ReadPage0Command) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  // We expect randomization to always be on for page0 metadata pages.
  nand->ExpectReadWriteCommand(kPage0ReadCommand, 0);
  nand->SetFakePage(0, NandPage0(false));

  std::vector<uint8_t> data(kTestNandWriteSize);
  size_t data_bytes_read = 0;
  uint32_t ecc_correct = -1;
  ASSERT_OK(nand->RawNandReadPageHwecc(0, data.data(), kTestNandWriteSize, &data_bytes_read,
                                       nullptr, 0, nullptr, &ecc_correct));
}

TEST(AmlRawnand, WriteBl2Command) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  // We don't use randomization for other BL2 pages.
  nand->ExpectReadWriteCommand(kDefaultWriteCommand, FakeAmlRawNand::kNoRandomSeed);

  std::vector<uint8_t> data(kTestNandWriteSize);
  ASSERT_OK(nand->RawNandWritePageHwecc(data.data(), kTestNandWriteSize, nullptr, 0, 1));
}

TEST(AmlRawnand, ReadBl2Command) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  // We don't use randomization for other BL2 pages.
  nand->ExpectReadWriteCommand(kDefaultReadCommand, FakeAmlRawNand::kNoRandomSeed);
  nand->SetFakePage(1, NandPage());

  std::vector<uint8_t> data(kTestNandWriteSize);
  size_t data_bytes_read = 0;
  uint32_t ecc_correct = -1;
  ASSERT_OK(nand->RawNandReadPageHwecc(1, data.data(), kTestNandWriteSize, &data_bytes_read,
                                       nullptr, 0, nullptr, &ecc_correct));
}

TEST(AmlRawnand, WriteCommand) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  // We don't use randomization for normal pages.
  nand->ExpectReadWriteCommand(kDefaultWriteCommand, FakeAmlRawNand::kNoRandomSeed);

  std::vector<uint8_t> data(kTestNandWriteSize);
  ASSERT_OK(
      nand->RawNandWritePageHwecc(data.data(), kTestNandWriteSize, nullptr, 0, kFirstNonBl2Page));
}

TEST(AmlRawnand, ReadCommand) {
  auto nand = FakeAmlRawNand::Create();
  ASSERT_NOT_NULL(nand);

  // We don't use randomization for normal pages.
  nand->ExpectReadWriteCommand(kDefaultReadCommand, FakeAmlRawNand::kNoRandomSeed);
  nand->SetFakePage(kFirstNonBl2Page, NandPage());

  std::vector<uint8_t> data(kTestNandWriteSize);
  size_t data_bytes_read = 0;
  uint32_t ecc_correct = -1;
  ASSERT_OK(nand->RawNandReadPageHwecc(kFirstNonBl2Page, data.data(), kTestNandWriteSize,
                                       &data_bytes_read, nullptr, 0, nullptr, &ecc_correct));
}

TEST(AmlRawNand, SuspendReleasesAllPins) {
  fake_ddk::Bind ddk;
  auto nand = FakeAmlRawNand::Create();
  zx_info_bti_t bti_info;
  size_t actual = 0, avail = 0;
  ASSERT_EQ(nand->bti().get_info(ZX_INFO_BTI, &bti_info, sizeof(bti_info), &actual, &avail), ZX_OK);
  EXPECT_GT(bti_info.pmo_count, 0);
  ASSERT_NOT_NULL(nand);
  ddk::SuspendTxn txn(nand->zxdev(), 0, 0, 0);
  nand->DdkSuspend(std::move(txn));
  ddk.WaitUntilSuspend();
  ASSERT_EQ(nand->bti().get_info(ZX_INFO_BTI, &bti_info, sizeof(bti_info), &actual, &avail), ZX_OK);
  EXPECT_EQ(bti_info.pmo_count, 0);
}

TEST(AmlRawNand, OperationsCanceledAfterSuspend) {
  fake_ddk::Bind ddk;
  auto nand = FakeAmlRawNand::Create();

  nand->ExpectReadWriteCommand(kDefaultWriteCommand, FakeAmlRawNand::kNoRandomSeed);
  std::vector<uint8_t> data(kTestNandWriteSize);
  EXPECT_OK(
      nand->RawNandWritePageHwecc(data.data(), kTestNandWriteSize, nullptr, 0, kFirstNonBl2Page));

  ddk::SuspendTxn txn(nand->zxdev(), 0, false, DEVICE_SUSPEND_REASON_REBOOT);
  nand->DdkSuspend(std::move(txn));
  ddk.WaitUntilSuspend();

  EXPECT_EQ(
      nand->RawNandWritePageHwecc(data.data(), kTestNandWriteSize, nullptr, 0, kFirstNonBl2Page),
      ZX_ERR_CANCELED);
}

}  // namespace

}  // namespace amlrawnand
