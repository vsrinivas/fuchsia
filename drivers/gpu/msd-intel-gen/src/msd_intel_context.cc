// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_context.h"

MsdIntelContext::MsdIntelContext() { magic_ = kMagic; }

void MsdIntelContext::InitEngine(EngineCommandStreamerId id,
                                 std::unique_ptr<MsdIntelBuffer> context_buffer)
{
    DASSERT(context_buffer);

    auto iter = context_buffer_map_.find(id);
    DASSERT(iter == context_buffer_map_.end());

    context_buffer_map_[id] = std::move(context_buffer);
}
