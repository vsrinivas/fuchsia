// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_MLAN_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_MLAN_H_

// This include file just wraps mlan_decl.h so we don't need extern "C" everywhere.

#ifdef __cplusplus
extern "C" {
#endif

#pragma push_macro("MACSTR")
#undef MACSTR
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan/mlan_decl.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan/mlan_ioctl.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan/mlan_ieee.h"
#pragma pop_macro("MACSTR")

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_MLAN_H_
