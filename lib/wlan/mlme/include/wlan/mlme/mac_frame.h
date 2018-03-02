// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/sequence.h>

#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <wlan/common/bitfield.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/macaddr.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zx/time.h>

#include <cstdint>

namespace wlan {

class Packet;

template <typename Body>
MgmtFrame<Body> BuildMgmtFrame(fbl::unique_ptr<Packet>* packet, size_t body_payload_len = 0,
                               bool has_ht_ctrl = false);

zx_status_t FillTxInfo(fbl::unique_ptr<Packet>* packet, const MgmtFrameHeader& hdr);

seq_t NextSeqNo(const MgmtFrameHeader& hdr, Sequence* seq);
seq_t NextSeqNo(const MgmtFrameHeader& hdr, uint8_t aci, Sequence* seq);
seq_t NextSeqNo(const DataFrameHeader& hdr, Sequence* seq);

void SetSeqNo(MgmtFrameHeader* hdr, Sequence* seq);
void SetSeqNo(MgmtFrameHeader* hdr, uint8_t aci, Sequence* seq);
void SetSeqNo(DataFrameHeader* hdr, Sequence* seq);
}  // namespace wlan
