// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/intel_hda_dsp.fidl INSTEAD.

#pragma once

#include <ddk/protocol/intel-hda-dsp.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_ihda_dsp_protocol_get_dev_info, IhdaDspGetDevInfo,
                                     void (C::*)(zx_pcie_device_info_t* out_out));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_ihda_dsp_protocol_get_mmio, IhdaDspGetMmio,
                                     zx_status_t (C::*)(zx_handle_t* out_vmo, size_t* out_size));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_ihda_dsp_protocol_get_bti, IhdaDspGetBti,
                                     zx_status_t (C::*)(zx_handle_t* out_bti));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_ihda_dsp_protocol_enable, IhdaDspEnable, void (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_ihda_dsp_protocol_disable, IhdaDspDisable, void (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_ihda_dsp_protocol_irq_enable, IhdaDspIrqEnable,
                                     zx_status_t (C::*)(const ihda_dsp_irq_t* callback));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_ihda_dsp_protocol_irq_disable, IhdaDspIrqDisable,
                                     void (C::*)());

template <typename D>
constexpr void CheckIhdaDspProtocolSubclass() {
    static_assert(internal::has_ihda_dsp_protocol_get_dev_info<D>::value,
                  "IhdaDspProtocol subclasses must implement "
                  "void IhdaDspGetDevInfo(zx_pcie_device_info_t* out_out");
    static_assert(internal::has_ihda_dsp_protocol_get_mmio<D>::value,
                  "IhdaDspProtocol subclasses must implement "
                  "zx_status_t IhdaDspGetMmio(zx_handle_t* out_vmo, size_t* out_size");
    static_assert(internal::has_ihda_dsp_protocol_get_bti<D>::value,
                  "IhdaDspProtocol subclasses must implement "
                  "zx_status_t IhdaDspGetBti(zx_handle_t* out_bti");
    static_assert(internal::has_ihda_dsp_protocol_enable<D>::value,
                  "IhdaDspProtocol subclasses must implement "
                  "void IhdaDspEnable(");
    static_assert(internal::has_ihda_dsp_protocol_disable<D>::value,
                  "IhdaDspProtocol subclasses must implement "
                  "void IhdaDspDisable(");
    static_assert(internal::has_ihda_dsp_protocol_irq_enable<D>::value,
                  "IhdaDspProtocol subclasses must implement "
                  "zx_status_t IhdaDspIrqEnable(const ihda_dsp_irq_t* callback");
    static_assert(internal::has_ihda_dsp_protocol_irq_disable<D>::value,
                  "IhdaDspProtocol subclasses must implement "
                  "void IhdaDspIrqDisable(");
}

} // namespace internal
} // namespace ddk
