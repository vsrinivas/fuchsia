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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_MOCK_BUS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_MOCK_BUS_H_

#include <functional>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/bus_interface.h"

namespace wlan::nxpfmac {

constexpr uint32_t kMockBusBufferAlignment = 32;

// A mock Device implementation for unit test purposes. Very little functionality is currently
// supported. Add more as needed.
class MockBus : public BusInterface {
 public:
  // BusInterface implementation
  zx_status_t OnMlanRegistered(void* mlan_adapter) override { return ZX_OK; }
  zx_status_t OnFirmwareInitialized() override { return ZX_OK; }
  zx_status_t TriggerMainProcess() override {
    if (trigger_main_process_) {
      return trigger_main_process_();
    }
    return ZX_OK;
  }

  zx_status_t PrepareVmo(uint8_t vmo_id, zx::vmo&& vmo, uint8_t* mapped_address,
                         size_t mapped_size) override {
    if (prepare_vmo_) {
      return prepare_vmo_(vmo_id, std::move(vmo), mapped_address, mapped_size);
    }
    return ZX_OK;
  }
  zx_status_t ReleaseVmo(uint8_t vmo_id) override {
    if (release_vmo_) {
      release_vmo_(vmo_id);
    }
    return ZX_OK;
  }
  uint16_t GetRxHeadroom() const override { return 0; }
  uint16_t GetTxHeadroom() const override { return 0; }
  uint32_t GetBufferAlignment() const override { return kMockBusBufferAlignment; }

  // Implementation mocks
  void SetTriggerMainProcess(std::function<zx_status_t(void)>&& trigger_main_process) {
    trigger_main_process_ = std::move(trigger_main_process);
  }
  void SetPrepareVmo(std::function<zx_status_t(uint8_t, zx::vmo, uint8_t*, size_t)>&& prepare_vmo) {
    prepare_vmo_ = std::move(prepare_vmo);
  }
  void SetReleaseVmo(std::function<zx_status_t(uint8_t)>&& release_vmo) {
    release_vmo_ = release_vmo;
  }

 private:
  std::function<zx_status_t(void)> trigger_main_process_;
  std::function<zx_status_t(uint8_t, zx::vmo, uint8_t*, size_t)> prepare_vmo_;
  std::function<zx_status_t(uint8_t)> release_vmo_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_MOCK_BUS_H_
