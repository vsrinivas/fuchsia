// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device-internal.h>
#include <zircon/types.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>

#include <stdint.h>

namespace ddk {

class EthmacIfcProxy;

namespace internal {

DECLARE_HAS_MEMBER_FN(has_ethmac_status, EthmacStatus);
DECLARE_HAS_MEMBER_FN(has_ethmac_recv, EthmacRecv);
DECLARE_HAS_MEMBER_FN(has_ethmac_complete_tx, EthmacCompleteTx);

template <typename D>
constexpr void CheckEthmacIfc() {
    static_assert(internal::has_ethmac_status<D>::value,
                  "EthmacIfc subclasses must implement EthmacStatus");
    static_assert(fbl::is_same<decltype(&D::EthmacStatus),
                                void (D::*)(uint32_t)>::value,
                  "EthmacStatus must be a non-static member function with signature "
                  "'void EthmacStatus(uint32_t)', and be visible to ddk::EthmacIfc<D> "
                  "(either because they are public, or because of friendship).");
    static_assert(internal::has_ethmac_recv<D>::value,
                  "EthmacIfc subclasses must implement EthmacRecv");
    static_assert(fbl::is_same<decltype(&D::EthmacRecv),
                                void (D::*)(void*, size_t, uint32_t)>::value,
                  "EthmacQuery must be a non-static member function with signature "
                  "'void EthmacRecv(void*, size_t, uint32_t)', and be visible to "
                  "ddk::EthmacIfc<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_ethmac_complete_tx<D>::value,
                  "EthmacIfc subclasses must implement EthmacCompleteTx");
    static_assert(fbl::is_same<decltype(&D::EthmacCompleteTx),
                                void (D::*)(ethmac_netbuf_t*, zx_status_t)>::value,
                  "EthmacCompleteTx must be a non-static member function with signature "
                  "'void EthmacCompleteTx(ethmac_netbuf_t*, zx_status_t)', and be visible to "
                  "ddk::EthmacIfc<D> (either because they are public, or because of friendship).");
}

DECLARE_HAS_MEMBER_FN(has_ethmac_query, EthmacQuery);
DECLARE_HAS_MEMBER_FN(has_ethmac_stop, EthmacStop);
DECLARE_HAS_MEMBER_FN(has_ethmac_start, EthmacStart);
DECLARE_HAS_MEMBER_FN(has_ethmac_queue_tx, EthmacQueueTx);
DECLARE_HAS_MEMBER_FN(has_ethmac_set_param, EthmacSetParam);
DECLARE_HAS_MEMBER_FN(has_ethmac_get_bti, EthmacGetBti);

template <typename D>
constexpr void CheckEthmacProtocolSubclass() {
    static_assert(internal::has_ethmac_query<D>::value,
                  "EthmacProtocol subclasses must implement EthmacQuery");
    static_assert(fbl::is_same<decltype(&D::EthmacQuery),
                                zx_status_t (D::*)(uint32_t, ethmac_info_t*)>::value,
                  "EthmacQuery must be a non-static member function with signature "
                  "'zx_status_t EthmacQuery(uint32_t, ethmac_info_t*)', and be visible to "
                  "ddk::EthmacProtocol<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_ethmac_stop<D>::value,
                  "EthmacProtocol subclasses must implement EthmacStop");
    static_assert(fbl::is_same<decltype(&D::EthmacStop),
                                void (D::*)()>::value,
                  "EthmacStop must be a non-static member function with signature "
                  "'void EthmacStop()', and be visible to ddk::EthmacProtocol<D> (either "
                  "because they are public, or because of friendship).");
    static_assert(internal::has_ethmac_start<D>::value,
                  "EthmacProtocol subclasses must implement EthmacStart");
    static_assert(fbl::is_same<decltype(&D::EthmacStart),
                                zx_status_t (D::*)(fbl::unique_ptr<EthmacIfcProxy>)>::value,
                  "EthmacStart must be a non-static member function with signature "
                  "'zx_status_t EthmacStart(fbl::unique_ptr<EthmacIfcProxy>)', and be visible "
                  "to ddk::EthmacProtocol<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_ethmac_queue_tx<D>::value,
                  "EthmacProtocol subclasses must implement EthmacQueueTx");
    static_assert(fbl::is_same<decltype(&D::EthmacQueueTx),
                                zx_status_t (D::*)(uint32_t, ethmac_netbuf_t*)>::value,
                  "EthmacQueueTx must be a non-static member function with signature "
                  "'zx_status_t EthmacQueueTx(uint32_t, ethmac_netbuf_t*)', and be visible to "
                  "ddk::EthmacProtocol<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_ethmac_set_param<D>::value,
                  "EthmacProtocol subclasses must implement EthmacSetParam");
    static_assert(fbl::is_same<decltype(&D::EthmacSetParam),
                                zx_status_t (D::*)(uint32_t, int32_t, void*)>::value,
                  "EthmacSetParam must be a non-static member function with signature "
                  "'zx_status_t EthmacSetParam(uint32_t, int32_t, void*)', and be visible to "
                  "ddk::EthmacProtocol<D> (either because they are public, or because of "
                  "friendship).");
    static_assert(internal::has_ethmac_get_bti<D>::value,
                  "EthmacProtocol subclasses must implement EthmacGetBti");
    static_assert(fbl::is_same<decltype(&D::EthmacGetBti),
                                zx_handle_t (D::*)()>::value,
                  "EthmacGetBti must be a non-static member function with signature "
                  "'zx_handle_t EthmacGetBti()', and be visible to ddk::EthmacProtocol<D> "
                  "(either because they are public, or because of friendship).");
}

}  // namespace internal
}  // namespace ddk
