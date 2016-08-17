// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ENGINE_COMMAND_STREAMER_H
#define ENGINE_COMMAND_STREAMER_H

#include "msd_intel_context.h"
#include "pagetable.h"
#include <memory>

class EngineCommandStreamer {
public:
    EngineCommandStreamer(EngineCommandStreamerId id, uint32_t mmio_base)
        : id_(id), mmio_base_(mmio_base)
    {
    }

    virtual ~EngineCommandStreamer() {}

    EngineCommandStreamerId id() { return id_; }

    bool InitContext(MsdIntelContext* context);

protected:
    // from intel-gfx-prm-osrc-bdw-vol03-gpu_overview_3.pdf p.7
    static constexpr uint32_t kRenderEngineMmioBase = 0x2000;

private:
    virtual uint32_t GetContextSize() { return PAGE_SIZE * 2; }

    bool InitContextBuffer(MsdIntelBuffer* context_buffer, uint32_t ringbuffer_size);

    EngineCommandStreamerId id_;
    uint32_t mmio_base_;
};

class RenderEngineCommandStreamer : public EngineCommandStreamer {
public:
    RenderEngineCommandStreamer()
        : EngineCommandStreamer(RENDER_COMMAND_STREAMER, kRenderEngineMmioBase)
    {
    }

private:
    uint32_t GetContextSize() override { return PAGE_SIZE * 20; }
};

#endif // ENGINE_COMMAND_STREAMER_H
