// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_TESTING_FAKE_PDEV_FAKE_PDEV_H_
#define SRC_DEVICES_BUS_TESTING_FAKE_PDEV_FAKE_PDEV_H_

#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <atomic>
#include <map>
#include <optional>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

namespace fake_pdev {

// This class is thread-safe.
class FakePDev : public ddk::PDevProtocol<FakePDev> {
 public:
  FakePDev() : proto_({&pdev_protocol_ops_, this}) {}

  const pdev_protocol_t* proto() const { return &proto_; }

  fake_ddk::FragmentEntry fragment() const;

  struct MmioInfo {
    zx::vmo vmo;
    zx_off_t offset;
    size_t size;
  };

  void set_mmio(uint32_t idx, MmioInfo mmio) {
    fbl::AutoLock al(&lock_);
    mmios_[idx] = std::move(mmio);
  }
  void set_bti(uint32_t idx, zx::bti bti) {
    fbl::AutoLock al(&lock_);
    btis_[idx] = std::move(bti);
  }
  void set_interrupt(uint32_t idx, zx::interrupt irq) {
    fbl::AutoLock al(&lock_);
    irqs_[idx] = std::move(irq);
  }
  void set_smc(uint32_t idx, zx::resource smc) {
    fbl::AutoLock al(&lock_);
    smcs_[idx] = std::move(smc);
  }

  // Creates a virtual interrupt and returns an unowned copy to it.
  zx::unowned_interrupt CreateVirtualInterrupt(uint32_t idx);

  // Generates a fake bti lazily if true.
  void UseFakeBti(bool use_fake_bti = true) { use_fake_bti_ = use_fake_bti; }
  // Generates a fake smc resource lazily if true.
  void UseFakeSmc(bool use_fake_smc = true) { use_fake_smc_ = use_fake_smc; }

  void set_device_info(std::optional<pdev_device_info_t> info) {
    fbl::AutoLock al(&lock_);
    device_info_ = info;
  }
  void set_board_info(std::optional<pdev_board_info_t> info) {
    fbl::AutoLock al(&lock_);
    board_info_ = info;
  }

  // PDev protocol implementation.
  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio);
  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti);
  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource);
  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info);
  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info);

 private:
  pdev_protocol_t proto_;

  fbl::Mutex lock_;

  std::map<uint32_t, MmioInfo> mmios_ __TA_GUARDED(lock_);
  std::map<uint32_t, zx::interrupt> irqs_ __TA_GUARDED(lock_);
  std::map<uint32_t, zx::bti> btis_ __TA_GUARDED(lock_);
  std::map<uint32_t, zx::resource> smcs_ __TA_GUARDED(lock_);

  std::atomic<bool> use_fake_bti_ = false;
  std::atomic<bool> use_fake_smc_ = false;

  std::optional<pdev_device_info_t> device_info_ __TA_GUARDED(lock_);
  std::optional<pdev_board_info_t> board_info_ __TA_GUARDED(lock_);
};

}  // namespace fake_pdev

#endif  // SRC_DEVICES_BUS_TESTING_FAKE_PDEV_FAKE_PDEV_H_
