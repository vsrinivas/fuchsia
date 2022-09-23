// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/test/test_data_plane.h"

namespace wlan::nxpfmac {

TestDataPlane::~TestDataPlane() {
  // Deleting the data plane should result in a call to remove the netdevice device.
  data_plane_.reset();
  if (async_remove_watcher_) {
    // Wait for that remove to register and for the the watcher thread to call release and exit.
    async_remove_watcher_->join();
  }
}

zx_status_t TestDataPlane::Create(DataPlaneIfc* data_plane_ifc, BusInterface* bus_interface,
                                  void* mlan_adapter,
                                  std::unique_ptr<TestDataPlane>* out_data_plane) {
  std::unique_ptr<TestDataPlane> test_data_plane(new TestDataPlane());

  const zx_status_t status =
      DataPlane::Create(test_data_plane->parent_.get(), data_plane_ifc, bus_interface, mlan_adapter,
                        &test_data_plane->data_plane_);
  if (status != ZX_OK) {
    return status;
  }
  test_data_plane->net_device_ = test_data_plane->parent_->children().back();

  // The mock DDK doesn't call release on mock devices. Create a thread that waits for async
  // remove calls and manually triggers the release calls.
  test_data_plane->async_remove_watcher_ =
      std::make_unique<std::thread>([test_data_plane = test_data_plane.get()]() {
        // Make a copy of the shared pointer here so we're guaranteed the device is alive for the
        // duration of this thread.
        const std::shared_ptr<zx_device_t> net_device = test_data_plane->net_device_;

        if (net_device) {
          net_device->WaitUntilAsyncRemoveCalled();
          mock_ddk::ReleaseFlaggedDevices(net_device.get());
        }
      });

  *out_data_plane = std::move(test_data_plane);
  return ZX_OK;
}

}  // namespace wlan::nxpfmac
