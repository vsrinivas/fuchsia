// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>

#include "dwc3-regs.h"
#include "dwc3.h"

namespace dwc3 {

void Dwc3::CmdStartNewConfig(const Endpoint& ep, uint32_t rsrc_id) {
  fbl::AutoLock lock(&lock_);

  auto* mmio = get_mmio();
  const uint8_t ep_num = ep.ep_num;

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

void Dwc3::CmdEpSetConfig(const Endpoint& ep, bool modify) {
  fbl::AutoLock lock(&lock_);

  auto* mmio = get_mmio();
  const uint8_t ep_num = ep.ep_num;

  // fifo number is zero for OUT endpoints and EP0_IN
  const uint32_t fifo_num = (ep.IsOutput() || (ep_num == kEp0In)) ? 0 : ep_num >> 1;
  const uint32_t action =
      modify ? DEPCFG_DEPCMDPAR0::ACTION_MODIFY : DEPCFG_DEPCMDPAR0::ACTION_INITIALIZE;

  DEPCFG_DEPCMDPAR0::Get(ep_num)
      .FromValue(0)
      .set_FIFO_NUM(fifo_num)
      .set_MAX_PACKET_SIZE(ep.max_packet_size)
      .set_EP_TYPE(ep.type)
      .set_ACTION(action)
      .WriteTo(mmio);
  DEPCFG_DEPCMDPAR1::Get(ep_num)
      .FromValue(0)
      .set_EP_NUMBER(ep_num)
      .set_INTERVAL(ep.interval)
      .set_XFER_NOT_READY_EN(1)
      .set_XFER_COMPLETE_EN(1)
      .set_INTR_NUM(0)
      .WriteTo(mmio);
  DEPCMDPAR2::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMD::Get(ep_num).FromValue(0).set_CMDTYP(DEPCMD::DEPCFG).set_CMDACT(1).WriteTo(mmio);
}

void Dwc3::CmdEpTransferConfig(const Endpoint& ep) {
  fbl::AutoLock lock(&lock_);
  auto* mmio = get_mmio();
  const uint8_t ep_num = ep.ep_num;

  DEPCMDPAR0::Get(ep_num).FromValue(0).set_PARAMETER(1).WriteTo(mmio);
  DEPCMDPAR1::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMDPAR2::Get(ep_num).FromValue(0).WriteTo(mmio);
  DEPCMD::Get(ep_num).FromValue(0).set_CMDTYP(DEPCMD::DEPXFERCFG).set_CMDACT(1).WriteTo(mmio);
}

void Dwc3::CmdEpStartTransfer(const Endpoint& ep, zx_paddr_t trb_phys) {
  fbl::AutoLock lock(&lock_);
  auto* mmio = get_mmio();
  const uint8_t ep_num = ep.ep_num;

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

void Dwc3::CmdEpEndTransfer(const Endpoint& ep) {
  fbl::AutoLock lock(&lock_);
  auto* mmio = get_mmio();

  const uint32_t ep_num = ep.ep_num;
  const uint32_t rsrc_id = ep.rsrc_id;

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

void Dwc3::CmdEpSetStall(const Endpoint& ep) {
  fbl::AutoLock lock(&lock_);
  auto* mmio = get_mmio();

  const uint32_t ep_num = ep.ep_num;

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

void Dwc3::CmdEpClearStall(const Endpoint& ep) {
  fbl::AutoLock lock(&lock_);
  auto* mmio = get_mmio();

  const uint32_t ep_num = ep.ep_num;

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

}  // namespace dwc3
