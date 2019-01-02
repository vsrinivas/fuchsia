// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block_device.h"

#include <memory>
#include <utility>

#include <fbl/array.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/ftl/volume.h>
#include <unittest/unittest.h>

namespace {

constexpr uint32_t kPageSize = 1024;
constexpr uint32_t kNumPages = 100;

class FakeVolume : public ftl::Volume {
  public:
    explicit FakeVolume(ftl::BlockDevice* device) : device_(device) {}
    ~FakeVolume() final {}

    // Volume interface.
    const char* Init(std::unique_ptr<ftl::NdmDriver> driver) final {
        device_->OnVolumeAdded(kPageSize, kNumPages);
        return nullptr;
    }
    const char* ReAttach() final { return nullptr; }
    zx_status_t Read(uint32_t first_page, int num_pages, void* buffer) final { return ZX_OK; }
    zx_status_t Write(uint32_t first_page, int num_pages, const void* buffer) final {
        return ZX_OK; }
    zx_status_t Format() final { return ZX_OK; }
    zx_status_t Mount() final { return ZX_OK; }
    zx_status_t Unmount() final { return ZX_OK; }
    zx_status_t Flush() final { return ZX_OK; }
    zx_status_t Trim(uint32_t first_page, uint32_t num_pages) final { return ZX_OK; }
    zx_status_t GarbageCollect() final { return ZX_OK; }
    zx_status_t GetStats(Stats* stats) final  { return ZX_OK; }

  private:
    ftl::BlockDevice* device_;
};

bool TrivialLifetimeTest() {
    BEGIN_TEST;
    ftl::BlockDevice device;
    device.SetVolumeForTest(std::make_unique<FakeVolume>(&device));
    ASSERT_EQ(ZX_OK, device.Init());
    END_TEST;
}

bool DdkLifetimeTest() {
    BEGIN_TEST;
    ftl::BlockDevice* device(new ftl::BlockDevice(fake_ddk::kFakeParent));
    device->SetVolumeForTest(std::make_unique<FakeVolume>(device));

    fake_ddk::Bind ddk;
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
    protocols[0] = {ZX_PROTOCOL_NAND, {nullptr, nullptr}};
    ddk.SetProtocols(std::move(protocols));

    ASSERT_EQ(ZX_OK, device->Bind());
    device->DdkUnbind();
    EXPECT_TRUE(ddk.Ok());

    // This should delete the object, which means this test should not leak.
    device->DdkRelease();
    END_TEST;
}

bool GetSizeTest() {
    BEGIN_TEST;
    ftl::BlockDevice device;
    device.SetVolumeForTest(std::make_unique<FakeVolume>(&device));
    ASSERT_EQ(ZX_OK, device.Init());
    EXPECT_EQ(kPageSize * kNumPages, device.DdkGetSize());
    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(BlockDeviceTests)
RUN_TEST_SMALL(TrivialLifetimeTest)
RUN_TEST_SMALL(DdkLifetimeTest)
RUN_TEST_SMALL(GetSizeTest)
END_TEST_CASE(BlockDeviceTests)
