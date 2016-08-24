// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine_command_streamer.h"
#include "instructions.h"
#include "magma_util/macros.h"
#include "msd_intel_buffer.h"
#include "ringbuffer.h"

EngineCommandStreamer::EngineCommandStreamer(Owner* owner, EngineCommandStreamerId id,
                                             uint32_t mmio_base)
    : owner_(owner), id_(id), mmio_base_(mmio_base)
{
    DASSERT(owner);
}

bool EngineCommandStreamer::InitContext(MsdIntelContext* context) const
{
    DASSERT(context);

    uint32_t context_size = GetContextSize();
    DASSERT(context_size > 0 && magma::is_page_aligned(context_size));

    std::unique_ptr<MsdIntelBuffer> context_buffer(MsdIntelBuffer::Create(context_size));
    if (!context_buffer)
        return DRETF(false, "couldn't create context buffer");

    std::unique_ptr<Ringbuffer> ringbuffer(new Ringbuffer(MsdIntelBuffer::Create(32 * PAGE_SIZE)));

    if (!InitContextBuffer(context_buffer.get(), ringbuffer->size()))
        return DRETF(false, "InitContextBuffer failed");

    // Transfer ownership of context_buffer
    context->SetEngineState(id(), std::move(context_buffer), std::move(ringbuffer));

    return true;
}

void EngineCommandStreamer::InitHardware(HardwareStatusPage* hardware_status_page)
{
    HardwareStatusPageAddress::write(register_io(), mmio_base_, hardware_status_page->gpu_addr());
}

// Register definitions from BSpec BXML Reference.
// Register State Context definition from public BSpec,
// intel-gfx-prm-osrc-bdw-vol07-3d_media_gpgpu_3.pdf pp. 27-28
class RegisterStateHelper {
public:
    RegisterStateHelper(EngineCommandStreamerId id, uint32_t mmio_base, uint32_t* state)
        : id_(id), mmio_base_(mmio_base), state_(state)
    {
    }

    void write_load_register_immediate_headers()
    {
        switch (id_) {
        case RENDER_COMMAND_STREAMER:
            state_[1] = 0x1100101B;
            state_[0x21] = 0x11001011;
            state_[0x41] = 0x11000001;
            break;
        }
    }

    // CTXT_SR_CTL - Context Save/Restore Control Register
    void write_context_save_restore_control()
    {
        constexpr uint32_t kInhibitSyncContextSwitchBit = 1 << 3;
        constexpr uint32_t kRenderContextRestoreInhibitBit = 1;

        state_[2] = mmio_base_ + 0x244;
        if (id_ == RENDER_COMMAND_STREAMER) {
            uint32_t bits = kInhibitSyncContextSwitchBit | kRenderContextRestoreInhibitBit;
            state_[3] = (bits << 16) | bits;
        }
    }

    // RING_BUFFER_HEAD - Ring Buffer Head
    void write_ring_head_pointer()
    {
        state_[4] = mmio_base_ + 0x34;
        state_[5] = 0;
    }

    // RING_BUFFER_TAIL - Ring Buffer Tail
    void write_ring_tail_pointer()
    {
        state_[6] = mmio_base_ + 0x30;
        state_[7] = 0;
    }

    // RING_BUFFER_START - Ring Buffer Start
    void write_ring_buffer_start(uint32_t ring_buffer_start)
    {
        state_[8] = mmio_base_ + 0x38;
        state_[9] = ring_buffer_start;
    }

    // RING_BUFFER_CTL - Ring Buffer Control
    void write_ring_buffer_control(uint32_t ringbuffer_size)
    {
        constexpr uint32_t kRingValid = 1;
        DASSERT(ringbuffer_size >= PAGE_SIZE && ringbuffer_size <= 512 * PAGE_SIZE);
        DASSERT(magma::is_page_aligned(ringbuffer_size));
        state_[0xA] = mmio_base_ + 0x3C;
        // This register assumes 4k pages
        DASSERT(PAGE_SIZE == 4096);
        state_[0xB] = (ringbuffer_size - PAGE_SIZE) | kRingValid;
    }

    // BB_ADDR_UDW - Batch Buffer Upper Head Pointer Register
    void write_batch_buffer_upper_head_pointer()
    {
        state_[0xC] = mmio_base_ + 0x168;
        state_[0xD] = 0;
    }

    // BB_ADDR - Batch Buffer Head Pointer Register
    void write_batch_buffer_head_pointer()
    {
        state_[0xE] = mmio_base_ + 0x140;
        state_[0xF] = 0;
    }

    // BB_STATE - Batch Buffer State Register
    void write_batch_buffer_state()
    {
        constexpr uint32_t kAddressSpacePpgtt = 1 << 5;
        state_[0x10] = mmio_base_ + 0x110;
        state_[0x11] = kAddressSpacePpgtt;
    }

    // SBB_ADDR_UDW - Second Level Batch Buffer Upper Head Pointer Register
    void write_second_level_batch_buffer_upper_head_pointer()
    {
        state_[0x12] = mmio_base_ + 0x11C;
        state_[0x13] = 0;
    }

    // SBB_ADDR - Second Level Batch Buffer Head Pointer Register
    void write_second_level_batch_buffer_head_pointer()
    {
        state_[0x14] = mmio_base_ + 0x114;
        state_[0x15] = 0;
    }

    // SBB_STATE - Second Level Batch Buffer State Register
    void write_second_level_batch_buffer_state()
    {
        state_[0x16] = mmio_base_ + 0x118;
        state_[0x17] = 0;
    }

    // BB_PER_CTX_PTR - Batch Buffer Per Context Pointer
    void write_batch_buffer_per_context_pointer()
    {
        state_[0x18] = mmio_base_ + 0x1C0;
        state_[0x19] = 0;
    }

    // INDIRECT_CTX - Indirect Context Pointer
    void write_indirect_context_pointer()
    {
        state_[0x1A] = mmio_base_ + 0x1C4;
        state_[0x1B] = 0;
    }

    // INDIRECT_CTX_OFFSET - Indirect Context Offset Pointer
    void write_indirect_context_offset_pointer()
    {
        state_[0x1C] = mmio_base_ + 0x1C8;
        state_[0x1D] = 0;
    }

    // CS_CTX_TIMESTAMP - CS Context Timestamp Count
    void write_context_timestamp()
    {
        state_[0x1E] = mmio_base_ + 0x3A8;
        state_[0x1F] = 0;
    }

    void write_pdp3_upper(uint64_t pdp_bus_addr)
    {
        state_[0x24] = mmio_base_ + 0x28C;
        state_[0x25] = magma::upper_32_bits(pdp_bus_addr);
    }

    void write_pdp3_lower(uint64_t pdp_bus_addr)
    {
        state_[0x26] = mmio_base_ + 0x288;
        state_[0x27] = magma::lower_32_bits(pdp_bus_addr);
    }

    void write_pdp2_upper(uint64_t pdp_bus_addr)
    {
        state_[0x28] = mmio_base_ + 0x284;
        state_[0x29] = magma::upper_32_bits(pdp_bus_addr);
    }

    void write_pdp2_lower(uint64_t pdp_bus_addr)
    {
        state_[0x2A] = mmio_base_ + 0x280;
        state_[0x2B] = magma::lower_32_bits(pdp_bus_addr);
    }

    void write_pdp1_upper(uint64_t pdp_bus_addr)
    {
        state_[0x2C] = mmio_base_ + 0x27C;
        state_[0x2D] = magma::upper_32_bits(pdp_bus_addr);
    }

    void write_pdp1_lower(uint64_t pdp_bus_addr)
    {
        state_[0x2E] = mmio_base_ + 0x278;
        state_[0x2F] = magma::lower_32_bits(pdp_bus_addr);
    }

    void write_pdp0_upper(uint64_t pdp_bus_addr)
    {
        state_[0x30] = mmio_base_ + 0x274;
        state_[0x31] = magma::upper_32_bits(pdp_bus_addr);
    }

    void write_pdp0_lower(uint64_t pdp_bus_addr)
    {
        state_[0x32] = mmio_base_ + 0x270;
        state_[0x33] = magma::lower_32_bits(pdp_bus_addr);
    }

    // R_PWR_CLK_STATE - Render Power Clock State Register
    void write_render_power_clock_state()
    {
        state_[0x42] = mmio_base_ + 0x0C8;
        state_[0x43] = 0;
    }

private:
    EngineCommandStreamerId id_;
    uint32_t mmio_base_;
    uint32_t* state_;
};

bool EngineCommandStreamer::InitContextBuffer(MsdIntelBuffer* buffer,
                                              uint32_t ringbuffer_size) const
{
    DASSERT(buffer->write_domain() == MEMORY_DOMAIN_CPU);

    auto platform_buf = buffer->platform_buffer();
    void* addr;
    if (!platform_buf->MapCpu(&addr))
        return DRETF(false, "Couldn't map context buffer");

    uint32_t* state = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(addr) + PAGE_SIZE);
    RegisterStateHelper helper(id(), mmio_base_, state);

    helper.write_load_register_immediate_headers();
    helper.write_context_save_restore_control();
    helper.write_ring_head_pointer();
    helper.write_ring_tail_pointer();
    // Ring buffer start is patched in later.
    helper.write_ring_buffer_start(~0);
    helper.write_ring_buffer_control(ringbuffer_size);
    helper.write_batch_buffer_upper_head_pointer();
    helper.write_batch_buffer_head_pointer();
    helper.write_batch_buffer_state();
    helper.write_second_level_batch_buffer_upper_head_pointer();
    helper.write_second_level_batch_buffer_head_pointer();
    helper.write_second_level_batch_buffer_state();
    helper.write_batch_buffer_per_context_pointer();
    helper.write_indirect_context_pointer();
    helper.write_indirect_context_offset_pointer();
    helper.write_context_timestamp();
    // TODO(MA-64) - get ppgtt addresses
    helper.write_pdp3_upper(0);
    helper.write_pdp3_lower(0);
    helper.write_pdp2_upper(0);
    helper.write_pdp2_lower(0);
    helper.write_pdp1_upper(0);
    helper.write_pdp1_lower(0);
    helper.write_pdp0_upper(0);
    helper.write_pdp0_lower(0);

    if (id() == RENDER_COMMAND_STREAMER) {
        helper.write_render_power_clock_state();
    }

    if (!platform_buf->UnmapCpu())
        return DRETF(false, "Couldn't unmap context buffer");

    return true;
}

///////////////////////////////////////////////////////////////////////////////

std::unique_ptr<RenderEngineCommandStreamer>
RenderEngineCommandStreamer::Create(EngineCommandStreamer::Owner* owner,
                                    AddressSpace* address_space)
{
    auto buffer = std::unique_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(RenderInitBatch::Size()));
    if (!buffer)
        return DRETP(nullptr, "failed to allocate render init buffer");

    auto batch = std::unique_ptr<RenderInitBatch>(new RenderInitBatch());

    if (!batch->Init(std::move(buffer), address_space))
        return DRETP(nullptr, "batch init failed");

    return std::unique_ptr<RenderEngineCommandStreamer>(
        new RenderEngineCommandStreamer(owner, std::move(batch)));
}

RenderEngineCommandStreamer::RenderEngineCommandStreamer(
    EngineCommandStreamer::Owner* owner, std::unique_ptr<RenderInitBatch> init_batch)
    : EngineCommandStreamer(owner, RENDER_COMMAND_STREAMER, kRenderEngineMmioBase),
      init_batch_(std::move(init_batch))
{
    DASSERT(init_batch_);
}

bool RenderEngineCommandStreamer::RenderInit(MsdIntelContext* context)
{
    DASSERT(init_batch_);
    uint64_t gpu_addr = init_batch_->GetGpuAddress();

    if (!StartBatchBuffer(context, gpu_addr, false))
        return DRETF(false, "failed to emit batch");

    // More RenderInit to come.

    return true;
}

bool RenderEngineCommandStreamer::StartBatchBuffer(MsdIntelContext* context, gpu_addr_t gpu_addr,
                                                   bool ppgtt)
{
    auto ringbuffer = context->get_ringbuffer(id());

    uint32_t dword_count = MiBatchBufferStart::kDwordCount + MiNoop::kDwordCount;

    if (!ringbuffer->HasSpace(dword_count * sizeof(uint32_t)))
        return DRETF(false, "ringbuffer has insufficient space");

    MiBatchBufferStart::write_ringbuffer(ringbuffer, gpu_addr, ppgtt);
    MiNoop::write_ringbuffer(ringbuffer);

    return true;
}
