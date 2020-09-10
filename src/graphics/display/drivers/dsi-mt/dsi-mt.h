// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DSI_MT_DSI_MT_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DSI_MT_DSI_MT_H_

#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/mipi-dsi/mipi-dsi.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <optional>

#include <ddk/driver.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/dsiimpl.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "mt-dsi-reg.h"

#define DSI_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DSI_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DSI_SPEW(fmt, ...) zxlogf(TRACE, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DSI_TRACE zxlogf(INFO, "[%s %d]", __func__, __LINE__)

namespace dsi_mt {

class DsiMt;

using DeviceType = ddk::Device<DsiMt, ddk::Unbindable>;

class DsiMt : public DeviceType, public ddk::DsiImplProtocol<DsiMt, ddk::base_protocol> {
 public:
  DsiMt(zx_device_t* parent) : DeviceType(parent), pdev_(parent) {}

  // This function is called from the c-bind function upon driver matching
  zx_status_t Bind();

  // Part of ZX_DSIIMPL_PROTOCOL
  zx_status_t DsiImplConfig(const dsi_config_t* dsi_config);
  void DsiImplPowerUp();
  void DsiImplPowerDown();
  void DsiImplSetMode(dsi_mode_t mode);
  zx_status_t DsiImplSendCmd(const mipi_dsi_cmd_t* cmd_list, size_t cmd_count);
  bool DsiImplIsPoweredUp();
  void DsiImplReset();
  zx_status_t DsiImplPhyConfig(const dsi_config_t* dsi_config) { return ZX_OK; }
  void DsiImplPhyPowerUp();
  void DsiImplPhyPowerDown() {}
  void DsiImplPhySendCode(uint32_t code, uint32_t parameter) {}
  zx_status_t DsiImplPhyWaitForReady() { return 0; }
  void DsiImplPrintDsiRegisters();
  zx_status_t DsiImplWriteReg(uint32_t reg, uint32_t val);
  zx_status_t DsiImplReadReg(uint32_t reg, uint32_t* val);
  zx_status_t DsiImplEnableBist(uint32_t pattern);

  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() {}

 private:
  uint32_t NsToCycle(uint32_t ns) { return (ns / cycle_time_); }

  void DsiReset();
  void StartDsi();
  zx_status_t WaitForIdle();
  zx_status_t WaitForRxReady();
  zx_status_t Write(const mipi_dsi_cmd_t& cmd);
  zx_status_t Read(const mipi_dsi_cmd_t& cmd);
  zx_status_t SendCmd(const mipi_dsi_cmd_t& cmd);
  zx_status_t GetColorCode(color_code_t c, uint8_t& code);
  zx_status_t GetVideoMode(video_mode_t v, uint8_t& mode);

  std::optional<ddk::MmioBuffer> dsi_mmio_;
  pdev_protocol_t pdev_proto_ = {nullptr, nullptr};
  ddk::PDev pdev_;
  uint32_t ui_;
  uint32_t cycle_time_;
};

}  // namespace dsi_mt

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DSI_MT_DSI_MT_H_
