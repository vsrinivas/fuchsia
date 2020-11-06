// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include <lib/fake_ddk/fake_ddk.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <mock-mmio-reg/mock-mmio-reg.h>

#include "src/devices/lib/metadata/llcpp/registers.h"

namespace registers {

namespace {

constexpr size_t kRegSize = 0x00001000;

}  // namespace

template <typename T>
class FakeRegistersDevice : public RegistersDevice<T> {
 public:
  static std::unique_ptr<FakeRegistersDevice> Create(std::vector<MmioInfo> mmios) {
    fbl::AllocChecker ac;
    auto device = fbl::make_unique_checked<FakeRegistersDevice>(&ac);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: device object alloc failed", __func__);
      return nullptr;
    }
    device->Init(std::move(mmios));

    return device;
  }

  void AddRegister(uint32_t mmio_index, RegistersMetadataEntry config) {
    clients_.emplace(config.id(), RegistersDevice<T>::AddRegister(mmio_index, config));
  }

  std::shared_ptr<::llcpp::fuchsia::hardware::registers::Device::SyncClient> GetClient(
      uint64_t id) {
    return clients_[id];
  }

  explicit FakeRegistersDevice() : RegistersDevice<T>(nullptr) {}

 private:
  std::map<uint64_t, std::shared_ptr<::llcpp::fuchsia::hardware::registers::Device::SyncClient>>
      clients_;
};

class RegistersDeviceTest : public zxtest::Test {
 public:
  template <typename T>
  std::unique_ptr<FakeRegistersDevice<T>> Init(uint32_t mmio_count) {
    fbl::AllocChecker ac;

    std::vector<MmioInfo> mmios;
    for (uint32_t i = 0; i < mmio_count; i++) {
      regs_.push_back(
          fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize / sizeof(T)], kRegSize / sizeof(T)));
      if (!ac.check()) {
        zxlogf(ERROR, "%s: regs_[%u] alloc failed", __func__, i);
        return nullptr;
      }
      mock_mmio_.push_back(std::make_unique<ddk_mock::MockMmioRegRegion>(regs_[i].get(), sizeof(T),
                                                                         kRegSize / sizeof(T)));

      std::vector<fbl::Mutex> locks(kRegSize / sizeof(T));
      mmios.push_back({
          .mmio = mock_mmio_[i]->GetMmioBuffer(),
          .base_address = 0,
          .locks = std::move(locks),
      });
    }

    return FakeRegistersDevice<T>::Create(std::move(mmios));
  }

  void TearDown() override {
    for (uint32_t i = 0; i < mock_mmio_.size(); i++) {
      ASSERT_NO_FATAL_FAILURES(mock_mmio_[i]->VerifyAll());
    }
  }

 protected:
  // Mmio Regs and Regions
  std::vector<fbl::Array<ddk_mock::MockMmioReg>> regs_;
  std::vector<std::unique_ptr<ddk_mock::MockMmioRegRegion>> mock_mmio_;

  std::map<uint64_t, std::unique_ptr<::llcpp::fuchsia::hardware::registers::Device::SyncClient>>
      clients_;

  fidl::BufferThenHeapAllocator<2048> allocator_;
};

TEST_F(RegistersDeviceTest, EncodeDecodeTest) {
  auto mmio_alloc = allocator_.make<MmioMetadataEntry[]>(3);
  fidl::VectorView<MmioMetadataEntry> mmio(std::move(mmio_alloc), 3);
  mmio[0] = registers::BuildMetadata(allocator_, 0x1234123412341234);
  mmio[1] = registers::BuildMetadata(allocator_, 0x4321432143214321);
  mmio[2] = registers::BuildMetadata(allocator_, 0xABCDABCDABCDABCD);

  auto registers_alloc = allocator_.make<RegistersMetadataEntry[]>(2);
  fidl::VectorView<RegistersMetadataEntry> registers(std::move(registers_alloc), 2);
  registers[0] = registers::BuildMetadata(
      allocator_, 0, 0x1111111111111111,
      std::vector<std::pair<uint32_t, uint32_t>>{{0xFFFF, 3}, {0x8888, 2}});
  registers[1] = registers::BuildMetadata(
      allocator_, 1, 0x2222222222222222,
      std::vector<std::pair<uint32_t, uint32_t>>{{0x5555, 1}, {0x77777777, 2}, {0x1234, 4}});

  auto metadata_original =
      registers::BuildMetadata(allocator_, std::move(mmio), std::move(registers));
  fidl::OwnedEncodedMessage<Metadata> msg(&metadata_original);
  EXPECT_EQ(msg.GetOutgoingMessage().handle_actual(), 0);
  EXPECT_EQ(msg.GetOutgoingMessage().handles(), nullptr);

  auto metadata = Metadata::DecodedMessage::FromOutgoingWithRawHandleCopy(&msg);
  ASSERT_TRUE(metadata.ok(), "%s", metadata.error());
  ASSERT_EQ(metadata.PrimaryObject()->mmio().count(), 3);
  EXPECT_EQ(metadata.PrimaryObject()->mmio()[0].base_address(), 0x1234123412341234);
  EXPECT_EQ(metadata.PrimaryObject()->mmio()[1].base_address(), 0x4321432143214321);
  EXPECT_EQ(metadata.PrimaryObject()->mmio()[2].base_address(), 0xABCDABCDABCDABCD);
  ASSERT_EQ(metadata.PrimaryObject()->registers().count(), 2);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].id(), 0);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].base_address(), 0x1111111111111111);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].masks()[0].mask().r32(), 0xFFFF);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].masks()[0].count(), 3);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].masks()[1].mask().r32(), 0x8888);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].masks()[1].count(), 2);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].id(), 1);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].base_address(), 0x2222222222222222);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[0].mask().r32(), 0x5555);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[0].count(), 1);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[1].mask().r32(), 0x77777777);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[1].count(), 2);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[2].mask().r32(), 0x1234);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[2].count(), 4);
}

TEST_F(RegistersDeviceTest, InvalidDecodeTest) {
  fidl::OwnedEncodedMessage<Metadata> msg(nullptr);
  auto metadata = Metadata::DecodedMessage::FromOutgoingWithRawHandleCopy(&msg);
  EXPECT_FALSE(metadata.ok());
}

TEST_F(RegistersDeviceTest, Read32Test) {
  auto device = Init<uint32_t>(/* mmio_count: */ 3);
  ASSERT_NOT_NULL(device);

  device->AddRegister(
      /* mmio_index: */ 0,
      /* config: */
      BuildMetadata(allocator_, 0, 0x0,
                    std::vector<std::pair<uint32_t, uint32_t>>{{0xFFFFFFFF, 1}}));
  device->AddRegister(
      /* mmio_index: */ 2,
      /* config: */
      BuildMetadata(allocator_, 1, 0x0,
                    std::vector<std::pair<uint32_t, uint32_t>>{{0xFFFFFFFF, 2}, {0xFFFF0000, 1}}));

  // Invalid Call
  auto invalid_call_result = device->GetClient(0)->ReadRegister8(/* address: */
                                                                 0x0000000000000000,
                                                                 /* mask: */ 0xFF);
  ASSERT_TRUE(invalid_call_result.ok());
  EXPECT_FALSE(invalid_call_result->result.is_response());

  // Address not aligned
  auto unaligned_result = device->GetClient(0)->ReadRegister32(/* address: */ 0x0000000000000001,
                                                               /* mask: */ 0xFFFFFFFF);
  EXPECT_TRUE(unaligned_result.ok());
  EXPECT_FALSE(unaligned_result->result.is_response());

  // Address out of range
  auto out_of_range_result = device->GetClient(1)->ReadRegister32(/* address: */
                                                                  0x000000000000000C,
                                                                  /* mask: */ 0xFFFFFFFF);
  ASSERT_TRUE(out_of_range_result.ok());
  EXPECT_FALSE(out_of_range_result->result.is_response());

  // Invalid mask
  auto invalid_mask_result = device->GetClient(1)->ReadRegister32(/* address: */
                                                                  0x0000000000000008,
                                                                  /* mask: */ 0xFFFFFFFF);
  EXPECT_TRUE(invalid_mask_result.ok());
  EXPECT_FALSE(invalid_mask_result->result.is_response());

  // Successful
  (*(mock_mmio_[0]))[0x0].ExpectRead(0x12341234);
  auto read_result1 =
      device->GetClient(0)->ReadRegister32(/* address: */ 0x00000000, /* mask: */ 0xFFFFFFFF);
  ASSERT_TRUE(read_result1.ok());
  ASSERT_TRUE(read_result1->result.is_response());
  EXPECT_EQ(read_result1->result.response().value, 0x12341234);

  (*(mock_mmio_[2]))[0x4].ExpectRead(0x12341234);
  auto read_result2 =
      device->GetClient(1)->ReadRegister32(/* address: */ 0x00000004, /* mask: */ 0xFFFF0000);
  EXPECT_TRUE(read_result2.ok());
  EXPECT_TRUE(read_result2->result.is_response());
  EXPECT_EQ(read_result2->result.response().value, 0x12340000);
}

TEST_F(RegistersDeviceTest, Write32Test) {
  auto device = Init<uint32_t>(/* mmio_count: */ 2);
  ASSERT_NOT_NULL(device);

  device->AddRegister(
      /* mmio_index: */ 0,
      /* config: */
      BuildMetadata(allocator_, 0, 0x0,
                    std::vector<std::pair<uint32_t, uint32_t>>{{0xFFFFFFFF, 1}}));
  device->AddRegister(
      /* mmio_index: */ 1,
      /* config: */
      BuildMetadata(allocator_, 1, 0x0,
                    std::vector<std::pair<uint32_t, uint32_t>>{{0xFFFFFFFF, 2}, {0xFFFF0000, 1}}));

  // Invalid Call
  auto invalid_call_result = device->GetClient(0)->WriteRegister8(/* address: */
                                                                  0x0000000000000000,
                                                                  /* mask: */ 0xFF,
                                                                  /* value:  */ 0x12);
  ASSERT_TRUE(invalid_call_result.ok());
  EXPECT_FALSE(invalid_call_result->result.is_response());

  // Address not aligned
  auto unaligned_result = device->GetClient(0)->WriteRegister32(/* address: */
                                                                0x0000000000000001,
                                                                /* mask: */ 0xFFFFFFFF,
                                                                /* value: */ 0x43214321);
  ASSERT_TRUE(unaligned_result.ok());
  EXPECT_FALSE(unaligned_result->result.is_response());

  // Address out of range
  auto out_of_range_result =
      device->GetClient(1)->WriteRegister32(/* address: */ 0x000000000000000C,
                                            /* mask: */ 0xFFFFFFFF,
                                            /* value: */ 0x43214321);
  EXPECT_TRUE(out_of_range_result.ok());
  EXPECT_FALSE(out_of_range_result->result.is_response());

  // Invalid mask
  auto invalid_mask_result =
      device->GetClient(1)->WriteRegister32(/* address: */ 0x0000000000000008,
                                            /* mask: */ 0xFFFFFFFF,
                                            /* value: */ 0x43214321);
  EXPECT_TRUE(invalid_mask_result.ok());
  EXPECT_FALSE(invalid_mask_result->result.is_response());

  // Successful
  (*(mock_mmio_[0]))[0x0].ExpectRead(0x00000000).ExpectWrite(0x43214321);
  auto read_result1 = device->GetClient(0)->WriteRegister32(
      /* address: */ 0x00000000, /* mask: */ 0xFFFFFFFF, /* value: */ 0x43214321);
  EXPECT_TRUE(read_result1.ok());
  EXPECT_TRUE(read_result1->result.is_response());

  (*(mock_mmio_[1]))[0x4].ExpectRead(0x00000000).ExpectWrite(0x43210000);
  auto read_result2 = device->GetClient(1)->WriteRegister32(
      /* address: */ 0x00000004, /* mask: */ 0xFFFF0000, /* value: */ 0x43214321);
  EXPECT_TRUE(read_result2.ok());
  EXPECT_TRUE(read_result2->result.is_response());
}

TEST_F(RegistersDeviceTest, Read64Test) {
  auto device = Init<uint64_t>(/* mmio_count: */ 3);
  ASSERT_NOT_NULL(device);

  device->AddRegister(
      /* mmio_index: */ 0,
      /* config: */
      BuildMetadata(allocator_, 0, 0x0,
                    std::vector<std::pair<uint64_t, uint32_t>>{{0xFFFFFFFFFFFFFFFF, 1}}));
  device->AddRegister(
      /* mmio_index: */ 2,
      /* config: */
      BuildMetadata(
          allocator_, 1, 0x0,
          std::vector<std::pair<uint64_t, uint32_t>>{
              {0xFFFFFFFFFFFFFFFF, 1}, {0x00000000FFFFFFFF, 1}, {0x0000FFFFFFFF0000, 1}}));

  // Invalid Call
  auto invalid_call_result = device->GetClient(0)->ReadRegister8(/* address: */
                                                                 0x0000000000000000,
                                                                 /* mask: */ 0xFF);
  ASSERT_TRUE(invalid_call_result.ok());
  EXPECT_FALSE(invalid_call_result->result.is_response());

  // Address not aligned
  auto unaligned_result = device->GetClient(0)->ReadRegister64(/* address: */ 0x0000000000000001,
                                                               /* mask: */ 0xFFFFFFFFFFFFFFFF);
  ASSERT_TRUE(unaligned_result.ok());
  EXPECT_FALSE(unaligned_result->result.is_response());

  // Address out of range
  auto out_of_range_result = device->GetClient(1)->ReadRegister64(/* address: */
                                                                  0x0000000000000020,
                                                                  /* mask: */
                                                                  0xFFFFFFFFFFFFFFFF);
  ASSERT_TRUE(out_of_range_result.ok());
  EXPECT_FALSE(out_of_range_result->result.is_response());

  // Invalid mask
  auto invalid_mask_result = device->GetClient(1)->ReadRegister64(/* address: */
                                                                  0x0000000000000008,
                                                                  /* mask: */
                                                                  0xFFFFFFFFFFFFFFFF);
  ASSERT_TRUE(invalid_mask_result.ok());
  EXPECT_FALSE(invalid_mask_result->result.is_response());

  // Successful
  (*(mock_mmio_[0]))[0x0].ExpectRead(0x1234123412341234);
  auto read_result1 = device->GetClient(0)->ReadRegister64(/* address: */ 0x00000000,
                                                           /* mask: */ 0xFFFFFFFFFFFFFFFF);
  ASSERT_TRUE(read_result1.ok());
  ASSERT_TRUE(read_result1->result.is_response());
  EXPECT_EQ(read_result1->result.response().value, 0x1234123412341234);

  (*(mock_mmio_[2]))[0x8].ExpectRead(0x1234123412341234);
  auto read_result2 = device->GetClient(1)->ReadRegister64(/* address: */ 0x00000008,
                                                           /* mask: */ 0x00000000FFFF0000);
  ASSERT_TRUE(read_result2.ok());
  ASSERT_TRUE(read_result2->result.is_response());
  EXPECT_EQ(read_result2->result.response().value, 0x0000000012340000);
}

TEST_F(RegistersDeviceTest, Write64Test) {
  auto device = Init<uint64_t>(/* mmio_count: */ 2);
  ASSERT_NOT_NULL(device);

  device->AddRegister(
      /* mmio_index: */ 0,
      /* config: */
      BuildMetadata(allocator_, 0, 0x0,
                    std::vector<std::pair<uint64_t, uint32_t>>{{0xFFFFFFFFFFFFFFFF, 1}}));
  device->AddRegister(
      /* mmio_index: */ 1,
      /* config: */
      BuildMetadata(
          allocator_, 1, 0x0,
          std::vector<std::pair<uint64_t, uint32_t>>{
              {0xFFFFFFFFFFFFFFFF, 1}, {0x00000000FFFFFFFF, 1}, {0x0000FFFFFFFF0000, 1}}));

  // Invalid Call
  auto invalid_call_result = device->GetClient(0)->WriteRegister8(/* address: */
                                                                  0x0000000000000000,
                                                                  /* mask: */ 0xFF,
                                                                  /* value:  */ 0x12);
  ASSERT_TRUE(invalid_call_result.ok());
  EXPECT_FALSE(invalid_call_result->result.is_response());

  // Address not aligned
  auto unaligned_result = device->GetClient(0)->WriteRegister64(/* address: */
                                                                0x0000000000000001,
                                                                /* mask: */ 0xFFFFFFFFFFFFFFFF,
                                                                /* value: */ 0x4321432143214321);
  ASSERT_TRUE(unaligned_result.ok());
  EXPECT_FALSE(unaligned_result->result.is_response());

  // Address out of range
  auto out_of_range_result =
      device->GetClient(1)->WriteRegister64(/* address: */ 0x0000000000000020,
                                            /* mask: */ 0xFFFFFFFFFFFFFFFF,
                                            /* value: */ 0x4321432143214321);
  ASSERT_TRUE(out_of_range_result.ok());
  EXPECT_FALSE(out_of_range_result->result.is_response());

  // Invalid mask
  auto invalid_mask_result =
      device->GetClient(1)->WriteRegister64(/* address: */ 0x0000000000000008,
                                            /* mask: */ 0xFFFFFFFFFFFFFFFF,
                                            /* value: */ 0x4321432143214321);
  ASSERT_TRUE(invalid_mask_result.ok());
  EXPECT_FALSE(invalid_mask_result->result.is_response());

  // Successful
  (*(mock_mmio_[0]))[0x0].ExpectRead(0x0000000000000000).ExpectWrite(0x4321432143214321);
  auto read_result1 = device->GetClient(0)->WriteRegister64(
      /* address: */ 0x00000000, /* mask: */ 0xFFFFFFFFFFFFFFFF, /* value: */
      0x4321432143214321);
  ASSERT_TRUE(read_result1.ok());
  EXPECT_TRUE(read_result1->result.is_response());

  (*(mock_mmio_[1]))[0x8].ExpectRead(0x0000000000000000).ExpectWrite(0x0000000043210000);
  auto read_result2 = device->GetClient(1)->WriteRegister64(
      /* address: */ 0x00000008, /* mask: */ 0x00000000FFFF0000, /* value: */
      0x0000000043210000);
  ASSERT_TRUE(read_result2.ok());
  EXPECT_TRUE(read_result2->result.is_response());
}

}  // namespace registers
