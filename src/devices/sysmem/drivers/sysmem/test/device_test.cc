// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/bti.h>
#include <stdlib.h>
#include <zircon/device/sysmem.h>

#include <ddktl/device.h>
#include <ddktl/protocol/platform/bus.h>
#include <ddktl/protocol/platform/device.h>
#include <zxtest/zxtest.h>

#include "../device.h"
#include "../driver.h"

namespace sysmem_driver {
namespace {
class FakePBus : public ddk::PBusProtocol<FakePBus, ddk::base_protocol> {
 public:
  FakePBus() : proto_({&pbus_protocol_ops_, this}) {}
  const pbus_protocol_t* proto() const { return &proto_; }
  zx_status_t PBusDeviceAdd(const pbus_dev_t* dev) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusRegisterProtocol(uint32_t proto_id, const void* protocol, size_t protocol_size) {
    return ZX_OK;
  }
  zx_status_t PBusGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PBusSetBootloaderInfo(const pbus_bootloader_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusCompositeDeviceAdd(const pbus_dev_t* dev,
                                     const device_fragment_t* fragments_list,
                                     size_t fragments_count, uint32_t coresident_device_index) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PBusRegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cbin) {
    return ZX_ERR_NOT_SUPPORTED;
  }

 private:
  pbus_protocol_t proto_;
};

class FakePDev : public ddk::PDevProtocol<FakePDev, ddk::base_protocol> {
 public:
  FakePDev() : proto_({&pdev_protocol_ops_, this}) {}

  const pdev_protocol_t* proto() const { return &proto_; }

  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti) {
    return fake_bti_create(out_bti->reset_and_get_address());
  }

  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

 private:
  pdev_protocol_t proto_;
};

TEST(Device, OverrideCommandLine) {
  const char* kCommandLine = "test.device.commandline";
  setenv(kCommandLine, "5", 1);

  uint64_t value = 4096;

  Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_EQ(65536, value);
  setenv(kCommandLine, "65537", 1);
  Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_EQ(65536 * 2, value);

  // Trailing characters should cause the entire value to be ignored.
  setenv(kCommandLine, "65536a", 1);
  Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_EQ(65536 * 2, value);

  // Empty values should be ignored.
  setenv(kCommandLine, "", 1);
  Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_EQ(65536 * 2, value);
}

class FakeDdkSysmem : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[2], 2);
    protocols[0] = {ZX_PROTOCOL_PBUS, *reinterpret_cast<const fake_ddk::Protocol*>(pbus_.proto())};
    protocols[1] = {ZX_PROTOCOL_PDEV, *reinterpret_cast<const fake_ddk::Protocol*>(pdev_.proto())};
    ddk_.SetProtocols(std::move(protocols));
    EXPECT_EQ(sysmem_.Bind(), ZX_OK);
  }

  void TearDown() override {
    sysmem_.DdkAsyncRemove();
    EXPECT_TRUE(ddk_.Ok());
  }

 protected:
  sysmem_driver::Driver sysmem_ctx_;
  sysmem_driver::Device sysmem_{fake_ddk::kFakeParent, &sysmem_ctx_};

  FakePBus pbus_;
  FakePDev pdev_;
  // ddk must be destroyed before sysmem because it may be executing messages against sysmem on
  // another thread.
  fake_ddk::Bind ddk_;
};

TEST_F(FakeDdkSysmem, TearDownLoop) {
  zx::channel allocator_server, allocator_client;
  ASSERT_OK(zx::channel::create(0u, &allocator_server, &allocator_client));
  EXPECT_OK(
      fuchsia_sysmem_DriverConnectorConnect(ddk_.FidlClient().get(), allocator_server.release()));

  zx::channel collection_server, collection_client;
  ASSERT_OK(zx::channel::create(0u, &collection_server, &collection_client));

  // Queue up something that would be processed on the FIDL thread, so we can try to detect a
  // use-after-free if the FidlServer outlives the sysmem device.
  EXPECT_OK(fuchsia_sysmem_AllocatorAllocateNonSharedCollection(allocator_client.get(),
                                                                collection_server.release()));
}

// Test that creating and tearing down a SecureMem connection works correctly.
TEST_F(FakeDdkSysmem, DummySecureMem) {
  zx::channel securemem_server, securemem_client;
  ASSERT_OK(zx::channel::create(0u, &securemem_server, &securemem_client));
  EXPECT_EQ(ZX_OK, sysmem_.SysmemRegisterSecureMem(std::move(securemem_server)));

  // This shouldn't deadlock waiting for a message on the channel.
  EXPECT_EQ(ZX_OK, sysmem_.SysmemUnregisterSecureMem());

  // This shouldn't cause a panic due to receiving peer closed.
  securemem_client.reset();
}

}  // namespace
}  // namespace sysmem_driver
