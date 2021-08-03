// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-hdmi.h"

#include <fuchsia/hardware/i2c/cpp/banjo.h>
#include <fuchsia/hardware/i2cimpl/cpp/banjo.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/epitaph.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>

#include "src/graphics/display/drivers/aml-hdmi/aml-hdmi-bind.h"
#include "top-regs.h"

#define HDMI_ASPECT_RATIO_NONE 0
#define HDMI_ASPECT_RATIO_4x3 1
#define HDMI_ASPECT_RATIO_16x9 2

#define HDMI_COLORIMETRY_ITU601 1
#define HDMI_COLORIMETRY_ITU709 2

#define DWC_OFFSET_MASK (0x10UL << 24)

namespace aml_hdmi {

void AmlHdmiDevice::HdmiConnect(zx::channel chan) {
  zx_status_t status;
  if (!loop_started_ && (status = loop_.StartThread("aml-hdmi-thread")) != ZX_OK) {
    zxlogf(ERROR, "%s: failed to start registers thread: %d", __func__, status);
    fidl_epitaph_write(chan.get(), status);
  }

  loop_started_ = true;
  fidl::OnChannelClosedFn<AmlHdmiDevice> cb = [&chan](AmlHdmiDevice* server) {
    fidl_epitaph_write(chan.get(), ZX_ERR_INTERNAL);
  };
  status = fidl::BindSingleInFlightOnly(
      loop_.dispatcher(), fidl::ServerEnd<fuchsia_hardware_hdmi::Hdmi>(std::move(chan)), this,
      std::move(cb));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to bind channel: %d", __func__, status);
  }
}

void AmlHdmiDevice::WriteReg(uint32_t reg, uint32_t val) {
  // determine if we are writing to HDMI TOP (AMLOGIC Wrapper) or HDMI IP
  uint32_t offset = (reg & DWC_OFFSET_MASK) >> 24;
  reg = reg & 0xffff;

  if (offset) {
    WriteIpReg(reg, val & 0xFF);
  } else {
    fbl::AutoLock lock(&register_lock_);
    hdmitx_mmio_->Write32(val, (reg << 2) + 0x8000);
  }

#ifdef LOG_HDMITX
  DISP_INFO("%s wr[0x%x] 0x%x\n", offset ? "DWC" : "TOP", reg, val);
#endif
}

uint32_t AmlHdmiDevice::ReadReg(uint32_t reg) {
  // determine if we are writing to HDMI TOP (AMLOGIC Wrapper) or HDMI IP
  uint32_t offset = (reg & DWC_OFFSET_MASK) >> 24;
  reg = reg & 0xffff;

  if (offset) {
    return ReadIpReg(reg);
  }

  fbl::AutoLock lock(&register_lock_);
  return hdmitx_mmio_->Read32((reg << 2) + 0x8000);
}

zx_status_t AmlHdmiDevice::Bind() {
  if (!pdev_.is_valid()) {
    DISP_ERROR("HdmiDw: Could not get ZX_PROTOCOL_PDEV protocol\n");
    return ZX_ERR_NO_RESOURCES;
  }

  // Map registers
  auto status = pdev_.MapMmio(MMIO_HDMI, &hdmitx_mmio_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map HDMITX mmio\n");
    return status;
  }

  status = DdkAdd(ddk::DeviceAddArgs("aml-hdmi"));
  if (status != ZX_OK) {
    DISP_ERROR("Could not add device\n");
    return status;
  }

  return ZX_OK;
}

void AmlHdmiDevice::Reset(ResetRequestView request, ResetCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(request->display_id == 1);  // only supports 1 display for now
  // TODO(fxb/69679): Add in Resets
  // reset hdmi related blocks (HIU, HDMI SYS, HDMI_TX)
  // auto reset0_result = display->reset_register_.WriteRegister32(PRESET0_REGISTER, 1 << 19, 1 <<
  // 19); if ((reset0_result.status() != ZX_OK) || reset0_result->result.is_err()) {
  //   zxlogf(ERROR, "Reset0 Write failed\n");
  // }

  /* FIXME: This will reset the entire HDMI subsystem including the HDCP engine.
   * At this point, we have no way of initializing HDCP block, so we need to
   * skip this for now.
   */
  // auto reset2_result = display->reset_register_.WriteRegister32(PRESET2_REGISTER, 1 << 15, 1 <<
  // 15); // Will mess up hdcp stuff if ((reset2_result.status() != ZX_OK) ||
  // reset2_result->result.is_err()) {
  //   zxlogf(ERROR, "Reset2 Write failed\n");
  // }

  // auto reset2_result = display->reset_register_.WriteRegister32(PRESET2_REGISTER, 1 << 2, 1 <<
  // 2); if ((reset2_result.status() != ZX_OK) || reset2_result->result.is_err()) {
  //   zxlogf(ERROR, "Reset2 Write failed\n");
  // }

  // Bring HDMI out of reset
  WriteReg(HDMITX_TOP_SW_RESET, 0);
  usleep(200);
  WriteReg(HDMITX_TOP_CLK_CNTL, 0x000000ff);

  fbl::AutoLock lock(&dw_lock_);
  auto status = hdmi_dw_->InitHw();
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void CalculateTxParam(const fuchsia_hardware_hdmi::wire::DisplayMode& mode,
                      hdmi_dw::hdmi_param_tx* p) {
  if ((mode.mode().pixel_clock_10khz * 10) > 500000) {
    p->is4K = true;
  } else {
    p->is4K = false;
  }

  if (mode.mode().h_addressable * 3 == mode.mode().v_addressable * 4) {
    p->aspect_ratio = HDMI_ASPECT_RATIO_4x3;
  } else if (mode.mode().h_addressable * 9 == mode.mode().v_addressable * 16) {
    p->aspect_ratio = HDMI_ASPECT_RATIO_16x9;
  } else {
    p->aspect_ratio = HDMI_ASPECT_RATIO_NONE;
  }

  p->colorimetry = HDMI_COLORIMETRY_ITU601;
}

void AmlHdmiDevice::ModeSet(ModeSetRequestView request, ModeSetCompleter::Sync& completer) {
  ZX_DEBUG_ASSERT(request->display_id == 1);  // only supports 1 display for now

  hdmi_dw::hdmi_param_tx p;
  CalculateTxParam(request->mode, &p);

  // Output normal TMDS Data
  WriteReg(HDMITX_TOP_BIST_CNTL, 1 << 12);

  // Configure HDMI TX IP
  fbl::AutoLock lock(&dw_lock_);
  hdmi_dw_->ConfigHdmitx(request->mode, p);
  WriteReg(HDMITX_TOP_INTR_STAT_CLR, 0x0000001f);
  hdmi_dw_->SetupInterrupts();
  WriteReg(HDMITX_TOP_INTR_MASKN, 0x9f);
  hdmi_dw_->Reset();

  if (p.is4K) {
    // Setup TMDS Clocks (taken from recommended test pattern in DVI spec)
    WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_01, 0);
    WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_23, 0x03ff03ff);
  } else {
    WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_01, 0x001f001f);
    WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_23, 0x001f001f);
  }
  hdmi_dw_->SetFcScramblerCtrl(p.is4K);

  WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_CNTL, 0x1);
  usleep(2);
  WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_CNTL, 0x2);

  hdmi_dw_->SetupScdc(p.is4K);
  hdmi_dw_->ResetFc();

  completer.ReplySuccess();
}

void AmlHdmiDevice::EdidTransfer(EdidTransferRequestView request,
                                 EdidTransferCompleter::Sync& completer) {
  if (request->ops.count() < 1 || request->ops.count() >= I2C_MAX_RW_OPS) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  fbl::AllocChecker ac;
  fbl::Array<uint8_t> read_buffer(new (&ac) uint8_t[I2C_MAX_TOTAL_TRANSFER],
                                  I2C_MAX_TOTAL_TRANSFER);
  if (!ac.check()) {
    zxlogf(ERROR, "%s could not allocate read_buffer", __FUNCTION__);
    completer.ReplyError(ZX_ERR_INTERNAL);
    return;
  }
  fbl::Array<uint8_t> write_buffer(new (&ac) uint8_t[I2C_MAX_TOTAL_TRANSFER],
                                   I2C_MAX_TOTAL_TRANSFER);
  if (!ac.check()) {
    zxlogf(ERROR, "%s could not allocate write_buffer", __FUNCTION__);
    completer.ReplyError(ZX_ERR_INTERNAL);
    return;
  }

  i2c_impl_op_t op_list[I2C_MAX_RW_OPS];
  size_t write_cnt = 0;
  size_t read_cnt = 0;
  uint8_t* p_writes = write_buffer.data();
  uint8_t* p_reads = read_buffer.data();
  for (size_t i = 0; i < request->ops.count(); ++i) {
    if (request->ops[i].is_write) {
      if (write_cnt >= request->write_segments_data.count()) {
        completer.ReplyError(ZX_ERR_INVALID_ARGS);
        return;
      }
      op_list[i].address = request->ops[i].address;
      memcpy(p_writes, request->write_segments_data[write_cnt].data(),
             request->write_segments_data[write_cnt].count());
      op_list[i].data_buffer = p_writes;
      op_list[i].data_size = request->write_segments_data[write_cnt].count();
      op_list[i].is_read = false;
      op_list[i].stop = false;
      p_writes += request->write_segments_data[write_cnt].count();
      write_cnt++;
    } else {
      if (read_cnt >= request->read_segments_length.count()) {
        completer.ReplyError(ZX_ERR_INVALID_ARGS);
        return;
      }
      op_list[i].address = request->ops[i].address;
      op_list[i].data_buffer = p_reads;
      op_list[i].data_size = request->read_segments_length[read_cnt];
      op_list[i].is_read = true;
      op_list[i].stop = false;
      p_reads += request->read_segments_length[read_cnt];
      read_cnt++;
    }
  }
  op_list[request->ops.count() - 1].stop = true;

  if (request->write_segments_data.count() != write_cnt) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  if (request->read_segments_length.count() != read_cnt) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  fbl::AutoLock lock(&dw_lock_);
  auto status = hdmi_dw_->EdidTransfer(op_list, request->ops.count());

  if (status == ZX_OK) {
    fidl::Arena allocator;
    fidl::VectorView<fidl::VectorView<uint8_t>> reads(allocator, read_cnt);
    size_t read_ops_cnt = 0;
    for (size_t i = 0; i < request->ops.count(); ++i) {
      if (!op_list[i].is_read) {
        continue;
      }
      reads[read_ops_cnt] = fidl::VectorView<uint8_t>::FromExternal(
          const_cast<uint8_t*>(op_list[i].data_buffer), op_list[i].data_size);
      read_ops_cnt++;
    }
    completer.ReplySuccess(std::move(reads));
  } else {
    completer.ReplyError(status);
  }
}

#define PRINT_REG(name) PrintReg(#name, (name))
void AmlHdmiDevice::PrintReg(std::string name, uint8_t reg) {
  zxlogf(INFO, "%s (0x%4x): %u", &name[0], reg, ReadReg(reg));
}

void AmlHdmiDevice::PrintHdmiRegisters(PrintHdmiRegistersRequestView request,
                                       PrintHdmiRegistersCompleter::Sync& completer) {
  zxlogf(INFO, "------------Top Registers------------");
  PRINT_REG(HDMITX_TOP_SW_RESET);
  PRINT_REG(HDMITX_TOP_CLK_CNTL);
  PRINT_REG(HDMITX_TOP_INTR_MASKN);
  PRINT_REG(HDMITX_TOP_INTR_STAT_CLR);
  PRINT_REG(HDMITX_TOP_BIST_CNTL);
  PRINT_REG(HDMITX_TOP_TMDS_CLK_PTTN_01);
  PRINT_REG(HDMITX_TOP_TMDS_CLK_PTTN_23);
  PRINT_REG(HDMITX_TOP_TMDS_CLK_PTTN_CNTL);

  fbl::AutoLock lock(&dw_lock_);
  hdmi_dw_->PrintRegisters();

  completer.Reply();
}
#undef PRINT_REG

// main bind function called from dev manager
zx_status_t AmlHdmiBind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<aml_hdmi::AmlHdmiDevice>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

constexpr zx_driver_ops_t aml_hdmi_ops = [] {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlHdmiBind;
  return ops;
}();

}  // namespace aml_hdmi

ZIRCON_DRIVER(aml_hdmi, aml_hdmi::aml_hdmi_ops, "zircon", "0.1");
