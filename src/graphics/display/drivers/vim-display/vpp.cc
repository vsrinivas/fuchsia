// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/io-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <hw/reg.h>

#include "hdmitx.h"
#include "registers.h"
#include "vim-display.h"

enum {
  IDX_OSD1_START,                    // 0
  OSD1_IDX_CFG_W0 = IDX_OSD1_START,  // 0
  OSD1_IDX_CTRL_STAT,                // 1
  OSD1_IDX_MISC,                     // 2
  IDX_OSD1_END,                      // 3

  IDX_OSD2_START = IDX_OSD1_END,     // 3
  OSD2_IDX_CFG_W0 = IDX_OSD2_START,  // 3
  OSD2_IDX_CTRL_STAT,                // 4
  OSD2_IDX_MISC,                     // 5
  IDX_OSD2_END,                      // 6

  IDX_VD1_START = IDX_OSD2_END,    // 6
  VD1_IDX_IF_GEN = IDX_VD1_START,  // 6
  VD1_IDX_IF_CANVAS,               // 7
  VD1_IDX_IF_MISC,                 // 8
  IDX_VD1_END,                     // 9

  IDX_VD2_START = IDX_VD1_END,     // 9
  VD2_IDX_IF_GEN = IDX_VD2_START,  // 9
  VD2_IDX_IF_CANVAS,               // 10
  VD2_IDX_IF_MISC,                 // 11
  IDX_VD2_END,                     // 12

  IDX_MAX = IDX_VD2_END,  // 12
};

constexpr uint32_t kRdmaTableMaxIndex = IDX_MAX;

#define OSD_INDEX_START(x) (IDX_OSD1_START + (x * 3))
#define OSD_INDEX_END(x) (IDX_OSD1_END + (x * 3))
#define VD_INDEX_START(x) (IDX_VD1_START + (x * 3))
#define VD_INDEX_END(x) (IDX_VD1_END + (x * 3))
#define INDEX_START(x) (x * 3)

void reset_rdma_table(vim2_display_t* display);
void set_rdma_table_value(vim2_display_t* display, uint32_t channel, uint32_t idx, uint32_t val);
void flush_rdma_table(vim2_display_t* display, uint32_t channel);
zx_status_t setup_rdma(vim2_display_t* display);
int get_next_avail_rdma_channel(vim2_display_t* display);

int rdma_thread(void* arg) {
  zx_status_t status;
  vim2_display_t* display = static_cast<vim2_display_t*>(arg);
  for (;;) {
    status = zx_interrupt_wait(display->rdma_interrupt, nullptr);
    if (status != ZX_OK) {
      DISP_ERROR("RDMA Interrupt wait failed\n");
      break;
    }
    // RDMA completed. Remove source for all finished DMA channels
    for (int i = 0; i < kMaxRdmaChannels; i++) {
      if (display->mmio_vpu->Read32(VPU_RDMA_STATUS) & RDMA_STATUS_DONE(i)) {
        uint32_t regVal = display->mmio_vpu->Read32(VPU_RDMA_ACCESS_AUTO);
        regVal &= ~RDMA_ACCESS_AUTO_INT_EN(i);  // VSYNC interrupt source
        display->mmio_vpu->Write32(regVal, VPU_RDMA_ACCESS_AUTO);
        // clear interrupts
        display->mmio_vpu->Write32(display->mmio_vpu->Read32(VPU_RDMA_CTRL) | RDMA_CTRL_INT_DONE(i),
                                   VPU_RDMA_CTRL);
        display->rdma_container.rdma_chnl_container[i].active = false;
      }
    }
  }

  return status;
}

void osd_debug_dump_register_all(vim2_display_t* display) {
  uint32_t reg = 0;
  uint32_t offset = 0;
  uint32_t index = 0;

  reg = VPU_VPU_VIU_VENC_MUX_CTRL;
  DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
  reg = VPU_VPP_MISC;
  DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
  reg = VPU_VPP_OFIFO_SIZE;
  DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
  reg = VPU_VPP_HOLD_LINES;
  DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
  reg = VPU_VPP_OSD_SC_CTRL0;
  DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
  reg = VPU_VPP_OSD_SCI_WH_M1;
  DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
  reg = VPU_VPP_OSD_SCO_H_START_END;
  DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
  reg = VPU_VPP_OSD_SCO_V_START_END;
  DISP_INFO("reg[0x%x]: 0x%08x\n\n", (reg >> 2), READ32_VPU_REG(reg));
  reg = VPU_VPP_POSTBLEND_H_SIZE;
  DISP_INFO("reg[0x%x]: 0x%08x\n\n", (reg >> 2), READ32_VPU_REG(reg));

  for (index = 0; index < 2; index++) {
    if (index == 1)
      offset = (0x20 << 2);
    reg = offset + VPU_VIU_OSD1_FIFO_CTRL_STAT;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = offset + VPU_VIU_OSD1_CTRL_STAT;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = offset + VPU_VIU_OSD1_BLK0_CFG_W0;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = offset + VPU_VIU_OSD1_BLK0_CFG_W1;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = offset + VPU_VIU_OSD1_BLK0_CFG_W2;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = offset + VPU_VIU_OSD1_BLK0_CFG_W3;
    DISP_INFO("reg[0x%x]: 0x%08x\n", (reg >> 2), READ32_VPU_REG(reg));
    reg = VPU_VIU_OSD1_BLK0_CFG_W4;
    if (index == 1)
      reg = VPU_VIU_OSD2_BLK0_CFG_W4;
    DISP_INFO("reg[0x%x]: 0x%08x\n\n", (reg >> 2), READ32_VPU_REG(reg));
  }
}

void disable_vd(vim2_display* display, uint32_t vd_index) {
  display->vd1_image_valid = false;
  auto* const vpu = &*display->mmio_vpu;
  registers::Vd(vd_index).IfGenReg().ReadFrom(vpu).set_enable(false).WriteTo(vpu);
  registers::VpuVppMisc::Get().ReadFrom(vpu).set_vd1_enable_postblend(false).WriteTo(vpu);
}

void configure_vd(vim2_display* display, uint32_t vd_index) {
  disable_vd(display, vd_index);
  auto* const vpu = &*display->mmio_vpu;
  uint32_t x_start, x_end, y_start, y_end;
  x_start = y_start = 0;
  x_end = display->cur_display_mode.h_addressable - 1;
  y_end = display->cur_display_mode.v_addressable - 1;

  auto vd = registers::Vd(vd_index);
  vd.IfLumaX0().FromValue(0).set_end(x_end).set_start(x_start).WriteTo(vpu);
  vd.IfLumaY0().FromValue(0).set_end(y_end).set_start(y_start).WriteTo(vpu);
  vd.IfChromaX0().FromValue(0).set_end(x_end / 2).set_start(x_start / 2).WriteTo(vpu);
  vd.IfChromaY0().FromValue(0).set_end(y_end / 2).set_start(y_start / 2).WriteTo(vpu);
  vd.IfGenReg2().FromValue(0).set_color_map(1).WriteTo(vpu);
  vd.FmtCtrl()
      .FromValue(0)
      .set_vertical_enable(true)
      .set_vertical_phase_step(8)
      .set_vertical_initial_phase(0xc)
      .set_vertical_repeat_line0(true)
      .set_horizontal_enable(true)
      .set_horizontal_yc_ratio(1)
      .WriteTo(vpu);
  vd.FmtW()
      .FromValue(0)
      .set_horizontal_width(display->cur_display_mode.h_addressable)
      .set_vertical_width(display->cur_display_mode.h_addressable / 2)
      .WriteTo(vpu);

  vd.IfRptLoop().FromValue(0).WriteTo(vpu);
  vd.IfLuma0RptPat().FromValue(0).WriteTo(vpu);
  vd.IfChroma0RptPat().FromValue(0).WriteTo(vpu);
  vd.IfLumaPsel().FromValue(0).WriteTo(vpu);
  vd.IfChromaPsel().FromValue(0).WriteTo(vpu);
}

void flip_vd(vim2_display* display, uint32_t vd_index, uint32_t index) {
  display->vd1_image_valid = true;
  display->vd1_image = index;
  auto* const vpu = &*display->mmio_vpu;
  auto vd = registers::Vd(vd_index);

  // Get the first available channel
  int rdma_channel = get_next_avail_rdma_channel(display);
  if (rdma_channel < 0) {
    ZX_DEBUG_ASSERT(false);
    return;
  }

  display->rdma_container.rdma_chnl_container[rdma_channel].active = true;
  DISP_SPEW("Channel used is %d, idx = %d\n", rdma_channel, index);

  set_rdma_table_value(display, rdma_channel, INDEX_START(vd_index) + VD1_IDX_IF_GEN,
                       vd.IfGenReg()
                           .FromValue(0)
                           .set_enable(true)
                           .set_separate_en(true)
                           .set_chro_rpt_lastl_ctrl(true)
                           .set_hold_lines(3)
                           .set_urgent_luma(true)
                           .set_urgent_chroma(true)
                           .reg_value());
  set_rdma_table_value(display, rdma_channel, INDEX_START(vd_index) + VD1_IDX_IF_CANVAS,
                       vd.IfCanvas0().FromValue(index).reg_value());
  set_rdma_table_value(
      display, rdma_channel, INDEX_START(vd_index) + VD1_IDX_IF_MISC,
      registers::VpuVppMisc::Get().ReadFrom(vpu).set_vd1_enable_postblend(true).reg_value());

  flush_rdma_table(display, rdma_channel);

  // Write the start and end address of the table. End address is the last address that the
  // RDMA engine reads from
  zx_paddr_t phys = display->rdma_container.rdma_chnl_container[rdma_channel].phys_offset;
  display->mmio_vpu->Write32(
      static_cast<uint32_t>(phys + VD_INDEX_START(vd_index) * sizeof(RdmaTable)),
      VPU_RDMA_AHB_START_ADDR(rdma_channel));
  display->mmio_vpu->Write32(
      static_cast<uint32_t>(phys + (VD_INDEX_END(vd_index) * sizeof(RdmaTable) - 4)),
      VPU_RDMA_AHB_END_ADDR(rdma_channel));

  // Enable Auto mode: Non-Increment, VSync Interrupt Driven, Write
  uint32_t regVal = vpu->Read32(VPU_RDMA_ACCESS_AUTO);
  regVal = RDMA_ACCESS_AUTO_INT_EN(rdma_channel);  // VSYNC interrupt source
  regVal |= RDMA_ACCESS_AUTO_WRITE(rdma_channel);  // Write
  vpu->Write32(regVal, VPU_RDMA_ACCESS_AUTO);
}

void disable_osd(vim2_display_t* display, uint32_t osd_index) {
  display->current_image_valid = false;
  auto* const vpu = &*display->mmio_vpu;
  auto osd = registers::Osd(osd_index);
  osd.CtrlStat().ReadFrom(vpu).set_osd_blk_enable(false).WriteTo(vpu);
  if (osd_index == 0) {
    registers::VpuVppMisc::Get().ReadFrom(vpu).set_osd1_enable_postblend(false).WriteTo(vpu);
  } else {
    registers::VpuVppMisc::Get().ReadFrom(vpu).set_osd2_enable_postblend(false).WriteTo(vpu);
  }
}

// Disables the OSD until a flip happens
zx_status_t configure_osd(vim2_display_t* display, uint32_t osd_index) {
  uint32_t x_start, x_end, y_start, y_end;
  x_start = y_start = 0;
  x_end = display->cur_display_mode.h_addressable - 1;
  y_end = display->cur_display_mode.v_addressable - 1;

  disable_osd(display, osd_index);
  auto* const vpu = &*display->mmio_vpu;
  auto osd = registers::Osd(osd_index);
  registers::VpuVppOsdScCtrl0::Get().FromValue(0).WriteTo(vpu);

  osd.CtrlStat2().ReadFrom(vpu).set_replaced_alpha_en(true).set_replaced_alpha(0xff).WriteTo(vpu);

  osd.Blk0CfgW1()
      .FromValue(0)
      .set_virtual_canvas_x_end(x_end)
      .set_virtual_canvas_x_start(x_start)
      .WriteTo(vpu);
  osd.Blk0CfgW2()
      .FromValue(0)
      .set_virtual_canvas_y_end(y_end)
      .set_virtual_canvas_y_start(y_start)
      .WriteTo(vpu);
  osd.Blk0CfgW3().FromValue(0).set_display_h_end(x_end).set_display_h_start(x_start).WriteTo(vpu);
  osd.Blk0CfgW4().FromValue(0).set_display_v_end(y_end).set_display_v_start(y_start).WriteTo(vpu);

  registers::VpuVppOsdScoHStartEnd::Get().FromValue(0).WriteTo(vpu);
  registers::VpuVppOsdScoVStartEnd::Get().FromValue(0).WriteTo(vpu);

  registers::VpuVppPostblendHSize::Get()
      .FromValue(display->cur_display_mode.h_addressable)
      .WriteTo(vpu);
  registers::VpuVppOsdSciWhM1::Get().FromValue(0).WriteTo(vpu);

  return ZX_OK;
}

void flip_osd(vim2_display_t* display, uint32_t osd_index, uint8_t idx) {
  display->current_image = idx;
  display->current_image_valid = true;
  auto* const vpu = &*display->mmio_vpu;
  auto osd = registers::Osd(osd_index);

  // Get the first available channel
  int rdma_channel = get_next_avail_rdma_channel(display);
  if (rdma_channel < 0) {
    ZX_DEBUG_ASSERT(false);
    return;
  }
  display->rdma_container.rdma_chnl_container[rdma_channel].active = true;
  DISP_SPEW("Channel used is %d, idx = %d\n", rdma_channel, idx);

  set_rdma_table_value(display, rdma_channel, INDEX_START(osd_index) + OSD1_IDX_CFG_W0,
                       osd.Blk0CfgW0()
                           .FromValue(0)
                           .set_tbl_addr(idx)
                           .set_little_endian(true)
                           .set_block_mode(registers::VpuViuOsdBlk0CfgW0::kBlockMode32Bit)
                           .set_rgb_en(true)
                           .set_color_matrix(registers::VpuViuOsdBlk0CfgW0::kColorMatrixARGB8888)
                           .reg_value());

  set_rdma_table_value(display, rdma_channel, INDEX_START(osd_index) + OSD1_IDX_CTRL_STAT,
                       osd.CtrlStat().ReadFrom(vpu).set_osd_blk_enable(true).reg_value());
  if (osd_index == 0) {
    set_rdma_table_value(
        display, rdma_channel, INDEX_START(osd_index) + OSD1_IDX_MISC,
        registers::VpuVppMisc::Get().ReadFrom(vpu).set_osd1_enable_postblend(true).reg_value());
  } else {
    set_rdma_table_value(
        display, rdma_channel, INDEX_START(osd_index) + OSD1_IDX_MISC,
        registers::VpuVppMisc::Get().ReadFrom(vpu).set_osd2_enable_postblend(true).reg_value());
  }

  flush_rdma_table(display, rdma_channel);

  // Write the start and end address of the table. End address is the last address that the
  // RDMA engine reads from
  zx_paddr_t phys = display->rdma_container.rdma_chnl_container[rdma_channel].phys_offset;
  display->mmio_vpu->Write32(
      static_cast<uint32_t>(phys + OSD_INDEX_START(osd_index) * sizeof(RdmaTable)),
      VPU_RDMA_AHB_START_ADDR(rdma_channel));
  display->mmio_vpu->Write32(
      static_cast<uint32_t>(phys + (OSD_INDEX_END(osd_index) * sizeof(RdmaTable) - 4)),
      VPU_RDMA_AHB_END_ADDR(rdma_channel));

  // Enable Auto mode: Non-Increment, VSync Interrupt Driven, Write
  uint32_t regVal = vpu->Read32(VPU_RDMA_ACCESS_AUTO);
  regVal |= RDMA_ACCESS_AUTO_INT_EN(rdma_channel);  // VSYNC interrupt source
  regVal |= RDMA_ACCESS_AUTO_WRITE(rdma_channel);   // Write
  vpu->Write32(regVal, VPU_RDMA_ACCESS_AUTO);
}

void reset_rdma_table(vim2_display_t* display) {
  for (int i = 0; i < kMaxRdmaChannels; i++) {
    // Setup RDMA Table Register values
    uint8_t* virt_offset = display->rdma_container.rdma_chnl_container[i].virt_offset;
    RdmaTable* rdma_table = reinterpret_cast<RdmaTable*>(virt_offset);
    rdma_table[OSD1_IDX_CFG_W0].reg = (VPU_VIU_OSD1_BLK0_CFG_W0 >> 2);
    rdma_table[OSD1_IDX_CTRL_STAT].reg = (VPU_VIU_OSD1_CTRL_STAT >> 2);
    rdma_table[OSD1_IDX_MISC].reg = (VPU_VPP_MISC >> 2);

    rdma_table[OSD2_IDX_CFG_W0].reg = (VPU_VIU_OSD2_BLK0_CFG_W0 >> 2);
    rdma_table[OSD2_IDX_CTRL_STAT].reg = (VPU_VIU_OSD2_CTRL_STAT >> 2);
    rdma_table[OSD2_IDX_MISC].reg = (VPU_VPP_MISC >> 2);

    rdma_table[VD1_IDX_IF_GEN].reg = (VPU_VD1_IF0_GEN_REG >> 2);
    rdma_table[VD1_IDX_IF_CANVAS].reg = (VPU_VD1_IF0_CANVAS0 >> 2);
    rdma_table[VD1_IDX_IF_MISC].reg = (VPU_VPP_MISC >> 2);

    rdma_table[VD2_IDX_IF_GEN].reg = (VPU_VD2_IF0_GEN_REG >> 2);
    rdma_table[VD2_IDX_IF_CANVAS].reg = (VPU_VD2_IF0_CANVAS0 >> 2);
    rdma_table[VD2_IDX_IF_MISC].reg = (VPU_VPP_MISC >> 2);
  }
}

void set_rdma_table_value(vim2_display_t* display, uint32_t channel, uint32_t idx, uint32_t val) {
  ZX_DEBUG_ASSERT(idx < kRdmaTableMaxIndex);
  ZX_DEBUG_ASSERT(channel < kMaxRdmaChannels);
  uint8_t* virt_offset = display->rdma_container.rdma_chnl_container[channel].virt_offset;
  RdmaTable* rdma_table = reinterpret_cast<RdmaTable*>(virt_offset);
  rdma_table[idx].val = val;
}

int get_next_avail_rdma_channel(vim2_display_t* display) {
  // The next RDMA channel is the one that is not being used by hardware
  // A channel is considered available if it's not busy OR the done bit is set
  uint8_t retry_count = 0;
  while (retry_count++ < kMaxRetries) {
    for (int i = 0; i < kMaxRdmaChannels; i++) {
      if (!display->rdma_container.rdma_chnl_container[i].active) {
        return i;
      }
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
  }
  return -1;
}

void flush_rdma_table(vim2_display_t* display, uint32_t channel) {
  uint8_t* virt = display->rdma_container.rdma_chnl_container[channel].virt_offset;
  zx_status_t status = zx_cache_flush(virt, sizeof(RdmaTable) * kRdmaTableMaxIndex,
                                      ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  if (status != ZX_OK) {
    DISP_ERROR("Could not clean cache %d\n", status);
    return;
  }
}

zx_status_t setup_rdma(vim2_display_t* display) {
  DISP_SPEW("Setting up RDMA\n");
  zx_status_t status;

  // since we are flushing the caches, make sure the tables are at least cache_line apart
  ZX_DEBUG_ASSERT(kChannelBaseOffset > zx_system_get_dcache_line_size());
  static_assert((kMaxRdmaChannels * kChannelBaseOffset) < ZX_PAGE_SIZE);

  // Allocate one page for RDMA Table
  status = zx_vmo_create(ZX_PAGE_SIZE, 0, &display->rdma_container.rdma_vmo);
  if (status != ZX_OK) {
    DISP_ERROR("Could not create RDMA VMO (%d)\n", status);
    return status;
  }

  status = zx_bti_pin(display->bti, ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE,
                      display->rdma_container.rdma_vmo, 0, ZX_PAGE_SIZE,
                      &display->rdma_container.rdma_phys, 1, &display->rdma_container.rdma_pmt);
  if (status != ZX_OK) {
    DISP_ERROR("Could not pin RDMA VMO (%d)\n", status);
    return status;
  }

  status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
                       display->rdma_container.rdma_vmo, 0, ZX_PAGE_SIZE,
                       reinterpret_cast<zx_vaddr_t*>(&display->rdma_container.rdma_vbuf));
  if (status != ZX_OK) {
    DISP_ERROR("Could not map vmar (%d)\n", status);
    return status;
  }

  // Initialize each rdma channel container
  for (int i = 0; i < kMaxRdmaChannels; i++) {
    zx_paddr_t phys = display->rdma_container.rdma_phys + (i * kChannelBaseOffset);
    uint8_t* virt = display->rdma_container.rdma_vbuf + (i * kChannelBaseOffset);
    display->rdma_container.rdma_chnl_container[i].phys_offset = phys;
    display->rdma_container.rdma_chnl_container[i].virt_offset = virt;
    display->rdma_container.rdma_chnl_container[i].active = false;
  }

  // Setup RDMA_CTRL:
  // Default: no reset, no clock gating, burst size 4x16B for read and write
  // DDR Read/Write request urgent
  uint32_t regVal = RDMA_CTRL_READ_URGENT | RDMA_CTRL_WRITE_URGENT;
  display->mmio_vpu->Write32(regVal, VPU_RDMA_CTRL);

  reset_rdma_table(display);

  return status;
}

void release_osd(vim2_display_t* display) {
  zx_interrupt_destroy(display->rdma_interrupt);
  int res;
  thrd_join(display->rdma_thread, &res);
  zx_pmt_unpin(display->rdma_container.rdma_pmt);
  zx_vmar_unmap(zx_vmar_root_self(),
                reinterpret_cast<zx_vaddr_t>(display->rdma_container.rdma_vbuf), ZX_PAGE_SIZE);
}
