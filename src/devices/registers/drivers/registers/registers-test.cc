// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "registers.h"

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <mock-mmio-reg/mock-mmio-reg.h>

#include "src/devices/lib/metadata/llcpp/registers.h"

namespace registers {

namespace {

constexpr size_t kRegSize = 0x00000100;

}  // namespace

template <typename T>
class FakeRegistersDevice : public RegistersDevice<T> {
 public:
  static std::unique_ptr<FakeRegistersDevice> Create(
      std::map<uint32_t, std::shared_ptr<MmioInfo>> mmios) {
    fbl::AllocChecker ac;
    auto device = fbl::make_unique_checked<FakeRegistersDevice>(&ac);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: device object alloc failed", __func__);
      return nullptr;
    }
    device->Init(std::move(mmios));

    return device;
  }

  void AddRegister(RegistersMetadataEntry config) {
    registers_.push_back(
        std::make_unique<Register<T>>(nullptr, RegistersDevice<T>::mmios_[config.mmio_id()]));
    registers_.back()->Init(config);

    zx::channel client_end, server_end;
    if (zx::channel::create(0, &client_end, &server_end) != ZX_OK) {
      return;
    }
    registers_.back()->RegistersConnect(std::move(server_end));
    clients_.emplace(config.bind_id(),
                     std::make_shared<fidl::WireSyncClient<fuchsia_hardware_registers::Device>>(
                         std::move(client_end)));
  }

  std::shared_ptr<fidl::WireSyncClient<fuchsia_hardware_registers::Device>> GetClient(uint64_t id) {
    return clients_[id];
  }

  explicit FakeRegistersDevice() : RegistersDevice<T>(nullptr) {}

 private:
  std::vector<std::unique_ptr<Register<T>>> registers_;
  std::map<uint64_t, std::shared_ptr<fidl::WireSyncClient<fuchsia_hardware_registers::Device>>>
      clients_;
};

class RegistersDeviceTest : public zxtest::Test {
 public:
  template <typename T>
  std::unique_ptr<FakeRegistersDevice<T>> Init(uint32_t mmio_count) {
    fbl::AllocChecker ac;

    std::map<uint32_t, std::shared_ptr<MmioInfo>> mmios;
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
      mmios.emplace(i, std::make_shared<MmioInfo>(MmioInfo{
                           .mmio = mock_mmio_[i]->GetMmioBuffer(),
                           .locks = std::move(locks),
                       }));
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

  std::map<uint64_t, fidl::WireSyncClient<fuchsia_hardware_registers::Device>> clients_;

  fidl::Arena<2048> allocator_;
};

TEST_F(RegistersDeviceTest, EncodeDecodeTest) {
  fidl::VectorView<MmioMetadataEntry> mmio(allocator_, 3);
  mmio[0] = registers::BuildMetadata(allocator_, 0);
  mmio[1] = registers::BuildMetadata(allocator_, 1);
  mmio[2] = registers::BuildMetadata(allocator_, 2);

  fidl::VectorView<RegistersMetadataEntry> registers(allocator_, 2);
  registers[0] = registers::BuildMetadata(allocator_, 0, 0,
                                          std::vector<MaskEntryBuilder<uint32_t>>{
                                              {.mask = 0xFFFF, .mmio_offset = 0x1, .reg_count = 3},
                                              {.mask = 0x8888, .mmio_offset = 0x2, .reg_count = 2},
                                          });
  registers[1] =
      registers::BuildMetadata(allocator_, 1, 1,
                               std::vector<MaskEntryBuilder<uint32_t>>{
                                   {.mask = 0x5555, .mmio_offset = 0x3, .reg_count = 1},
                                   {.mask = 0x77777777, .mmio_offset = 0x4, .reg_count = 2},
                                   {.mask = 0x1234, .mmio_offset = 0x5, .reg_count = 4},
                               });

  auto metadata_original =
      registers::BuildMetadata(allocator_, std::move(mmio), std::move(registers));
  fidl::OwnedEncodedMessage<Metadata> msg(&metadata_original);
  EXPECT_EQ(msg.GetOutgoingMessage().handle_actual(), 0);
  EXPECT_EQ(msg.GetOutgoingMessage().handles(), nullptr);

  auto converted = fidl::OutgoingToIncomingMessage(msg.GetOutgoingMessage());
  auto metadata = Metadata::DecodedMessage(fidl::internal::kLLCPPEncodedWireFormatVersion,
                                           std::move(converted.incoming_message()));
  ASSERT_TRUE(metadata.ok(), "%s", metadata.FormatDescription().c_str());
  ASSERT_EQ(metadata.PrimaryObject()->mmio().count(), 3);
  EXPECT_EQ(metadata.PrimaryObject()->mmio()[0].id(), 0);
  EXPECT_EQ(metadata.PrimaryObject()->mmio()[1].id(), 1);
  EXPECT_EQ(metadata.PrimaryObject()->mmio()[2].id(), 2);
  ASSERT_EQ(metadata.PrimaryObject()->registers().count(), 2);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].bind_id(), 0);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].mmio_id(), 0);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].masks()[0].mask().r32(), 0xFFFF);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].masks()[0].mmio_offset(), 0x1);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].masks()[0].count(), 3);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].masks()[1].mask().r32(), 0x8888);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].masks()[1].mmio_offset(), 0x2);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[0].masks()[1].count(), 2);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].bind_id(), 1);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].mmio_id(), 1);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[0].mask().r32(), 0x5555);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[0].mmio_offset(), 0x3);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[0].count(), 1);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[1].mask().r32(), 0x77777777);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[1].mmio_offset(), 0x4);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[1].count(), 2);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[2].mask().r32(), 0x1234);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[2].mmio_offset(), 0x5);
  EXPECT_EQ(metadata.PrimaryObject()->registers()[1].masks()[2].count(), 4);
}

TEST_F(RegistersDeviceTest, InvalidDecodeTest) {
  fidl::OwnedEncodedMessage<Metadata> msg(nullptr);
  auto converted = fidl::OutgoingToIncomingMessage(msg.GetOutgoingMessage());
  ASSERT_TRUE(converted.ok());
  auto metadata = Metadata::DecodedMessage(fidl::internal::kLLCPPEncodedWireFormatVersion,
                                           std::move(converted.incoming_message()));
  EXPECT_FALSE(metadata.ok());
}

TEST_F(RegistersDeviceTest, Read32Test) {
  auto device = Init<uint32_t>(/* mmio_count: */ 3);
  ASSERT_NOT_NULL(device);

  device->AddRegister(BuildMetadata(allocator_, 0, 0,
                                    std::vector<MaskEntryBuilder<uint32_t>>{
                                        {.mask = 0xFFFFFFFF, .mmio_offset = 0x0, .reg_count = 1}}));
  device->AddRegister(BuildMetadata(allocator_, 1, 2,
                                    std::vector<MaskEntryBuilder<uint32_t>>{
                                        {.mask = 0xFFFFFFFF, .mmio_offset = 0x0, .reg_count = 2},
                                        {.mask = 0xFFFF0000, .mmio_offset = 0x8, .reg_count = 1},
                                    }));

  // Invalid Call
  auto invalid_call_result =
      device->GetClient(0)->ReadRegister8(/* offset: */ 0x0, /* mask: */ 0xFF);
  ASSERT_TRUE(invalid_call_result.ok());
  EXPECT_FALSE(invalid_call_result->result.is_response());

  // Address not aligned
  auto unaligned_result =
      device->GetClient(0)->ReadRegister32(/* offset: */ 0x1, /* mask: */ 0xFFFFFFFF);
  EXPECT_TRUE(unaligned_result.ok());
  EXPECT_FALSE(unaligned_result->result.is_response());

  // Address out of range
  auto out_of_range_result =
      device->GetClient(1)->ReadRegister32(/* offset: */ 0xC, /* mask: */ 0xFFFFFFFF);
  ASSERT_TRUE(out_of_range_result.ok());
  EXPECT_FALSE(out_of_range_result->result.is_response());

  // Invalid mask
  auto invalid_mask_result =
      device->GetClient(1)->ReadRegister32(/* offset: */ 0x8, /* mask: */ 0xFFFFFFFF);
  EXPECT_TRUE(invalid_mask_result.ok());
  EXPECT_FALSE(invalid_mask_result->result.is_response());

  // Successful
  (*(mock_mmio_[0]))[0x0].ExpectRead(0x12341234);
  auto read_result1 =
      device->GetClient(0)->ReadRegister32(/* offset: */ 0x0, /* mask: */ 0xFFFFFFFF);
  ASSERT_TRUE(read_result1.ok());
  ASSERT_TRUE(read_result1->result.is_response());
  EXPECT_EQ(read_result1->result.response().value, 0x12341234);

  (*(mock_mmio_[2]))[0x4].ExpectRead(0x12341234);
  auto read_result2 =
      device->GetClient(1)->ReadRegister32(/* offset: */ 0x4, /* mask: */ 0xFFFF0000);
  EXPECT_TRUE(read_result2.ok());
  EXPECT_TRUE(read_result2->result.is_response());
  EXPECT_EQ(read_result2->result.response().value, 0x12340000);

  (*(mock_mmio_[2]))[0x8].ExpectRead(0x12341234);
  auto read_result3 =
      device->GetClient(1)->ReadRegister32(/* offset: */ 0x8, /* mask: */ 0xFFFF0000);
  EXPECT_TRUE(read_result3.ok());
  EXPECT_TRUE(read_result3->result.is_response());
  EXPECT_EQ(read_result3->result.response().value, 0x12340000);
}

TEST_F(RegistersDeviceTest, Write32Test) {
  auto device = Init<uint32_t>(/* mmio_count: */ 2);
  ASSERT_NOT_NULL(device);

  device->AddRegister(BuildMetadata(allocator_, 0, 0,
                                    std::vector<MaskEntryBuilder<uint32_t>>{
                                        {.mask = 0xFFFFFFFF, .mmio_offset = 0x0, .reg_count = 1}}));
  device->AddRegister(BuildMetadata(allocator_, 1, 1,
                                    std::vector<MaskEntryBuilder<uint32_t>>{
                                        {.mask = 0xFFFFFFFF, .mmio_offset = 0x0, .reg_count = 2},
                                        {.mask = 0xFFFF0000, .mmio_offset = 0x8, .reg_count = 1},
                                    }));

  // Invalid Call
  auto invalid_call_result =
      device->GetClient(0)->WriteRegister8(/* offset: */ 0x0, /* mask: */ 0xFF, /* value:  */ 0x12);
  ASSERT_TRUE(invalid_call_result.ok());
  EXPECT_FALSE(invalid_call_result->result.is_response());

  // Address not aligned
  auto unaligned_result = device->GetClient(0)->WriteRegister32(
      /* offset: */ 0x1, /* mask: */ 0xFFFFFFFF, /* value: */ 0x43214321);
  ASSERT_TRUE(unaligned_result.ok());
  EXPECT_FALSE(unaligned_result->result.is_response());

  // Address out of range
  auto out_of_range_result = device->GetClient(1)->WriteRegister32(
      /* offset: */ 0xC, /* mask: */ 0xFFFFFFFF, /* value: */ 0x43214321);
  EXPECT_TRUE(out_of_range_result.ok());
  EXPECT_FALSE(out_of_range_result->result.is_response());

  // Invalid mask
  auto invalid_mask_result = device->GetClient(1)->WriteRegister32(
      /* offset: */ 0x8, /* mask: */ 0xFFFFFFFF, /* value: */ 0x43214321);
  EXPECT_TRUE(invalid_mask_result.ok());
  EXPECT_FALSE(invalid_mask_result->result.is_response());

  // Successful
  (*(mock_mmio_[0]))[0x0].ExpectRead(0x00000000).ExpectWrite(0x43214321);
  auto read_result1 = device->GetClient(0)->WriteRegister32(
      /* offset: */ 0x0, /* mask: */ 0xFFFFFFFF, /* value: */ 0x43214321);
  EXPECT_TRUE(read_result1.ok());
  EXPECT_TRUE(read_result1->result.is_response());

  (*(mock_mmio_[1]))[0x4].ExpectRead(0x00000000).ExpectWrite(0x43210000);
  auto read_result2 = device->GetClient(1)->WriteRegister32(
      /* offset: */ 0x4, /* mask: */ 0xFFFF0000, /* value: */ 0x43214321);
  EXPECT_TRUE(read_result2.ok());
  EXPECT_TRUE(read_result2->result.is_response());

  (*(mock_mmio_[1]))[0x8].ExpectRead(0x00000000).ExpectWrite(0x43210000);
  auto read_result3 = device->GetClient(1)->WriteRegister32(
      /* offset: */ 0x8, /* mask: */ 0xFFFF0000, /* value: */ 0x43214321);
  EXPECT_TRUE(read_result3.ok());
  EXPECT_TRUE(read_result3->result.is_response());
}

TEST_F(RegistersDeviceTest, Read64Test) {
  auto device = Init<uint64_t>(/* mmio_count: */ 3);
  ASSERT_NOT_NULL(device);

  device->AddRegister(
      BuildMetadata(allocator_, 0, 0,
                    std::vector<MaskEntryBuilder<uint64_t>>{
                        {.mask = 0xFFFFFFFFFFFFFFFF, .mmio_offset = 0x0, .reg_count = 1}}));
  device->AddRegister(
      BuildMetadata(allocator_, 1, 2,
                    std::vector<MaskEntryBuilder<uint64_t>>{
                        {.mask = 0xFFFFFFFFFFFFFFFF, .mmio_offset = 0x0, .reg_count = 1},
                        {.mask = 0x00000000FFFFFFFF, .mmio_offset = 0x8, .reg_count = 1},
                        {.mask = 0x0000FFFFFFFF0000, .mmio_offset = 0x10, .reg_count = 1},
                    }));

  // Invalid Call
  auto invalid_call_result =
      device->GetClient(0)->ReadRegister8(/* offset: */ 0x0, /* mask: */ 0xFF);
  ASSERT_TRUE(invalid_call_result.ok());
  EXPECT_FALSE(invalid_call_result->result.is_response());

  // Address not aligned
  auto unaligned_result =
      device->GetClient(0)->ReadRegister64(/* offset: */ 0x1, /* mask: */ 0xFFFFFFFFFFFFFFFF);
  ASSERT_TRUE(unaligned_result.ok());
  EXPECT_FALSE(unaligned_result->result.is_response());

  // Address out of range
  auto out_of_range_result =
      device->GetClient(1)->ReadRegister64(/* offset: */ 0x20, /* mask: */ 0xFFFFFFFFFFFFFFFF);
  ASSERT_TRUE(out_of_range_result.ok());
  EXPECT_FALSE(out_of_range_result->result.is_response());

  // Invalid mask
  auto invalid_mask_result =
      device->GetClient(1)->ReadRegister64(/* offset: */ 0x8, /* mask: */ 0xFFFFFFFFFFFFFFFF);
  ASSERT_TRUE(invalid_mask_result.ok());
  EXPECT_FALSE(invalid_mask_result->result.is_response());

  // Successful
  (*(mock_mmio_[0]))[0x0].ExpectRead(0x1234123412341234);
  auto read_result1 =
      device->GetClient(0)->ReadRegister64(/* offset: */ 0x0, /* mask: */ 0xFFFFFFFFFFFFFFFF);
  ASSERT_TRUE(read_result1.ok());
  ASSERT_TRUE(read_result1->result.is_response());
  EXPECT_EQ(read_result1->result.response().value, 0x1234123412341234);

  (*(mock_mmio_[2]))[0x8].ExpectRead(0x1234123412341234);
  auto read_result2 =
      device->GetClient(1)->ReadRegister64(/* offset: */ 0x8, /* mask: */ 0x00000000FFFF0000);
  ASSERT_TRUE(read_result2.ok());
  ASSERT_TRUE(read_result2->result.is_response());
  EXPECT_EQ(read_result2->result.response().value, 0x0000000012340000);
}

TEST_F(RegistersDeviceTest, Write64Test) {
  auto device = Init<uint64_t>(/* mmio_count: */ 2);
  ASSERT_NOT_NULL(device);

  device->AddRegister(
      BuildMetadata(allocator_, 0, 0,
                    std::vector<MaskEntryBuilder<uint64_t>>{
                        {.mask = 0xFFFFFFFFFFFFFFFF, .mmio_offset = 0x0, .reg_count = 1}}));
  device->AddRegister(
      BuildMetadata(allocator_, 1, 1,
                    std::vector<MaskEntryBuilder<uint64_t>>{
                        {.mask = 0xFFFFFFFFFFFFFFFF, .mmio_offset = 0x0, .reg_count = 1},
                        {.mask = 0x00000000FFFFFFFF, .mmio_offset = 0x8, .reg_count = 1},
                        {.mask = 0x0000FFFFFFFF0000, .mmio_offset = 0x10, .reg_count = 1},
                    }));

  // Invalid Call
  auto invalid_call_result =
      device->GetClient(0)->WriteRegister8(/* offset: */ 0x0, /* mask: */ 0xFF, /* value:  */ 0x12);
  ASSERT_TRUE(invalid_call_result.ok());
  EXPECT_FALSE(invalid_call_result->result.is_response());

  // Address not aligned
  auto unaligned_result = device->GetClient(0)->WriteRegister64(
      /* offset: */ 0x1, /* mask: */ 0xFFFFFFFFFFFFFFFF, /* value: */ 0x4321432143214321);
  ASSERT_TRUE(unaligned_result.ok());
  EXPECT_FALSE(unaligned_result->result.is_response());

  // Address out of range
  auto out_of_range_result = device->GetClient(1)->WriteRegister64(
      /* offset: */ 0x20, /* mask: */ 0xFFFFFFFFFFFFFFFF, /* value: */ 0x4321432143214321);
  ASSERT_TRUE(out_of_range_result.ok());
  EXPECT_FALSE(out_of_range_result->result.is_response());

  // Invalid mask
  auto invalid_mask_result = device->GetClient(1)->WriteRegister64(/* offset: */ 0x8,
                                                                   /* mask: */ 0xFFFFFFFFFFFFFFFF,
                                                                   /* value: */ 0x4321432143214321);
  ASSERT_TRUE(invalid_mask_result.ok());
  EXPECT_FALSE(invalid_mask_result->result.is_response());

  // Successful
  (*(mock_mmio_[0]))[0x0].ExpectRead(0x0000000000000000).ExpectWrite(0x4321432143214321);
  auto read_result1 = device->GetClient(0)->WriteRegister64(
      /* offset: */ 0x0, /* mask: */ 0xFFFFFFFFFFFFFFFF, /* value: */
      0x4321432143214321);
  ASSERT_TRUE(read_result1.ok());
  EXPECT_TRUE(read_result1->result.is_response());

  (*(mock_mmio_[1]))[0x8].ExpectRead(0x0000000000000000).ExpectWrite(0x0000000043210000);
  auto read_result2 = device->GetClient(1)->WriteRegister64(
      /* offset: */ 0x8, /* mask: */ 0x00000000FFFF0000, /* value: */
      0x0000000043210000);
  ASSERT_TRUE(read_result2.ok());
  EXPECT_TRUE(read_result2->result.is_response());
}

}  // namespace registers
