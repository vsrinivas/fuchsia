// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ENGINE_COMMAND_STREAMER_H
#define ENGINE_COMMAND_STREAMER_H

#include "address_space.h"
#include "hardware_status_page.h"
#include "mapped_batch.h"
#include "msd_intel_context.h"
#include "pagetable.h"
#include "register_io.h"
#include "render_init_batch.h"
#include "sequencer.h"
#include <memory>
#include <queue>

class EngineCommandStreamer {
public:
    class Owner {
    public:
        virtual RegisterIo* register_io() = 0;
        virtual Sequencer* sequencer() = 0;
        virtual HardwareStatusPage* hardware_status_page(EngineCommandStreamerId id) = 0;
    };

    EngineCommandStreamer(Owner* owner, EngineCommandStreamerId id, uint32_t mmio_base);

    virtual ~EngineCommandStreamer() {}

    EngineCommandStreamerId id() const { return id_; }

    // Initialize backing store for the given context on this engine command streamer.
    bool InitContext(MsdIntelContext* context) const;

    // Initialize engine command streamer hardware.
    void InitHardware();

    uint64_t GetActiveHeadPointer();

    virtual bool ExecuteCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf,
                                      std::shared_ptr<AddressSpace> ggtt,
                                      uint32_t* sequence_number_out) = 0;

    virtual bool WaitIdle() = 0;

protected:
    bool SubmitContext(MsdIntelContext* context);
    bool UpdateContext(MsdIntelContext* context);
    void SubmitExeclists(MsdIntelContext* context);
    bool PipeControl(MsdIntelContext* context, uint32_t flags);

    // from intel-gfx-prm-osrc-bdw-vol03-gpu_overview_3.pdf p.7
    static constexpr uint32_t kRenderEngineMmioBase = 0x2000;

    RegisterIo* register_io() { return owner_->register_io(); }

    uint32_t mmio_base() const { return mmio_base_; }

    Sequencer* sequencer() { return owner_->sequencer(); }

    HardwareStatusPage* hardware_status_page(EngineCommandStreamerId id)
    {
        return owner_->hardware_status_page(id);
    }

private:
    virtual uint32_t GetContextSize() const { return PAGE_SIZE * 2; }

    bool InitContextBuffer(MsdIntelBuffer* context_buffer, Ringbuffer* ringbuffer) const;

    Owner* owner_;
    EngineCommandStreamerId id_;
    uint32_t mmio_base_;

    friend class TestEngineCommandStreamer;
};

class RenderEngineCommandStreamer : public EngineCommandStreamer {
public:
    static std::unique_ptr<RenderEngineCommandStreamer> Create(EngineCommandStreamer::Owner* owner);

    static std::unique_ptr<RenderInitBatch> CreateRenderInitBatch(uint32_t device_id);

    // |address_space| used to map the render init batch.
    bool RenderInit(std::shared_ptr<MsdIntelContext> context,
                    std::unique_ptr<RenderInitBatch> init_batch,
                    std::shared_ptr<AddressSpace> address_space);

    bool ExecuteCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf,
                              std::shared_ptr<AddressSpace> ggtt,
                              uint32_t* sequence_number_out) override;

    void ProcessCompletedCommandBuffers(uint32_t* last_completed_sequence_number_out);

    bool WaitIdle() override;

private:
    RenderEngineCommandStreamer(EngineCommandStreamer::Owner* owner);

    uint32_t GetContextSize() const override { return PAGE_SIZE * 20; }

    bool ExecBatch(std::unique_ptr<MappedBatch> mapped_batch, uint32_t pipe_control_flags,
                   uint32_t* sequence_number_out);

    bool StartBatchBuffer(MsdIntelContext* context, uint64_t gpu_addr,
                          AddressSpaceId address_space_id);
    bool WriteSequenceNumber(MsdIntelContext* context, uint32_t sequence_number);

    class InflightCommandSequence {
    public:
        InflightCommandSequence(uint32_t sequence_number, uint32_t ringbuffer_offset,
                                std::unique_ptr<MappedBatch> mapped_batch)
            : sequence_number_(sequence_number), ringbuffer_offset_(ringbuffer_offset),
              mapped_batch_(std::move(mapped_batch))
        {
        }

        uint32_t sequence_number() { return sequence_number_; }

        uint32_t ringbuffer_offset() { return ringbuffer_offset_; }

        MsdIntelContext* GetContext() { return mapped_batch_->GetContext(); }

        MappedBatch* mapped_batch() { return mapped_batch_.get(); }

    private:
        uint32_t sequence_number_;
        uint32_t ringbuffer_offset_;
        std::unique_ptr<MappedBatch> mapped_batch_;
    };

    std::queue<InflightCommandSequence> inflight_command_sequences_;

    friend class TestEngineCommandStreamer;
};

#endif // ENGINE_COMMAND_STREAMER_H
