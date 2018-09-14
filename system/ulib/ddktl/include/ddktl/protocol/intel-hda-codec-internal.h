// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/intel_hda_codec.fidl INSTEAD.

#pragma once

#include <ddk/protocol/intel-hda-codec.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_ihda_codec_protocol_get_driver_channel,
                                     IhdaCodecGetDriverChannel,
                                     zx_status_t (C::*)(zx_handle_t* out_channel));

template <typename D>
constexpr void CheckIhdaCodecProtocolSubclass() {
    static_assert(internal::has_ihda_codec_protocol_get_driver_channel<D>::value,
                  "IhdaCodecProtocol subclasses must implement "
                  "zx_status_t IhdaCodecGetDriverChannel(zx_handle_t* out_channel");
}

} // namespace internal
} // namespace ddk
