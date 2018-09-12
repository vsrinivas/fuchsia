// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/sdio.banjo INSTEAD.

#pragma once

#include <ddk/protocol/sdio.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdio_protocol_get_dev_hw_info, SdioGetDevHwInfo,
                                     zx_status_t (C::*)(sdio_hw_info_t* out_hw_info));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdio_protocol_enable_fn, SdioEnableFn,
                                     zx_status_t (C::*)(uint8_t fn_idx));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdio_protocol_disable_fn, SdioDisableFn,
                                     zx_status_t (C::*)(uint8_t fn_idx));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdio_protocol_enable_fn_intr, SdioEnableFnIntr,
                                     zx_status_t (C::*)(uint8_t fn_idx));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdio_protocol_disable_fn_intr, SdioDisableFnIntr,
                                     zx_status_t (C::*)(uint8_t fn_idx));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdio_protocol_update_block_size, SdioUpdateBlockSize,
                                     zx_status_t (C::*)(uint8_t fn_idx, uint16_t blk_sz,
                                                        bool deflt));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdio_protocol_get_block_size, SdioGetBlockSize,
                                     zx_status_t (C::*)(uint8_t fn_idx,
                                                        uint16_t* out_cur_blk_size));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdio_protocol_do_rw_txn, SdioDoRwTxn,
                                     zx_status_t (C::*)(uint8_t fn_idx, sdio_rw_txn_t* txn));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_sdio_protocol_do_rw_byte, SdioDoRwByte,
                                     zx_status_t (C::*)(bool write, uint8_t fn_idx, uint32_t addr,
                                                        uint8_t write_byte,
                                                        uint8_t* out_read_byte));

template <typename D>
constexpr void CheckSdioProtocolSubclass() {
    static_assert(internal::has_sdio_protocol_get_dev_hw_info<D>::value,
                  "SdioProtocol subclasses must implement "
                  "zx_status_t SdioGetDevHwInfo(sdio_hw_info_t* out_hw_info");
    static_assert(internal::has_sdio_protocol_enable_fn<D>::value,
                  "SdioProtocol subclasses must implement "
                  "zx_status_t SdioEnableFn(uint8_t fn_idx");
    static_assert(internal::has_sdio_protocol_disable_fn<D>::value,
                  "SdioProtocol subclasses must implement "
                  "zx_status_t SdioDisableFn(uint8_t fn_idx");
    static_assert(internal::has_sdio_protocol_enable_fn_intr<D>::value,
                  "SdioProtocol subclasses must implement "
                  "zx_status_t SdioEnableFnIntr(uint8_t fn_idx");
    static_assert(internal::has_sdio_protocol_disable_fn_intr<D>::value,
                  "SdioProtocol subclasses must implement "
                  "zx_status_t SdioDisableFnIntr(uint8_t fn_idx");
    static_assert(internal::has_sdio_protocol_update_block_size<D>::value,
                  "SdioProtocol subclasses must implement "
                  "zx_status_t SdioUpdateBlockSize(uint8_t fn_idx, uint16_t blk_sz, bool deflt");
    static_assert(internal::has_sdio_protocol_get_block_size<D>::value,
                  "SdioProtocol subclasses must implement "
                  "zx_status_t SdioGetBlockSize(uint8_t fn_idx, uint16_t* out_cur_blk_size");
    static_assert(internal::has_sdio_protocol_do_rw_txn<D>::value,
                  "SdioProtocol subclasses must implement "
                  "zx_status_t SdioDoRwTxn(uint8_t fn_idx, sdio_rw_txn_t* txn");
    static_assert(internal::has_sdio_protocol_do_rw_byte<D>::value,
                  "SdioProtocol subclasses must implement "
                  "zx_status_t SdioDoRwByte(bool write, uint8_t fn_idx, uint32_t addr, uint8_t "
                  "write_byte, uint8_t* out_read_byte");
}

} // namespace internal
} // namespace ddk
