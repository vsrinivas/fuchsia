// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>

#include <fbl/auto_lock.h>

#include "dwc3-regs.h"
#include "dwc3.h"

void dwc3_cmd_start_new_config(dwc3_t* dwc, unsigned ep_num, unsigned rsrc_id) {
  auto* mmio = dwc3_mmio(dwc);

  fbl::AutoLock lock(&dwc->lock);

  DEPCMDPAR0::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMDPAR1::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMDPAR2::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMD::Get(ep_num)
      .FromValue(0)
      .set_CMDTYP(DEPCMD::DEPSTARTCFG)
      .set_COMMANDPARAM(rsrc_id)
      .set_CMDACT(1)
      .WriteTo(mmio);
}

void dwc3_cmd_ep_set_config(dwc3_t* dwc, unsigned ep_num, unsigned ep_type,
                            unsigned max_packet_size, unsigned interval, bool modify) {
  auto* mmio = dwc3_mmio(dwc);

  fbl::AutoLock lock(&dwc->lock);

  // fifo number is zero for OUT endpoints and EP0_IN
  uint32_t fifo_num = (EP_OUT(ep_num) || ep_num == EP0_IN ? 0 : ep_num >> 1);
  uint32_t action =
      (modify ? DEPCFG_DEPCMDPAR0::ACTION_MODIFY : DEPCFG_DEPCMDPAR0::ACTION_INITIALIZE);

  DEPCFG_DEPCMDPAR0::Get(ep_num)
      .FromValue(0)
      .set_FIFO_NUM(fifo_num)
      .set_MAX_PACKET_SIZE(max_packet_size)
      .set_EP_TYPE(ep_type)
      .set_ACTION(action)
      .WriteTo(mmio);
  DEPCFG_DEPCMDPAR1::Get(ep_num)
      .FromValue(0)
      .set_EP_NUMBER(ep_num)
      .set_INTERVAL(interval)
      .set_XFER_NOT_READY_EN(1)
      .set_XFER_COMPLETE_EN(1)
      .set_INTR_NUM(0)
      .WriteTo(mmio);
  DEPCMDPAR2::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMD::Get(ep_num).FromValue(0).set_CMDTYP(DEPCMD::DEPCFG).set_CMDACT(1).WriteTo(mmio);
}

void dwc3_cmd_ep_transfer_config(dwc3_t* dwc, unsigned ep_num) {
  auto* mmio = dwc3_mmio(dwc);

  fbl::AutoLock lock(&dwc->lock);

  DEPCMDPAR0::Get(ep_num).FromValue(0).set_PARAMETER(1).WriteTo(mmio);
  DEPCMDPAR1::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMDPAR2::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMD::Get(ep_num).FromValue(0).set_CMDTYP(DEPCMD::DEPXFERCFG).set_CMDACT(1).WriteTo(mmio);
}

void dwc3_cmd_ep_start_transfer(dwc3_t* dwc, unsigned ep_num, zx_paddr_t trb_phys) {
  auto* mmio = dwc3_mmio(dwc);

  fbl::AutoLock lock(&dwc->lock);

  DEPCMDPAR0::Get(ep_num)
      .FromValue(0)
      .set_PARAMETER(static_cast<uint32_t>(trb_phys >> 32))
      .WriteTo(mmio);
  DEPCMDPAR1::Get(ep_num).FromValue(0).set_PARAMETER(static_cast<uint32_t>(trb_phys)).WriteTo(mmio);
  DEPCMDPAR2::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMD::Get(ep_num)
      .FromValue(0)
      .set_CMDTYP(DEPCMD::DEPSTRTXFER)
      .set_CMDACT(1)
      .set_CMDIOC(1)
      .WriteTo(mmio);

  while (DEPCMD::Get(ep_num).ReadFrom(mmio).CMDACT()) {
    usleep(1000);
  }
}

void dwc3_cmd_ep_end_transfer(dwc3_t* dwc, unsigned ep_num) {
  auto* mmio = dwc3_mmio(dwc);

  fbl::AutoLock lock(&dwc->lock);

  unsigned rsrc_id = dwc->eps[ep_num].rsrc_id;

  DEPCMDPAR0::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMDPAR1::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMDPAR2::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMD::Get(ep_num)
      .FromValue(0)
      .set_CMDTYP(DEPCMD::DEPENDXFER)
      .set_COMMANDPARAM(rsrc_id)
      .set_CMDACT(1)
      .set_CMDIOC(1)
      .set_HIPRI_FORCERM(1)
      .WriteTo(mmio);

  while (DEPCMD::Get(ep_num).ReadFrom(mmio).CMDACT()) {
    usleep(1000);
  }
}

void dwc3_cmd_ep_set_stall(dwc3_t* dwc, unsigned ep_num) {
  auto* mmio = dwc3_mmio(dwc);

  fbl::AutoLock lock(&dwc->lock);

  DEPCMDPAR0::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMDPAR1::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMDPAR2::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMD::Get(ep_num)
      .FromValue(0)
      .set_CMDTYP(DEPCMD::DEPSSTALL)
      .set_CMDACT(1)
      .set_CMDIOC(1)
      .WriteTo(mmio);

  while (DEPCMD::Get(ep_num).ReadFrom(mmio).CMDACT()) {
    usleep(1000);
  }
}

void dwc3_cmd_ep_clear_stall(dwc3_t* dwc, unsigned ep_num) {
  auto* mmio = dwc3_mmio(dwc);

  fbl::AutoLock lock(&dwc->lock);

  DEPCMDPAR0::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMDPAR1::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMDPAR2::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMD::Get(ep_num)
      .FromValue(0)
      .set_CMDTYP(DEPCMD::DEPCSTALL)
      .set_CMDACT(1)
      .set_CMDIOC(1)
      .WriteTo(mmio);

  while (DEPCMD::Get(ep_num).ReadFrom(mmio).CMDACT()) {
    usleep(1000);
  }
}
