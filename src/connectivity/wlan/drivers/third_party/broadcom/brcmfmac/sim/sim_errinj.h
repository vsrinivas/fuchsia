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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_ERRINJ_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_ERRINJ_H_

#include <net/ethernet.h>
#include <zircon/status.h>

#include "src/connectivity/wlan/drivers/testing/lib/sim-env/sim-env.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bits.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil_types.h"

namespace wlan::brcmfmac {
// Error inject class that enables setting various types of SIM FW errors.
class SimErrorInjector {
 public:
  explicit SimErrorInjector();
  ~SimErrorInjector();

  // Iovar int command specific
  void AddErrInjCmd(uint32_t cmd, zx_status_t ret_status, std::optional<uint16_t> ifidx = {});
  void DelErrInjCmd(uint32_t cmd);
  bool CheckIfErrInjCmdEnabled(uint32_t cmd, zx_status_t* ret_status, uint16_t ifidx);

  // Iovar string command specific
  void AddErrInjIovar(const char* iovar, zx_status_t ret_status, std::optional<uint16_t> ifidx = {},
                      const std::vector<uint8_t>* alt_data = nullptr);
  void DelErrInjIovar(const char* iovar);
  bool CheckIfErrInjIovarEnabled(const char* iovar, zx_status_t* ret_status,
                                 const std::vector<uint8_t>** alt_value_out,
                                 uint16_t ifidx);

  void SetSignalErrInj(bool enable);
  bool HandleRxFrameErrorInjection(uint8_t* buffer);

 private:
  struct ErrInjCmd {
    std::optional<uint16_t> ifidx;
    uint32_t cmd;
    zx_status_t ret_status;

    ErrInjCmd(uint32_t cmd, zx_status_t status, std::optional<uint16_t> ifidx)
        : ifidx(ifidx), cmd(cmd), ret_status(status) {}
  };

  struct ErrInjIovar {
    // Name of the iovar to override
    std::vector<uint8_t> iovar;

    // If set, only apply this override on the specified interface
    std::optional<uint16_t> ifidx;

    // Status code to return when iovar is read
    zx_status_t ret_status;

    // If set, specifies bytes to be used to override the payload
    const std::vector<uint8_t>* alt_data;

    ErrInjIovar(const char* iovar_str, zx_status_t status, std::optional<uint16_t> ifidx = {},
                const std::vector<uint8_t>* alt_data = nullptr)
        : iovar(strlen(iovar_str) + 1), ifidx(ifidx), ret_status(status), alt_data(alt_data) {
      memcpy(iovar.data(), iovar_str, strlen(iovar_str) + 1);
    }
  };
  std::list<ErrInjCmd> cmds_;
  std::list<ErrInjIovar> iovars_;
  // If set to true this flag injects error (sets rssi to 0) in the rssi signal
  bool enable_rssi_sig_err_ = false;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_ERRINJ_H_
