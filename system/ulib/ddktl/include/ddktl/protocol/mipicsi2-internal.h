// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/mipicsi2 INSTEAD.

#pragma once

#include <ddk/protocol/mipicsi2.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_mipi_csi2protocol_init, MipiCsi2Init,
                                     zx_status_t (C::*)(const mipi_info_t* info));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_mipi_csi2protocol_de_init, MipiCsi2DeInit,
                                     zx_status_t (C::*)());

template <typename D>
constexpr void CheckMipiCsi2ProtocolSubclass() {
    static_assert(internal::has_mipi_csi2protocol_init<D>::value,
                  "MipiCsi2Protocol subclasses must implement "
                  "zx_status_t MipiCsi2Init(const mipi_info_t* info");
    static_assert(internal::has_mipi_csi2protocol_de_init<D>::value,
                  "MipiCsi2Protocol subclasses must implement "
                  "zx_status_t MipiCsi2DeInit(");
}

} // namespace internal
} // namespace ddk
