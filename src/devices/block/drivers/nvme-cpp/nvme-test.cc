// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/nvme-cpp/nvme.h"

#include <lib/fake-bti/bti.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/inspect/testing/cpp/zxtest/inspect.h>

#include <memory>

#include <zxtest/zxtest.h>

#include "fuchsia/hardware/block/cpp/banjo.h"
#include "src/devices/block/drivers/nvme-cpp/fake/admin-commands.h"
#include "src/devices/block/drivers/nvme-cpp/fake/fake-nvme-controller.h"
#include "src/devices/block/drivers/nvme-cpp/fake/fake-nvme-namespace.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace nvme {
namespace {
// Recursively unbind all devices, and shutdown the provided dispatcher before calling Release on
// the provided device.
zx_status_t ProcessDeviceRemoval(MockDevice* device, fdf::Dispatcher* dispatcher,
                                 sync_completion_t* dispatcher_shutdown) {
  device->UnbindOp();
  // deleting children, so use a while loop:
  while (!device->children().empty()) {
    // Only stop the dispatcher before calling the final ReleaseOp.
    auto status = ProcessDeviceRemoval(device->children().back().get(), nullptr, nullptr);
    if (status != ZX_OK) {
      return status;
    }
  }
  if (device->HasUnbindOp()) {
    zx_status_t status = device->WaitUntilUnbindReplyCalled();
    if (status != ZX_OK) {
      return status;
    }
  }

  if (dispatcher != nullptr) {
    dispatcher->ShutdownAsync();
    sync_completion_wait(dispatcher_shutdown, ZX_TIME_INFINITE);
  }
  device->ReleaseOp();
  return ZX_OK;
}
}  // namespace

class NvmeTest : public inspect::InspectTestHelper, public zxtest::Test {
 public:
  void SetUp() override {
    fake_root_ = MockDevice::FakeRootParent();

    // Set up dispatcher.
    auto dispatcher =
        fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS, "nvme-test",
                                [this](fdf_dispatcher_t*) { sync_completion_signal(&shutdown_); });
    ASSERT_OK(dispatcher.status_value());
    dispatcher_ = std::move(*dispatcher);

    // Create a fake BTI.
    zx::bti fake_bti;
    ASSERT_OK(fake_bti_create(fake_bti.reset_and_get_address()));

    // Set up an interrupt.
    auto irq = controller_.GetOrCreateInterrupt(0);
    ASSERT_OK(irq.status_value());

    // Set up the driver.
    auto driver =
        std::make_unique<Nvme>(fake_root_.get(), ddk::Pci(), controller_.registers().GetBuffer());
    driver->bti_ = std::move(fake_bti);
    driver->dispatcher_ = dispatcher_.borrow();
    driver->irq_ = std::move(*irq);
    ASSERT_OK(driver->Bind());
    __UNUSED auto unused = driver.release();

    device_ = fake_root_->GetLatestChild();
    nvme_ = device_->GetDeviceContext<Nvme>();
    controller_.SetNvme(nvme_);
  }

  void RunInit() {
    device_->InitOp();
    ASSERT_OK(device_->WaitUntilInitReplyCalled(zx::time::infinite()));
    ASSERT_OK(device_->InitReplyCallStatus());
  }

  void TearDown() override { ProcessDeviceRemoval(device_, &dispatcher_, &shutdown_); }

  void CheckStringPropertyPrefix(const inspect::NodeValue& node, std::string property,
                                 const char* expected) {
    const auto* actual = node.get_property<inspect::StringPropertyValue>(property);
    EXPECT_TRUE(actual);
    if (!actual) {
      return;
    }
    EXPECT_EQ(0, strncmp(actual->value().data(), expected, strlen(expected)));
  }

 protected:
  std::shared_ptr<zx_device> fake_root_;
  zx_device* device_;
  Nvme* nvme_;
  fake_nvme::FakeNvmeController controller_;
  fake_nvme::DefaultAdminCommands admin_commands_{controller_};
  fdf::Dispatcher dispatcher_;
  sync_completion_t shutdown_;
};

TEST_F(NvmeTest, BasicTest) {
  ASSERT_NO_FATAL_FAILURE(RunInit());
  ASSERT_NO_FATAL_FAILURE(ReadInspect(nvme_->inspect_vmo()));
  CheckStringPropertyPrefix(hierarchy().node(), "serial-no",
                            fake_nvme::DefaultAdminCommands::kSerialNumber);
}

TEST_F(NvmeTest, NamespaceBlockSize) {
  fake_nvme::FakeNvmeNamespace ns;
  controller_.AddNamespace(1, ns);
  ASSERT_NO_FATAL_FAILURE(RunInit());
  while (device_->child_count() == 0) {
    zx::nanosleep(zx::deadline_after(zx::msec(1)));
  }

  zx_device* ns_dev = device_->GetLatestChild();
  ns_dev->InitOp();
  ns_dev->WaitUntilInitReplyCalled(zx::time::infinite());
  ASSERT_OK(ns_dev->InitReplyCallStatus());

  ddk::BlockImplProtocolClient client(ns_dev);
  block_info_t info;
  uint64_t op_size;
  client.Query(&info, &op_size);
  ASSERT_EQ(512, info.block_size);
  ASSERT_EQ(1024, info.block_count);
}

}  // namespace nvme
