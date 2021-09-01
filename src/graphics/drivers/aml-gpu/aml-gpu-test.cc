// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-gpu.h"

#include <fidl/fuchsia.hardware.gpu.amlogic/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/zx/vmo.h>

#include <zxtest/zxtest.h>

#include "s905d2-gpu.h"
#include "src/devices/registers/testing/mock-registers/mock-registers.h"

namespace aml_gpu {

class TestAmlGpu {
 public:
  static void TestSetClkFreq() {
    aml_gpu::AmlGpu aml_gpu(nullptr);
    aml_gpu.gpu_block_ = &s905d2_gpu_blocks;
    zx::vmo vmo;
    constexpr uint32_t kHiuRegisterSize = 1024 * 16;
    ASSERT_OK(zx::vmo::create(kHiuRegisterSize, 0, &vmo));
    ASSERT_OK(ddk::MmioBuffer::Create(0, kHiuRegisterSize, std::move(vmo), ZX_CACHE_POLICY_CACHED,
                                      &aml_gpu.hiu_buffer_));
    aml_gpu.SetClkFreqSource(1);
    uint32_t value = aml_gpu.hiu_buffer_->Read32(0x6c << 2);
    // Mux should be set to 1.
    EXPECT_EQ(1, value >> kFinalMuxBitShift);
    uint32_t parent_mux_value = (value >> 16) & 0xfff;
    uint32_t source = parent_mux_value >> 9;
    bool enabled = (parent_mux_value >> kClkEnabledBitShift) & 1;
    uint32_t divisor = (parent_mux_value & 0xff) + 1;
    EXPECT_EQ(S905D2_FCLK_DIV5, source);
    EXPECT_TRUE(enabled);
    EXPECT_EQ(1, divisor);
  }

  static void TestInitialClkFreq() {
    aml_gpu::AmlGpu aml_gpu(nullptr);
    aml_gpu.gpu_block_ = &s905d2_gpu_blocks;
    zx::vmo vmo;
    constexpr uint32_t kHiuRegisterSize = 1024 * 16;
    ASSERT_OK(zx::vmo::create(kHiuRegisterSize, 0, &vmo));
    ASSERT_OK(ddk::MmioBuffer::Create(0, kHiuRegisterSize, std::move(vmo), ZX_CACHE_POLICY_CACHED,
                                      &aml_gpu.hiu_buffer_));
    ASSERT_OK(zx::vmo::create(kHiuRegisterSize, 0, &vmo));
    ASSERT_OK(ddk::MmioBuffer::Create(0, kHiuRegisterSize, std::move(vmo), ZX_CACHE_POLICY_CACHED,
                                      &aml_gpu.gpu_buffer_));
    async::Loop loop{&kAsyncLoopConfigNeverAttachToThread};
    loop.StartThread();
    mock_registers::MockRegistersDevice reset_mock(loop.dispatcher());
    zx::channel client_end, server_end;
    ASSERT_OK(zx::channel::create(0, &client_end, &server_end));
    reset_mock.RegistersConnect(std::move(server_end));
    aml_gpu.reset_register_ =
        fidl::WireSyncClient<fuchsia_hardware_registers::Device>(std::move(client_end));
    reset_mock.fidl_service()->ExpectWrite<uint32_t>(aml_gpu.gpu_block_->reset0_mask_offset,
                                                     aml_registers::MALI_RESET0_MASK, 0);
    reset_mock.fidl_service()->ExpectWrite<uint32_t>(aml_gpu.gpu_block_->reset0_level_offset,
                                                     aml_registers::MALI_RESET0_MASK, 0);
    reset_mock.fidl_service()->ExpectWrite<uint32_t>(aml_gpu.gpu_block_->reset2_mask_offset,
                                                     aml_registers::MALI_RESET2_MASK, 0);
    reset_mock.fidl_service()->ExpectWrite<uint32_t>(aml_gpu.gpu_block_->reset2_level_offset,
                                                     aml_registers::MALI_RESET2_MASK, 0);
    reset_mock.fidl_service()->ExpectWrite<uint32_t>(aml_gpu.gpu_block_->reset0_level_offset,
                                                     aml_registers::MALI_RESET0_MASK,
                                                     aml_registers::MALI_RESET0_MASK);
    reset_mock.fidl_service()->ExpectWrite<uint32_t>(aml_gpu.gpu_block_->reset2_level_offset,
                                                     aml_registers::MALI_RESET2_MASK,
                                                     aml_registers::MALI_RESET2_MASK);
    aml_gpu.InitClock();
    uint32_t value = aml_gpu.hiu_buffer_->Read32(0x6c << 2);
    // Glitch-free mux should stay unchanged.
    EXPECT_EQ(0, value >> kFinalMuxBitShift);
    uint32_t parent_mux_value = value & 0xfff;
    uint32_t source = parent_mux_value >> 9;
    bool enabled = (parent_mux_value >> kClkEnabledBitShift) & 1;
    uint32_t divisor = (parent_mux_value & 0xff) + 1;
    // S905D2 starts at the highest frequency by default.
    EXPECT_EQ(S905D2_GP0, source);
    EXPECT_TRUE(enabled);
    EXPECT_EQ(1, divisor);
    EXPECT_OK(reset_mock.fidl_service()->VerifyAll());
  }

  static void TestMetadata() {
    using fuchsia_hardware_gpu_amlogic::wire::Metadata;
    aml_gpu::AmlGpu aml_gpu(nullptr);
    arm_mali_protocol_t protocol;
    EXPECT_OK(aml_gpu.DdkGetProtocol(ZX_PROTOCOL_ARM_MALI, &protocol));

    mali_properties_t properties;
    protocol.ops->get_properties(protocol.ctx, &properties);
    EXPECT_FALSE(properties.supports_protected_mode);

    {
      fidl::Arena allocator;
      Metadata metadata(allocator);
      metadata.set_supports_protected_mode(allocator, false);
      {
        fidl::OwnedEncodedMessage<Metadata> encoded_metadata(&metadata);
        ASSERT_TRUE(encoded_metadata.ok());
        auto message_bytes = encoded_metadata.GetOutgoingMessage().CopyBytes();
        EXPECT_OK(aml_gpu.ProcessMetadata(std::vector<uint8_t>(
            message_bytes.data(), message_bytes.data() + message_bytes.size())));
      }
    }

    protocol.ops->get_properties(protocol.ctx, &properties);
    EXPECT_FALSE(properties.supports_protected_mode);

    {
      fidl::Arena allocator;
      Metadata metadata(allocator);
      metadata.set_supports_protected_mode(allocator, true);
      {
        fidl::OwnedEncodedMessage<Metadata> encoded_metadata(&metadata);
        ASSERT_TRUE(encoded_metadata.ok());
        auto message_bytes = encoded_metadata.GetOutgoingMessage().CopyBytes();
        EXPECT_OK(aml_gpu.ProcessMetadata(std::vector<uint8_t>(
            message_bytes.data(), message_bytes.data() + message_bytes.size())));
      }
    }

    protocol.ops->get_properties(protocol.ctx, &properties);
    EXPECT_TRUE(properties.supports_protected_mode);
  }
};
}  // namespace aml_gpu

TEST(AmlGpu, SetClkFreq) { aml_gpu::TestAmlGpu::TestSetClkFreq(); }

TEST(AmlGpu, InitialClkFreq) { aml_gpu::TestAmlGpu::TestInitialClkFreq(); }

TEST(AmlGpu, Metadata) { aml_gpu::TestAmlGpu::TestMetadata(); }
