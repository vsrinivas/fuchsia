// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/arraysize.h"
#include "mock/mock_mmio.h"
#include "power_manager.h"
#include "registers.h"
#include "gtest/gtest.h"

class TestPowerManager {
public:
    void MockEnable()
    {
        std::unique_ptr<RegisterIo> reg_io(new RegisterIo(MockMmio::Create(1024 * 1024)));
        auto power_manager = std::make_unique<PowerManager>();

        constexpr uint32_t kShaderOnOffset =
            static_cast<uint32_t>(registers::CoreReadyState::CoreType::kShader) +
            static_cast<uint32_t>(registers::CoreReadyState::ActionType::kActionPowerOn);
        constexpr uint32_t kShaderOnHighOffset = kShaderOnOffset + 4;
        constexpr uint32_t kDummyHighValue = 1500;
        reg_io->Write32(kShaderOnHighOffset, kDummyHighValue);
        power_manager->EnableCores(reg_io.get());
        // Higher word shouldn't be written to because none of them are being
        // enabled.
        EXPECT_EQ(kDummyHighValue, reg_io->Read32(kShaderOnHighOffset));

        registers::CoreReadyState::CoreType actions[] = {
            registers::CoreReadyState::CoreType::kShader, registers::CoreReadyState::CoreType::kL2,
            registers::CoreReadyState::CoreType::kTiler};
        for (size_t i = 0; i < arraysize(actions); i++) {

            uint32_t offset =
                static_cast<uint32_t>(actions[i]) +
                static_cast<uint32_t>(registers::CoreReadyState::ActionType::kActionPowerOn);
            EXPECT_EQ(1u, reg_io->Read32(offset));
        }
    }
};

TEST(PowerManager, MockEnable)
{
    TestPowerManager test;
    test.MockEnable();
}
