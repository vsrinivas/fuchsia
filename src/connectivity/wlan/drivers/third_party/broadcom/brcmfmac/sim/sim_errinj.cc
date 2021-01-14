/*
 * Copyright (c) 2020 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_errinj.h"

#include <cstring>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bits.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil.h"

namespace wlan::brcmfmac {

SimErrorInjector::SimErrorInjector() = default;
SimErrorInjector::~SimErrorInjector() = default;

void SimErrorInjector::AddErrInjCmd(uint32_t cmd, zx_status_t status, bcme_status_t fw_err,
                                    std::optional<uint16_t> ifidx) {
  for (auto& existing_cmd : cmds_) {
    if (existing_cmd.cmd == cmd) {
      // Entry already present, just replace with the new values
      BRCMF_DBG(SIMERRINJ, "entry cmd: %d if:%d status:%d present, replace", cmd,
                ifidx.value_or(-1), status);
      existing_cmd.ifidx = ifidx;
      existing_cmd.ret_status = status;
      return;
    }
  }
  ErrInjCmd err_inj_cmd(cmd, status, fw_err, ifidx);
  cmds_.push_back(err_inj_cmd);
  BRCMF_DBG(SIMERRINJ, "Num entries in list: %lu\n", cmds_.size());
}

void SimErrorInjector::DelErrInjCmd(uint32_t cmd) {
  for (auto it = cmds_.begin(); it != cmds_.end(); ++it) {
    if (it->cmd == cmd) {
      BRCMF_DBG(SIMERRINJ, "Del Err Inj entry found status:%d cmd:%d", it->ret_status, cmd);
      cmds_.erase(it);
      BRCMF_DBG(SIMERRINJ, "Num entries in list: %lu\n", cmds_.size());
      return;
    }
  }
  BRCMF_DBG(SIMERRINJ, "Cmd: %d not found", cmd);
}

void SimErrorInjector::AddErrInjIovar(const char* iovar, zx_status_t status, bcme_status_t fw_err,
                                      std::optional<uint16_t> ifidx,
                                      const std::vector<uint8_t>* alt_data) {
  for (auto& existing_iovar : iovars_) {
    if (existing_iovar.iovar.size() == strlen(iovar) + 1 &&
        std::memcmp(iovar, existing_iovar.iovar.data(), existing_iovar.iovar.size()) == 0) {
      // Entry already present
      BRCMF_DBG(SIMERRINJ, "entry iovar: %s if:%d status:%d present, replace", iovar,
                ifidx.value_or(-1), status);
      existing_iovar.ifidx = ifidx;
      existing_iovar.ret_status = status;
      return;
    }
  }
  ErrInjIovar err_inj_iovar(iovar, status, fw_err, ifidx, alt_data);
  iovars_.push_back(err_inj_iovar);
  BRCMF_DBG(SIMERRINJ, "Num entries in list: %lu\n", iovars_.size());
}

void SimErrorInjector::DelErrInjIovar(const char* iovar) {
  for (auto it = iovars_.begin(); it != iovars_.end(); ++it) {
    if (!it->iovar.empty() && std::memcmp(it->iovar.data(), iovar, strlen(iovar) + 1) == 0) {
      BRCMF_DBG(SIMERRINJ, "Del Err Inj entry found status:%d iovar:%s", it->ret_status, iovar);
      iovars_.erase(it);
      BRCMF_DBG(SIMERRINJ, "Num entries in list: %lu\n", iovars_.size());
      return;
    }
  }
  BRCMF_DBG(SIMERRINJ, "iovar: %s not found", iovar);
}

bool SimErrorInjector::CheckIfErrInjCmdEnabled(uint32_t cmd, zx_status_t* ret_status,
                                               bcme_status_t* ret_fw_err, uint16_t ifidx) {
  for (auto& existing_cmd : cmds_) {
    if (((existing_cmd.ifidx.has_value() && existing_cmd.ifidx == ifidx) ||
         !existing_cmd.ifidx.has_value()) &&
        existing_cmd.cmd == cmd) {
      BRCMF_DBG(SIMERRINJ, "Err Inj entry found if:%d status:%d cmd:%d", ifidx,
                existing_cmd.ret_status, cmd);
      if (ret_status) {
        *ret_status = existing_cmd.ret_status;
      }
      if (ret_fw_err) {
        *ret_fw_err = existing_cmd.ret_fw_err;
      }
      return true;
    }
  }
  BRCMF_DBG(SIMERRINJ, "Cmd: %d ifidx: %d not found", cmd, ifidx);
  return false;
}

bool SimErrorInjector::CheckIfErrInjIovarEnabled(const char* iovar, zx_status_t* ret_status,
                                                 bcme_status_t* ret_fw_err,
                                                 const std::vector<uint8_t>** alt_value_out,
                                                 uint16_t ifidx) {
  for (auto& existing_iovar : iovars_) {
    if (((existing_iovar.ifidx.has_value() && existing_iovar.ifidx == ifidx) ||
         !existing_iovar.ifidx.has_value()) &&
        existing_iovar.iovar.size() == strlen(iovar) + 1 &&
        !std::memcmp(existing_iovar.iovar.data(), iovar, existing_iovar.iovar.size())) {
      BRCMF_DBG(SIMERRINJ, "Err Inj entry found if:%d status:%d iovar:%s", ifidx,
                existing_iovar.ret_status, iovar);
      if (alt_value_out) {
        *alt_value_out = existing_iovar.alt_data;
      }
      if (ret_status) {
        *ret_status = existing_iovar.ret_status;
      }
      if (ret_fw_err) {
        *ret_fw_err = existing_iovar.ret_fw_err;
      }
      return true;
    }
  }
  BRCMF_DBG(SIMERRINJ, "iovar: %s ifidx: %d not found", iovar, ifidx);
  return false;
}

}  // namespace wlan::brcmfmac
