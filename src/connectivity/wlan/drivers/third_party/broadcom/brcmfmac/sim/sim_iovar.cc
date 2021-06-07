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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/sim_fw.h"

namespace wlan::brcmfmac {

SimIovar::SimIovar(std::optional<size_t> value_len, SimFirmware* caller_fw,
                   IovarSetHandler set_func, IovarGetHandler get_func,
                   std::optional<size_t> iftbl_offset, std::optional<void*> var_addr)
    : value_len_(value_len),
      caller_fw_(caller_fw),
      set_func_(set_func),
      get_func_(get_func),
      iftbl_offset_(iftbl_offset),
      var_addr_(var_addr) {}

zx_status_t SimIovar::Set(uint16_t ifidx, int32_t bsscfgidx, const void* value, size_t value_len) {
  if (set_func_ == nullptr) {
    // FIXME: For now, just pretend that we successfully set the value even when we did nothing.
    BRCMF_ERR("Set function of this iovar is not registered.");
    return ZX_OK;
  }

  // If the benchmark(value_len_) exists and the input value_len is smaller than it, return an
  // error.
  if (value_len_.has_value() && value_len < value_len_) {
    BRCMF_ERR("Value length does not match in set function.");
    return ZX_ERR_IO_REFUSED;
  }
  SimIovarSetReq req = {.ifidx = ifidx,
                        .bsscfgidx = bsscfgidx,
                        .value = value,
                        .value_len = value_len,
                        .iftbl_offset = iftbl_offset_,
                        .var_addr = var_addr_};

  return (caller_fw_->*set_func_)(&req);
}

zx_status_t SimIovar::Get(uint16_t ifidx, void* value, size_t value_len) {
  if (get_func_ == nullptr) {
    // FIXME: We should return an error for an unrecognized firmware variable.
    BRCMF_ERR("Get function of this iovar is not registered.");
    memset(value, 0, value_len);
    return ZX_OK;
  }

  // If the benchmark(value_len_) exists the input buffer length(value_len) is smaller than it,
  // return an error.
  if (value_len_.has_value() && value_len < value_len_) {
    BRCMF_ERR("Value length does not match in get function.");
    return ZX_ERR_IO_REFUSED;
  }

  SimIovarGetReq req = {.ifidx = ifidx,
                        .value = value,
                        .value_len = value_len,
                        .iftbl_offset = iftbl_offset_,
                        .var_addr = var_addr_};
  return (caller_fw_->*get_func_)(&req);
}

}  // namespace wlan::brcmfmac
