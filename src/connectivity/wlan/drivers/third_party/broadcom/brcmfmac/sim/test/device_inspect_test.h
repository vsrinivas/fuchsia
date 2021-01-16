/*
 * Copyright (c) 2020 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_TEST_DEVICE_INSPECT_TEST_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_TEST_DEVICE_INSPECT_TEST_H_

#include <lib/async/cpp/executor.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/inspect/testing/cpp/inspect.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/sim_test.h"

namespace wlan {
namespace brcmfmac {

typedef fit::result<inspect::Hierarchy> Hierarchy;

class DeviceInspectTestHelper : public gtest::RealLoopFixture {
  void SetUp() {
    env_ = std::make_shared<simulation::Environment>();
    dev_mgr_ = std::make_unique<simulation::FakeDevMgr>();
    ASSERT_EQ(ZX_OK, SimDevice::Create(dev_mgr_->GetRootDevice(), dev_mgr_.get(), env_, &device_));
    ASSERT_EQ(ZX_OK, device_->Init());

    // By default, inspect timer is disabled in sim-test env.
    // Starting it for Inspect testing.
    device_->inspect_.StartTimers();
  }

  // Run a promise to completion on the default async executor.
  void RunPromiseToCompletion(fit::promise<> promise) {
    bool done = false;
    executor_.schedule_task(std::move(promise).and_then([&]() { done = true; }));
    RunLoopUntilIdle();
    ASSERT_TRUE(done);
  }

  async::Executor executor_;
  std::unique_ptr<simulation::FakeDevMgr> dev_mgr_;

 public:
  DeviceInspectTestHelper() : executor_(dispatcher()) { SetUp(); };

  void FetchHierarchy() {
    RunPromiseToCompletion(
        inspect::ReadFromInspector(device_->get_inspector()).then([&](Hierarchy& result) {
          hierarchy_ = std::move(result);
        }));
  }

  std::shared_ptr<simulation::Environment> env_;

 protected:
  SimDevice* device_;
  Hierarchy hierarchy_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_TEST_DEVICE_INSPECT_TEST_H_
