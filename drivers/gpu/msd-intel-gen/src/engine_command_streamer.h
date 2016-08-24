// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ENGINE_COMMAND_STREAMER_H
#define ENGINE_COMMAND_STREAMER_H

#include "address_space.h"
#include "msd_intel_context.h"
#include "pagetable.h"
#include "register_io.h"
#include "render_init_batch.h"
#include <memory>

class EngineCommandStreamer {
public:
    class Owner {
    public:
        virtual RegisterIo* register_io() = 0;
    };

    EngineCommandStreamer(Owner* owner, EngineCommandStreamerId id, uint32_t mmio_base);

    virtual ~EngineCommandStreamer() {}

    EngineCommandStreamerId id() const { return id_; }

    // Initialize backing store for the given context on this engine command streamer.
    bool InitContext(MsdIntelContext* context) const;

    // Initialize engine command streamer hardware.
    void InitHardware(HardwareStatusPage* hardware_status_page);

protected:
    // from intel-gfx-prm-osrc-bdw-vol03-gpu_overview_3.pdf p.7
    static constexpr uint32_t kRenderEngineMmioBase = 0x2000;

    RegisterIo* register_io() { return owner_->register_io(); }

    uint32_t mmio_base() const { return mmio_base_; }

private:
    // from intel-gfx-prm-osrc-bdw-vol02c-commandreference-registers_4.pdf p.712
    class HardwareStatusPageAddress {
    public:
        static constexpr uint32_t kOffset = 0x80;
        static void write(RegisterIo* reg_io, uint64_t mmio_base, uint32_t addr)
        {
            reg_io->Write32(mmio_base + kOffset, addr);
            reg_io->mmio()->PostingRead32(mmio_base + kOffset);
        }
    };

    virtual uint32_t GetContextSize() const { return PAGE_SIZE * 2; }

    bool InitContextBuffer(MsdIntelBuffer* context_buffer, uint32_t ringbuffer_size) const;

    Owner* owner_;
    EngineCommandStreamerId id_;
    uint32_t mmio_base_;

    friend class TestEngineCommandStreamer;
};

class RenderEngineCommandStreamer : public EngineCommandStreamer {
public:
    // |address_space| used to map the render init batch.
    static std::unique_ptr<RenderEngineCommandStreamer> Create(EngineCommandStreamer::Owner* owner,
                                                               AddressSpace* address_space);

    bool RenderInit(MsdIntelContext* context);

private:
    RenderEngineCommandStreamer(EngineCommandStreamer::Owner* owner,
                                std::unique_ptr<RenderInitBatch> init_batch);

    uint32_t GetContextSize() const override { return PAGE_SIZE * 20; }

    bool StartBatchBuffer(MsdIntelContext* context, uint64_t gpu_addr, bool ppgtt);

    std::unique_ptr<RenderInitBatch> init_batch_;

    friend class TestEngineCommandStreamer;
};

#endif // ENGINE_COMMAND_STREAMER_H
