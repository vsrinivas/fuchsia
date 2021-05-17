// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <memory>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "integration-test.h"

namespace libdriver_integration_test {

class DeviceAddTest : public IntegrationTest {
 protected:
  Promise<void> CreateDevice(std::initializer_list<zx_device_prop_t> props,
                             zx_status_t expected_status,
                             std::unique_ptr<RootMockDevice>* root_device,
                             std::unique_ptr<MockDevice>* child_device) {
    // Copy from props into a vector owned by the lambda, since a capture "by
    // value" of props does not copy deeply.
    std::vector<zx_device_prop_t> properties(props);
    return ExpectBind(root_device, [=, properties = std::move(properties)](
                                       HookInvocation record, Completer<void> completer) {
      ActionList actions;
      actions.AppendAddMockDevice(loop_.dispatcher(), (*root_device)->path(), "first_child",
                                  std::move(properties), expected_status, std::move(completer),
                                  child_device);
      actions.AppendReturnStatus(expected_status);
      return actions;
    });
  }
};

TEST_F(DeviceAddTest, CreateDevice) {
  std::unique_ptr<RootMockDevice> root_device;
  std::unique_ptr<MockDevice> child_device;

  auto promise = CreateDevice(
                     {
                         (zx_device_prop_t){BIND_PCI_VID, 0, 1234},
                     },
                     ZX_OK, &root_device, &child_device)
                     .and_then([&]() -> Promise<void> {
                       // Destroy the test device.  This should cause an unbind of the child
                       // device.
                       root_device.reset();
                       return ExpectUnbindThenRelease(child_device);
                     });

  RunPromise(std::move(promise));
}

}  // namespace libdriver_integration_test
