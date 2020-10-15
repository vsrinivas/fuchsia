// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DSI_DW_DSI_DW_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DSI_DW_DSI_DW_H_

#include <fuchsia/hardware/dsi/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fidl/llcpp/types.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/mipi-dsi/mipi-dsi.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <optional>

#include <ddk/driver.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/dsi.h>
#include <ddktl/protocol/dsiimpl.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "dw-mipi-dsi-reg.h"

#define DSI_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DSI_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DSI_SPEW(fmt, ...) zxlogf(TRACE, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DSI_TRACE zxlogf(INFO, "[%s %d]", __func__, __LINE__)

namespace dsi_dw {

class DsiDwBase;
class DsiDw;

namespace fidl_dsi = ::llcpp::fuchsia::hardware::dsi;

using DeviceTypeBase = ddk::Device<DsiDwBase, ddk::Unbindable, ddk::Messageable>;
class DsiDwBase : public DeviceTypeBase,
                  public ddk::EmptyProtocol<ZX_PROTOCOL_DSI_BASE>,
                  public llcpp::fuchsia::hardware::dsi::DsiBase::Interface {
 public:
  DsiDwBase(zx_device_t* parent, DsiDw* dsidw) : DeviceTypeBase(parent), dsidw_(dsidw) {}
  zx_status_t Bind();

  // FIDL
  void SendCmd(::llcpp::fuchsia::hardware::dsi::MipiDsiCmd cmd, ::fidl::VectorView<uint8_t> txdata,
               SendCmdCompleter::Sync& _completer) override;

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);

 private:
  DsiDw* dsidw_;
};

using DeviceType = ddk::Device<DsiDw, ddk::Unbindable>;

class DsiDw : public DeviceType, public ddk::DsiImplProtocol<DsiDw, ddk::base_protocol> {
 public:
  DsiDw(zx_device_t* parent) : DeviceType(parent), pdev_(parent) {}

  // This function is called from the c-bind function upon driver matching
  zx_status_t Bind();

  // Part of ZX_DSIIMPL_PROTOCOL
  zx_status_t DsiImplConfig(const dsi_config_t* dsi_config);
  void DsiImplPowerUp();
  void DsiImplPowerDown();
  void DsiImplSetMode(dsi_mode_t mode);
  zx_status_t DsiImplSendCmd(const mipi_dsi_cmd_t* cmd_list, size_t cmd_count);
  bool DsiImplIsPoweredUp();
  void DsiImplReset() { DsiImplPowerDown(); }
  zx_status_t DsiImplPhyConfig(const dsi_config_t* dsi_config) { return ZX_OK; }
  void DsiImplPhyPowerUp();
  void DsiImplPhyPowerDown();
  void DsiImplPhySendCode(uint32_t code, uint32_t parameter);
  zx_status_t DsiImplPhyWaitForReady();
  void DsiImplPrintDsiRegisters();
  zx_status_t DsiImplWriteReg(uint32_t reg, uint32_t val);
  zx_status_t DsiImplReadReg(uint32_t reg, uint32_t* val);
  zx_status_t DsiImplEnableBist(uint32_t pattern);

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

 private:
  inline bool IsPldREmpty() TA_REQ(command_lock_);
  inline bool IsPldRFull() TA_REQ(command_lock_);
  inline bool IsPldWEmpty() TA_REQ(command_lock_);
  inline bool IsPldWFull() TA_REQ(command_lock_);
  inline bool IsCmdEmpty() TA_REQ(command_lock_);
  inline bool IsCmdFull() TA_REQ(command_lock_);
  zx_status_t WaitforFifo(uint32_t bit, bool val) TA_REQ(command_lock_);
  zx_status_t WaitforPldWNotFull() TA_REQ(command_lock_);
  zx_status_t WaitforPldWEmpty() TA_REQ(command_lock_);
  zx_status_t WaitforPldRFull() TA_REQ(command_lock_);
  zx_status_t WaitforPldRNotEmpty() TA_REQ(command_lock_);
  zx_status_t WaitforCmdNotFull() TA_REQ(command_lock_);
  zx_status_t WaitforCmdEmpty() TA_REQ(command_lock_);
  void DumpCmd(const mipi_dsi_cmd_t& cmd);
  zx_status_t GenericPayloadRead(uint32_t* data) TA_REQ(command_lock_);
  zx_status_t GenericHdrWrite(uint32_t data) TA_REQ(command_lock_);
  zx_status_t GenericPayloadWrite(uint32_t data) TA_REQ(command_lock_);
  void EnableBta() TA_REQ(command_lock_);
  void DisableBta() TA_REQ(command_lock_);
  zx_status_t WaitforBtaAck() TA_REQ(command_lock_);
  zx_status_t GenWriteShort(const mipi_dsi_cmd_t& cmd) TA_REQ(command_lock_);
  zx_status_t DcsWriteShort(const mipi_dsi_cmd_t& cmd) TA_REQ(command_lock_);
  zx_status_t GenWriteLong(const mipi_dsi_cmd_t& cmd) TA_REQ(command_lock_);
  zx_status_t DcsWriteShort(const fidl_dsi::MipiDsiCmd& cmd, fidl::VectorView<uint8_t>& txdata)
      TA_REQ(command_lock_);
  zx_status_t GenRead(const mipi_dsi_cmd_t& cmd) TA_REQ(command_lock_);
  zx_status_t SendCommand(const mipi_dsi_cmd_t& cmd);
  zx_status_t SendCommand(const fidl_dsi::MipiDsiCmd& cmd, fidl::VectorView<uint8_t>& txdata,
                          fidl::VectorView<uint8_t>& response);
  zx_status_t GetColorCode(color_code_t c, bool& packed, uint8_t& code);
  zx_status_t GetVideoMode(video_mode_t v, uint8_t& mode);
  std::optional<ddk::MmioBuffer> dsi_mmio_;
  pdev_protocol_t pdev_proto_ = {nullptr, nullptr};
  ddk::PDev pdev_;

  // This lock is used to synchronize SendCmd issued from FIDL server and Banjo interface
  fbl::Mutex command_lock_;

  friend DsiDwBase;
};

}  // namespace dsi_dw

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DSI_DW_DSI_DW_H_
