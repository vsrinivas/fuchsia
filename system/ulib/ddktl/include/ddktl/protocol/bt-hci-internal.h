// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/bt_hci.banjo INSTEAD.

#pragma once

#include <ddk/protocol/bt-hci.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_bt_hci_protocol_open_command_channel,
                                     BtHciOpenCommandChannel,
                                     zx_status_t (C::*)(zx_handle_t* out_channel));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_bt_hci_protocol_open_acl_data_channel,
                                     BtHciOpenAclDataChannel,
                                     zx_status_t (C::*)(zx_handle_t* out_channel));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_bt_hci_protocol_open_snoop_channel, BtHciOpenSnoopChannel,
                                     zx_status_t (C::*)(zx_handle_t* out_channel));

template <typename D>
constexpr void CheckBtHciProtocolSubclass() {
    static_assert(internal::has_bt_hci_protocol_open_command_channel<D>::value,
                  "BtHciProtocol subclasses must implement "
                  "zx_status_t BtHciOpenCommandChannel(zx_handle_t* out_channel");
    static_assert(internal::has_bt_hci_protocol_open_acl_data_channel<D>::value,
                  "BtHciProtocol subclasses must implement "
                  "zx_status_t BtHciOpenAclDataChannel(zx_handle_t* out_channel");
    static_assert(internal::has_bt_hci_protocol_open_snoop_channel<D>::value,
                  "BtHciProtocol subclasses must implement "
                  "zx_status_t BtHciOpenSnoopChannel(zx_handle_t* out_channel");
}

} // namespace internal
} // namespace ddk
