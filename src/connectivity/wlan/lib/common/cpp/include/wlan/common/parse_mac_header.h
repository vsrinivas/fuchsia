// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>
#include <wlan/common/buffer_reader.h>
#include <wlan/common/mac_frame.h>

namespace wlan {
namespace common {

struct ParsedDataFrameHeader {
    const DataFrameHeader* fixed;
    const common::MacAddr* addr4; // nullable
    const QosControl* qos_ctrl; // nullable
    const HtControl* ht_ctrl; // nullable
};

std::optional<ParsedDataFrameHeader> ParseDataFrameHeader(BufferReader* r);


struct ParsedMeshDataHeader {
    ParsedDataFrameHeader mac_header;
    const MeshControl* mesh_ctrl;
    Span<const common::MacAddr> addr_ext; // length 0, 1 or 2
    const LlcHeader* llc;
};

std::optional<ParsedMeshDataHeader> ParseMeshDataHeader(BufferReader* r);

} // namespace common
} // namespace wlan
