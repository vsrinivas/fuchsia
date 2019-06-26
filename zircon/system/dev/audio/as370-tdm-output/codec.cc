// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec.h"

namespace audio {
namespace as370 {

zx_status_t Codec::GetInfo() {
    sync_completion_t completion;
    proto_client_.GetInfo(
        [](void* ctx, const info_t* info) {
            auto* completion = reinterpret_cast<sync_completion_t*>(ctx);
            zxlogf(INFO, "audio: Found codec %s by %s\n", info->product_name, info->manufacturer);
            sync_completion_signal(completion);
        },
        &completion);
    auto status = sync_completion_wait(&completion, zx::sec(kCodecTimeoutSecs).get());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s sync_completion_wait failed %d\n", __func__, status);
    }
    return status;
}

} // namespace as370
} // namespace audio
