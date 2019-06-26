// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/debug.h>
#include <ddktl/protocol/codec.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>

namespace audio {
namespace as370 {

struct Codec {
    static constexpr uint32_t kCodecTimeoutSecs = 1;

    zx_status_t GetInfo();

    ddk::CodecProtocolClient proto_client_;
};

} // namespace as370
} // namespace audio
