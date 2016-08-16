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
    EngineCommandStreamer(EngineCommandStreamerId id) : id_(id) {}

    virtual ~EngineCommandStreamer() {}

    EngineCommandStreamerId id() { return id_; }

    bool InitContext(MsdIntelContext* context);

private:
    virtual uint32_t GetContextSize() { return PAGE_SIZE * 2; }

    EngineCommandStreamerId id_;
};

class RenderEngineCommandStreamer : public EngineCommandStreamer {
public:
    RenderEngineCommandStreamer() : EngineCommandStreamer(RENDER_COMMAND_STREAMER) {}

private:
    uint32_t GetContextSize() override { return PAGE_SIZE * 20; }
};

#endif // ENGINE_COMMAND_STREAMER_H
