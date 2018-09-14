// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/pciroot.fidl INSTEAD.

#pragma once

#include <ddk/protocol/pciroot.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_get_auxdata, PcirootGetAuxdata,
                                     zx_status_t (C::*)(const char* args, void* out_data_buffer,
                                                        size_t data_size, size_t* out_data_actual));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_get_bti, PcirootGetBti,
                                     zx_status_t (C::*)(uint32_t bdf, uint32_t index,
                                                        zx_handle_t* out_bti));

template <typename D>
constexpr void CheckPcirootProtocolSubclass() {
    static_assert(internal::has_pciroot_protocol_get_auxdata<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootGetAuxdata(const char* args, void* out_data_buffer, size_t "
                  "data_size, size_t* out_data_actual");
    static_assert(internal::has_pciroot_protocol_get_bti<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootGetBti(uint32_t bdf, uint32_t index, zx_handle_t* out_bti");
}

} // namespace internal
} // namespace ddk
