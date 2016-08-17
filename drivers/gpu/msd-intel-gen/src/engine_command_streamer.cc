// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine_command_streamer.h"
#include "magma_util/macros.h"
#include "msd_intel_buffer.h"
#include "ringbuffer.h"

bool EngineCommandStreamer::InitContext(MsdIntelContext* context)
{
    DASSERT(context);

    uint32_t context_size = GetContextSize();
    DASSERT(context_size > 0 && magma::is_page_aligned(context_size));

    std::unique_ptr<MsdIntelBuffer> context_buffer(MsdIntelBuffer::Create(context_size));
    if (!context_buffer)
        return DRETF(false, "couldn't create context buffer");

    std::unique_ptr<Ringbuffer> ringbuffer(Ringbuffer::Create());
    if (!ringbuffer)
        return DRETF(false, "couldn't create ringbuffer");

    // Transfer ownership of context_buffer
    context->SetEngineState(id(), std::move(context_buffer), std::move(ringbuffer));

    return true;
}
