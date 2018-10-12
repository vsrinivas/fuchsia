// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/mipicsi.banjo INSTEAD.

#pragma once

#include <ddk/protocol/mipicsi.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_mipi_csi_protocol_init, MipiCsiInit,
                                     zx_status_t (C::*)(const mipi_info_t* info));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_mipi_csi_protocol_de_init, MipiCsiDeInit,
                                     zx_status_t (C::*)());

template <typename D>
constexpr void CheckMipiCsiProtocolSubclass() {
    static_assert(internal::has_mipi_csi_protocol_init<D>::value,
                  "MipiCsiProtocol subclasses must implement "
                  "zx_status_t MipiCsiInit(const mipi_info_t* info");
    static_assert(internal::has_mipi_csi_protocol_de_init<D>::value,
                  "MipiCsiProtocol subclasses must implement "
                  "zx_status_t MipiCsiDeInit(");
}

} // namespace internal
} // namespace ddk
