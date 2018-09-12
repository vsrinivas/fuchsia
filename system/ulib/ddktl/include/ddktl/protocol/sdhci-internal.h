// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/sdhci.banjo INSTEAD.

#pragma once

#include <ddk/protocol/sdhci.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdhci_protocol_get_interrupt, SdhciGetInterrupt,
                                     zx_status_t (C::*)(zx_handle_t* out_irq));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdhci_protocol_get_mmio, SdhciGetMmio,
                                     zx_status_t (C::*)(zx_handle_t* out_mmio));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdhci_protocol_get_bti, SdhciGetBti,
                                     zx_status_t (C::*)(uint32_t index, zx_handle_t* out_bti));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdhci_protocol_get_base_clock, SdhciGetBaseClock,
                                     uint32_t (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdhci_protocol_get_quirks, SdhciGetQuirks,
                                     uint64_t (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdhci_protocol_hw_reset, SdhciHwReset, void (C::*)());

template <typename D>
constexpr void CheckSdhciProtocolSubclass() {
    static_assert(internal::has_sdhci_protocol_get_interrupt<D>::value,
                  "SdhciProtocol subclasses must implement "
                  "zx_status_t SdhciGetInterrupt(zx_handle_t* out_irq");
    static_assert(internal::has_sdhci_protocol_get_mmio<D>::value,
                  "SdhciProtocol subclasses must implement "
                  "zx_status_t SdhciGetMmio(zx_handle_t* out_mmio");
    static_assert(internal::has_sdhci_protocol_get_bti<D>::value,
                  "SdhciProtocol subclasses must implement "
                  "zx_status_t SdhciGetBti(uint32_t index, zx_handle_t* out_bti");
    static_assert(internal::has_sdhci_protocol_get_base_clock<D>::value,
                  "SdhciProtocol subclasses must implement "
                  "uint32_t SdhciGetBaseClock(");
    static_assert(internal::has_sdhci_protocol_get_quirks<D>::value,
                  "SdhciProtocol subclasses must implement "
                  "uint64_t SdhciGetQuirks(");
    static_assert(internal::has_sdhci_protocol_hw_reset<D>::value,
                  "SdhciProtocol subclasses must implement "
                  "void SdhciHwReset(");
}

} // namespace internal
} // namespace ddk
