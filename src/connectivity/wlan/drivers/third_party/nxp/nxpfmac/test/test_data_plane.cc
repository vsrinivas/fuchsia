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
  data_plane_.reset();
  if (async_remove_watcher_) {
    async_remove_watcher_running_ = false;
    // Pretend that an sync remove happened to wake up the async remove watcher thread.
    if (net_device_) {
      net_device_.load()->RecordAsyncRemove(ZX_OK);
    }
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
  test_data_plane->net_device_.store(test_data_plane->parent_->GetLatestChild());

  // The mock DDK doesn't call release on mock devices. Create a thread that waits for async
  // remove calls and manually triggers the release calls.
  test_data_plane->async_remove_watcher_ =
      std::make_unique<std::thread>([test_data_plane = test_data_plane.get()]() {
        while (test_data_plane->net_device_.load() &&
               test_data_plane->async_remove_watcher_running_.load()) {
          test_data_plane->net_device_.load()->WaitUntilAsyncRemoveCalled();
          if (mock_ddk::ReleaseFlaggedDevices(test_data_plane->net_device_) == ZX_OK) {
            test_data_plane->net_device_.store(nullptr);
          }
        }
      });

  *out_data_plane = std::move(test_data_plane);
  return ZX_OK;
}

}  // namespace wlan::nxpfmac
