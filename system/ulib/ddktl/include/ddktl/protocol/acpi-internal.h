// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/acpi.banjo INSTEAD.

#pragma once

#include <ddk/protocol/acpi.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_acpi_protocol_map_resource, AcpiMapResource,
                                     zx_status_t (C::*)(uint32_t resource_id, uint32_t cache_policy,
                                                        void** out_vaddr_buffer, size_t* vaddr_size,
                                                        zx_handle_t* out_handle));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_acpi_protocol_map_interrupt, AcpiMapInterrupt,
                                     zx_status_t (C::*)(int64_t irq_id, zx_handle_t* out_handle));

template <typename D>
constexpr void CheckAcpiProtocolSubclass() {
    static_assert(internal::has_acpi_protocol_map_resource<D>::value,
                  "AcpiProtocol subclasses must implement "
                  "zx_status_t AcpiMapResource(uint32_t resource_id, uint32_t cache_policy, void** "
                  "out_vaddr_buffer, size_t* vaddr_size, zx_handle_t* out_handle");
    static_assert(internal::has_acpi_protocol_map_interrupt<D>::value,
                  "AcpiProtocol subclasses must implement "
                  "zx_status_t AcpiMapInterrupt(int64_t irq_id, zx_handle_t* out_handle");
}

} // namespace internal
} // namespace ddk
