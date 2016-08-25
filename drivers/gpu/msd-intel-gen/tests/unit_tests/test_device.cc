// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/platform_device_helper.h"
#include "msd_intel_device.h"
#include "msd_intel_driver.h"
#include "gtest/gtest.h"

class TestMsdIntelDevice {
public:
    TestMsdIntelDevice() { driver_ = MsdIntelDriver::Create(); }

    ~TestMsdIntelDevice() { MsdIntelDriver::Destroy(driver_); }

    void CreateAndDestroy()
    {
        magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
        if (!platform_device) {
            printf("No platform device\n");
            return;
        }

        std::unique_ptr<MsdIntelDevice> device(
            driver_->CreateDevice(platform_device->GetDeviceHandle()));
        EXPECT_NE(device, nullptr);

        // test register read
        uint32_t value = device->register_io()->Read32(0x44038);
        EXPECT_EQ(0x1f2u, value);
    }

    void Dump()
    {
        magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
        if (!platform_device) {
            printf("No platform device\n");
            return;
        }

        std::unique_ptr<MsdIntelDevice> device(
            driver_->CreateDevice(platform_device->GetDeviceHandle()));
        EXPECT_NE(device, nullptr);

        MsdIntelDevice::DumpState dump_state;
        device->Dump(&dump_state);
        EXPECT_EQ(dump_state.render_cs.sequence_number,
                  device->global_context()
                      ->hardware_status_page(RENDER_COMMAND_STREAMER)
                      ->read_sequence_number());
        EXPECT_EQ(dump_state.render_cs.active_head_pointer, 0u);
        EXPECT_FALSE(dump_state.fault_present);

        uint32_t engine = 0;
        uint32_t src = 0xff;
        uint32_t type = 0x3;
        uint32_t valid = 0x1;
        device->DumpFault(&dump_state, (engine << 12) | (src << 3) | (type << 1) | valid);

        EXPECT_EQ(dump_state.fault_present, valid);
        EXPECT_EQ(dump_state.fault_engine, engine);
        EXPECT_EQ(dump_state.fault_src, src);
        EXPECT_EQ(dump_state.fault_type, type);

        std::string dump_string;
        device->DumpToString(dump_string);
        DLOG("%s", dump_string.c_str());
    }

private:
    MsdIntelDriver* driver_;
};

TEST(MsdIntelDevice, CreateAndDestroy)
{
    TestMsdIntelDevice test;
    test.CreateAndDestroy();
}

TEST(MsdIntelDevice, Dump)
{
    TestMsdIntelDevice test;
    test.Dump();
}
