// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/rawnand.banjo INSTEAD.

#pragma once

#include <ddk/protocol/rawnand.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_raw_nand_protocol_read_page_hwecc, RawNandReadPageHwecc,
                                     zx_status_t (C::*)(uint32_t nandpage, void* out_data_buffer,
                                                        size_t data_size, size_t* out_data_actual,
                                                        void* out_oob_buffer, size_t oob_size,
                                                        size_t* out_oob_actual,
                                                        uint32_t* out_ecc_correct));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_raw_nand_protocol_write_page_hwecc, RawNandWritePageHwecc,
                                     zx_status_t (C::*)(const void* data_buffer, size_t data_size,
                                                        const void* oob_buffer, size_t oob_size,
                                                        uint32_t nandpage));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_raw_nand_protocol_erase_block, RawNandEraseBlock,
                                     zx_status_t (C::*)(uint32_t nandpage));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_raw_nand_protocol_get_nand_info, RawNandGetNandInfo,
                                     zx_status_t (C::*)(nand_info_t* out_info));

template <typename D>
constexpr void CheckRawNandProtocolSubclass() {
    static_assert(internal::has_raw_nand_protocol_read_page_hwecc<D>::value,
                  "RawNandProtocol subclasses must implement "
                  "zx_status_t RawNandReadPageHwecc(uint32_t nandpage, void* out_data_buffer, "
                  "size_t data_size, size_t* out_data_actual, void* out_oob_buffer, size_t "
                  "oob_size, size_t* out_oob_actual, uint32_t* out_ecc_correct");
    static_assert(internal::has_raw_nand_protocol_write_page_hwecc<D>::value,
                  "RawNandProtocol subclasses must implement "
                  "zx_status_t RawNandWritePageHwecc(const void* data_buffer, size_t data_size, "
                  "const void* oob_buffer, size_t oob_size, uint32_t nandpage");
    static_assert(internal::has_raw_nand_protocol_erase_block<D>::value,
                  "RawNandProtocol subclasses must implement "
                  "zx_status_t RawNandEraseBlock(uint32_t nandpage");
    static_assert(internal::has_raw_nand_protocol_get_nand_info<D>::value,
                  "RawNandProtocol subclasses must implement "
                  "zx_status_t RawNandGetNandInfo(nand_info_t* out_info");
}

} // namespace internal
} // namespace ddk
