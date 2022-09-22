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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_TEST_DATA_PLANE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_TEST_DATA_PLANE_H_

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/data_plane.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace wlan::nxpfmac {

// A support class that helps manage the lifecycle of a DataPlane object. It does this by ensuring
// that the correct DDK lifecycles functions are called, ensuring that destruction of a DataPlane
// object works correctly in tests. This is needed because the mock device used in tests does not
// asynchronously call release when async_remove is called, instead the test is expected to do this.
//
// Since the DataPlane's destructor has to rely on the release call to safely destruct we need to
// emulate the behavior of the real DDK by calling release whenever async_remove is called.
class TestDataPlane {
 public:
  ~TestDataPlane();
  static zx_status_t Create(DataPlaneIfc* data_plane_ifc, BusInterface* bus_interface,
                            void* mlan_adapter, std::unique_ptr<TestDataPlane>* out_data_plane);

  DataPlane* GetDataPlane() { return data_plane_.get(); }
  zx_device_t* GetParent() { return parent_.get(); }
  zx_device_t* GetNetDevice() { return net_device_.load(); }

 private:
  TestDataPlane() = default;

  std::unique_ptr<std::thread> async_remove_watcher_;
  std::atomic<bool> async_remove_watcher_running_{true};
  std::atomic<zx_device*> net_device_ = nullptr;
  std::unique_ptr<DataPlane> data_plane_;
  std::shared_ptr<MockDevice> parent_{MockDevice::FakeRootParent()};
};

}  // namespace wlan::nxpfmac
#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_TEST_DATA_PLANE_H_
