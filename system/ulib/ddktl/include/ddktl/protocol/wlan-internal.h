// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device-internal.h>
#include <magenta/types.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>

#include <stdint.h>

namespace ddk {

class WlanmacIfcProxy;

namespace internal {

DECLARE_HAS_MEMBER_FN(has_wlanmac_status, WlanmacStatus);
DECLARE_HAS_MEMBER_FN(has_wlanmac_recv, WlanmacRecv);

template <typename D>
constexpr void CheckWlanmacIfc() {
    static_assert(internal::has_wlanmac_status<D>::value,
                  "WlanmacIfc subclasses must implement WlanmacStatus");
    static_assert(fbl::is_same<decltype(&D::WlanmacStatus),
                                void (D::*)(uint32_t)>::value,
                  "WlanmacStatus must be a non-static member function with signature "
                  "'void WlanmacStatus(uint32_t)', and be visible to ddk::WlanmacIfc<D> "
                  "(either because they are public, or because of friendship).");
    static_assert(internal::has_wlanmac_recv<D>::value,
                  "WlanmacIfc subclasses must implement WlanmacRecv");
    static_assert(fbl::is_same<decltype(&D::WlanmacRecv),
                                void (D::*)(uint32_t, const void*, size_t, wlan_rx_info_t*)>::value,
                  "WlanmacQuery must be a non-static member function with signature "
                  "'void WlanmacRecv(uint32_t, const void*, size_t, wlan_rx_info_t*)', and be "
                  "visible to ddk::WlanmacIfc<D> (either because they are public, or because of "
                  "friendship).");
}

DECLARE_HAS_MEMBER_FN(has_wlanmac_query, WlanmacQuery);
DECLARE_HAS_MEMBER_FN(has_wlanmac_stop, WlanmacStop);
DECLARE_HAS_MEMBER_FN(has_wlanmac_start, WlanmacStart);
DECLARE_HAS_MEMBER_FN(has_wlanmac_send, WlanmacTx);
DECLARE_HAS_MEMBER_FN(has_wlanmac_set_channel, WlanmacSetChannel);

template <typename D>
constexpr void CheckWlanmacProtocolSubclass() {
    static_assert(internal::has_wlanmac_query<D>::value,
                  "WlanmacProtocol subclasses must implement WlanmacQuery");
    static_assert(fbl::is_same<decltype(&D::WlanmacQuery),
                                mx_status_t (D::*)(uint32_t, ethmac_info_t*)>::value,
                  "WlanmacQuery must be a non-static member function with signature "
                  "'mx_status_t WlanmacQuery(uint32_t, ethmac_info_t*)', and be visible to "
                  "ddk::WlanmacProtocol<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_wlanmac_stop<D>::value,
                  "WlanmacProtocol subclasses must implement WlanmacStop");
    static_assert(fbl::is_same<decltype(&D::WlanmacStop),
                                void (D::*)()>::value,
                  "WlanmacStop must be a non-static member function with signature "
                  "'void WlanmacStop()', and be visible to ddk::WlanmacProtocol<D> (either "
                  "because they are public, or because of friendship).");
    static_assert(internal::has_wlanmac_start<D>::value,
                  "WlanmacProtocol subclasses must implement WlanmacStart");
    static_assert(fbl::is_same<decltype(&D::WlanmacStart),
                                mx_status_t (D::*)(fbl::unique_ptr<WlanmacIfcProxy>)>::value,
                  "WlanmacStart must be a non-static member function with signature "
                  "'mx_status_t WlanmacStart(fbl::unique_ptr<WlanmacIfcProxy>)', and be visible "
                  "to ddk::WlanmacProtocol<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_wlanmac_send<D>::value,
                  "WlanmacProtocol subclasses must implement WlanmacTx");
    static_assert(fbl::is_same<decltype(&D::WlanmacTx),
                                void (D::*)(uint32_t, const void*, size_t)>::value,
                  "WlanmacTx must be a non-static member function with signature "
                  "'mx_status_t WlanmacTx(uint32_t, const void*, size_t)', and be visible to "
                  "ddk::WlanmacProtocol<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_wlanmac_set_channel<D>::value,
                  "WlanmacProtocol subclasses must implement WlanmacSetChannel");
    static_assert(fbl::is_same<decltype(&D::WlanmacSetChannel),
                                mx_status_t (D::*)(uint32_t, wlan_channel_t*)>::value,
                  "WlanmacSetChannel must be a non-static member function with signature "
                  "'mx_status_t WlanmacSetChannel(uint32_t, wlan_channel_t*)', and be visible to "
                  "ddk::WlanmacProtocol<D> (either because they are public, or because of "
                  "friendship).");
}

}  // namespace internal
}  // namespace ddk
