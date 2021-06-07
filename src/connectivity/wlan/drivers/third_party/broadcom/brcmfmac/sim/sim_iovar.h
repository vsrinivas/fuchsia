/*
 * Copyright (c) 2021 The Fuchsia Authors
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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_IOVAR_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_IOVAR_H_

#include <sys/types.h>
#include <zircon/types.h>

#include <functional>
#include <optional>
#include <utility>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_fw.h"

namespace wlan::brcmfmac {

class SimFirmware;

// Iovar Set Req
struct SimIovarSetReq {
  uint16_t ifidx;
  int32_t bsscfgidx;
  const void* value;
  size_t value_len;
  std::optional<size_t> iftbl_offset;
  std::optional<void*> var_addr;
};

// Iovar Get Req
struct SimIovarGetReq {
  uint16_t ifidx;
  void* value;
  size_t value_len;
  std::optional<size_t> iftbl_offset;
  std::optional<void*> var_addr;
};

using IovarSetHandler = zx_status_t (SimFirmware::*)(SimIovarSetReq*);
using IovarGetHandler = zx_status_t (SimFirmware::*)(SimIovarGetReq*);

class SimIovar {
 public:
  SimIovar(std::optional<size_t> value_len, SimFirmware* caller_fw,
           IovarSetHandler set_func = nullptr, IovarGetHandler get_func = nullptr,
           std::optional<size_t> iftbl_offset = std::nullopt,
           std::optional<void*> var_addr = std::nullopt);

  zx_status_t Set(uint16_t ifidx, int32_t bsscfgidx, const void* value, size_t value_len);
  zx_status_t Get(uint16_t ifidx, void* value, size_t value_len);

 private:
  // The target value len for both get and set if there is a value that needs to be stored.
  const std::optional<size_t> value_len_;

  // The pointer to the owner of the handler functions
  SimFirmware* caller_fw_;
  const IovarSetHandler set_func_;
  const IovarGetHandler get_func_;
  std::optional<size_t> iftbl_offset_ = std::nullopt;
  std::optional<void*> var_addr_ = std::nullopt;
};

}  // namespace wlan::brcmfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_SIM_SIM_IOVAR_H_
