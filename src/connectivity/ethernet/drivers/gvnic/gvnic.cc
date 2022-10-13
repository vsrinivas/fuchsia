// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/ethernet/drivers/gvnic/gvnic.h"

#include "src/connectivity/ethernet/drivers/gvnic/gvnic-bind.h"

#define SET_REG(f, v)                                                               \
  do {                                                                              \
    regs_.f = (v);                                                                  \
    reg_mmio_->Write<decltype(regs_.f)::wrapped_type>(regs_.f.GetBE(),              \
                                                      offsetof(GvnicRegisters, f)); \
  } while (0)
#define GET_REG(f) \
  (regs_.f.SetBE(reg_mmio_->Read<decltype(regs_.f)::wrapped_type>(offsetof(GvnicRegisters, f))))

#define CACHELINE_SIZE (64)

#define GVNIC_MSG(s, m, ...) zxlogf(s, "%s: " m, __FUNCTION__, ##__VA_ARGS__)

namespace gvnic {

zx_status_t Gvnic::Bind(void* ctx, zx_device_t* dev) {
  zx_status_t status;

  auto driver = std::make_unique<Gvnic>(dev);
  status = driver->Bind();
  if (status != ZX_OK) {
    GVNIC_MSG(ERROR, "Bind(): FAILED (%s)", zx_status_get_string(status));
    return status;
  }

  // The DriverFramework now owns driver.
  __UNUSED auto ptr = driver.release();

  GVNIC_MSG(INFO, "Succeess.");

  return ZX_OK;
}

zx_status_t Gvnic::Bind() {
  zx_status_t status;

  status = SetUpPci();
  if (status != ZX_OK) {
    GVNIC_MSG(ERROR, "SetUpPci(): FAILED (%s)", zx_status_get_string(status));
    return status;
  }
  status = MapBars();
  if (status != ZX_OK) {
    GVNIC_MSG(ERROR, "MapBars(): FAILED (%s)", zx_status_get_string(status));
    return status;
  }
  status = DdkAdd(ddk::DeviceAddArgs("gvnic").set_inspect_vmo(inspect_.DuplicateVmo()));
  if (status != ZX_OK) {
    GVNIC_MSG(ERROR, "DdkAdd(): FAILED (%s)", zx_status_get_string(status));
    return status;
  }
  is_bound.Set(true);

  return ZX_OK;
}

zx_status_t Gvnic::SetUpPci() {
  zx_status_t status;

  pci_ = ddk::Pci::FromFragment(parent());
  if (!pci_.is_valid()) {
    GVNIC_MSG(ERROR, "pci_.is_valid(): FAILED");
    return ZX_ERR_INTERNAL;
  }
  status = pci_.GetBti(0, &bti_);
  if (status != ZX_OK) {
    GVNIC_MSG(ERROR, "pci_.GetBti(): FAILED (%s)", zx_status_get_string(status));
    return status;
  }
  status = pci_.SetBusMastering(true);
  if (status != ZX_OK) {
    GVNIC_MSG(ERROR, "pci_.SetBusMastering(): FAILED (%s)", zx_status_get_string(status));
    return status;
  }
  status = pci_.SetInterruptMode(fuchsia_hardware_pci::InterruptMode::kMsiX, 1);
  if (status != ZX_OK) {
    GVNIC_MSG(ERROR, "pci_.SetInterruptMode(): FAILED (%s)", zx_status_get_string(status));
    return status;
  }
  buffer_factory_ = dma_buffer::CreateBufferFactory();
  return ZX_OK;
}

zx_status_t Gvnic::MapBars() {
  zx_status_t status;

  status = pci_.MapMmio(GVNIC_REGISTER_BAR, ZX_CACHE_POLICY_UNCACHED_DEVICE, &reg_mmio_);
  if (status != ZX_OK) {
    GVNIC_MSG(ERROR, "pci_.MapMmio() reg_mmio_: FAILED (%s)", zx_status_get_string(status));
    return status;
  }
  status = pci_.MapMmio(GVNIC_DOORBELL_BAR, ZX_CACHE_POLICY_UNCACHED_DEVICE, &doorbell_mmio_);
  if (status != ZX_OK) {
    GVNIC_MSG(ERROR, "pci_.MapMmio() doorbell_mmio_: FAILED (%s)", zx_status_get_string(status));
    return status;
  }
  // Initialize the local copies of regs_.
  memset(&regs_, 0, sizeof(regs_));
  GET_REG(dev_status);
  GET_REG(max_tx_queues);
  GET_REG(max_rx_queues);
  GET_REG(admin_queue_counter);
  GET_REG(dma_mask);
  return ZX_OK;
}

void Gvnic::DdkInit(ddk::InitTxn txn) { txn.Reply(ZX_OK); }

void Gvnic::DdkRelease() { delete this; }

static zx_driver_ops_t gvnic_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Gvnic::Bind;
  return ops;
}();

}  // namespace gvnic

ZIRCON_DRIVER(Gvnic, gvnic::gvnic_driver_ops, "zircon", GVNIC_VERSION);
