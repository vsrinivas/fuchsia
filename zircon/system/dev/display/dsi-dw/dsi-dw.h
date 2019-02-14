// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unistd.h>
#include <zircon/compiler.h>
#include <lib/zx/bti.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/protocol/dsiimpl.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddktl/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/mipi-dsi/mipi-dsi.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include "dw-mipi-dsi-reg.h"
#include <optional>

#define DSI_ERROR(fmt, ...)    zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DSI_INFO(fmt, ...)     zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DSI_SPEW(fmt, ...)     zxlogf(SPEW, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define DSI_TRACE              zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

namespace dsi_dw {

class DsiDw;

using DeviceType = ddk::Device<DsiDw, ddk::Unbindable>;

class DsiDw : public DeviceType,
              public ddk::DsiImplProtocol<DsiDw, ddk::base_protocol> {
public:
    DsiDw(zx_device_t* parent) : DeviceType(parent), pdev_(parent) {}

    // This function is called from the c-bind function upon driver matching
    zx_status_t Bind();

    // Part of ZX_DSIIMPL_PROTOCOL
    zx_status_t DsiImplConfig(const dsi_config_t* dsi_config);
    zx_status_t DsiImplSendCmd(const mipi_dsi_cmd_t* cmd_list, size_t cmd_count);
    void DsiImplSetMode(dsi_mode_t mode);
    void DsiImplPowerUpDsi();
    void DsiImplPowerDownDsi();
    void DsiImplSendPhyCode(uint32_t code, uint32_t parameter);
    void DsiImplPhyPowerUp();
    bool DsiImplIsDsiPoweredUp();
    zx_status_t DsiImplWaitForPhyReady();
    void DsiImplPrintDsiRegisters();

    void DdkUnbind();
    void DdkRelease();

private:
    inline bool IsPldREmpty();
    inline bool IsPldRFull();
    inline bool IsPldWEmpty();
    inline bool IsPldWFull();
    inline bool IsCmdEmpty();
    inline bool IsCmdFull();
    zx_status_t WaitforFifo(uint32_t bit, bool val);
    zx_status_t WaitforPldWNotFull();
    zx_status_t WaitforPldWEmpty();
    zx_status_t WaitforPldRFull();
    zx_status_t WaitforPldRNotEmpty();
    zx_status_t WaitforCmdNotFull();
    zx_status_t WaitforCmdEmpty();
    void DumpCmd(const mipi_dsi_cmd_t& cmd);
    zx_status_t GenericPayloadRead(uint32_t* data);
    zx_status_t GenericHdrWrite(uint32_t data);
    zx_status_t GenericPayloadWrite(uint32_t data);
    void EnableBta();
    void DisableBta();
    zx_status_t WaitforBtaAck();
    zx_status_t GenWriteShort(const mipi_dsi_cmd_t& cmd);
    zx_status_t DcsWriteShort(const mipi_dsi_cmd_t& cmd);
    zx_status_t GenWriteLong(const mipi_dsi_cmd_t& cmd);
    zx_status_t GenRead(const mipi_dsi_cmd_t& cmd);
    zx_status_t SendCmd(const mipi_dsi_cmd_t& cmd);

    std::optional<ddk::MmioBuffer> dsi_mmio_;
    pdev_protocol_t pdev_proto_ = {nullptr, nullptr};
    ddk::PDev pdev_;
};

} // namespace dsi_dw
