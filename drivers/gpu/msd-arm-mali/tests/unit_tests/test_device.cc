// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "mock/mock_mmio.h"
#include "msd_arm_device.h"
#include "registers.h"
#include "gtest/gtest.h"

// These tests are unit testing the functionality of MsdArmDevice.
// All of these tests instantiate the device in test mode, that is without the device thread active.
class TestMsdArmDevice {
public:
    void CreateAndDestroy()
    {
        std::unique_ptr<MsdArmDevice> device = MsdArmDevice::Create(GetTestDeviceHandle(), false);
        EXPECT_NE(device, nullptr);
    }

    void Dump()
    {
        std::unique_ptr<MsdArmDevice> device = MsdArmDevice::Create(GetTestDeviceHandle(), false);
        EXPECT_NE(device, nullptr);

        MsdArmDevice::DumpState dump_state;
        device->Dump(&dump_state);
        ASSERT_EQ(12u, dump_state.power_states.size());
        EXPECT_EQ(std::string("L2 Cache"), dump_state.power_states[0].core_type);
        EXPECT_EQ(std::string("Present"), dump_state.power_states[0].status_type);
        EXPECT_EQ(1lu, dump_state.power_states[0].bitmask);

        std::string dump_string;
        device->FormatDump(dump_state, dump_string);
        EXPECT_NE(nullptr,
                  strstr(dump_string.c_str(), "Core type L2 Cache state Present bitmap: 0x1"));
    }

    void MockDump()
    {
        std::unique_ptr<RegisterIo> reg_io(new RegisterIo(MockMmio::Create(1024 * 1024)));

        uint32_t offset = static_cast<uint32_t>(registers::CoreReadyState::CoreType::kShader) +
                          static_cast<uint32_t>(registers::CoreReadyState::StatusType::kReady);
        reg_io->Write32(offset, 2);
        reg_io->Write32(offset + 4, 5);

        MsdArmDevice::DumpState dump_state;
        MsdArmDevice::DumpRegisters(reg_io.get(), &dump_state);
        bool found = false;
        for (auto& pstate : dump_state.power_states) {
            if (std::string("Shader") == pstate.core_type &&
                std::string("Ready") == pstate.status_type) {
                EXPECT_EQ(0x500000002ul, pstate.bitmask);
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }

    void ProcessRequest()
    {
        std::unique_ptr<MsdArmDevice> device(MsdArmDevice::Create(GetTestDeviceHandle(), false));
        ASSERT_NE(device, nullptr);

        class TestRequest : public DeviceRequest {
        public:
            TestRequest(std::shared_ptr<bool> processing_complete)
                : processing_complete_(processing_complete)
            {
            }

        protected:
            magma::Status Process(MsdArmDevice* device) override
            {
                *processing_complete_ = true;
                return MAGMA_STATUS_OK;
            }

        private:
            std::shared_ptr<bool> processing_complete_;
        };

        auto processing_complete = std::make_shared<bool>(false);

        auto request = std::make_unique<TestRequest>(processing_complete);
        request->ProcessAndReply(device.get());

        EXPECT_TRUE(processing_complete);
    }
};

TEST(MsdArmDevice, CreateAndDestroy)
{
    TestMsdArmDevice test;
    test.CreateAndDestroy();
}

TEST(MsdArmDevice, Dump)
{
    TestMsdArmDevice test;
    test.Dump();
}

TEST(MsdArmDevice, MockDump)
{
    TestMsdArmDevice test;
    test.MockDump();
}

TEST(MsdArmDevice, ProcessRequest)
{
    TestMsdArmDevice test;
    test.ProcessRequest();
}
