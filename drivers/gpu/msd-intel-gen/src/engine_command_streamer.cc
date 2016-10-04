// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine_command_streamer.h"
#include "device_id.h"
#include "instructions.h"
#include "magma_util/macros.h"
#include "magma_util/sleep.h"
#include "msd_intel_buffer.h"
#include "registers.h"
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

    if (!InitContextBuffer(context_buffer.get(), ringbuffer.get()))
        return DRETF(false, "InitContextBuffer failed");

    // Transfer ownership of context_buffer
    context->SetEngineState(id(), std::move(context_buffer), std::move(ringbuffer));

    return true;
}

void EngineCommandStreamer::InitHardware(HardwareStatusPage* hardware_status_page)
{
    registers::HardwareStatusPageAddress::write(register_io(), mmio_base_,
                                                hardware_status_page->gpu_addr());

    uint32_t initial_sequence_number = sequencer()->next_sequence_number();
    hardware_status_page->write_sequence_number(initial_sequence_number);

    DLOG("initialized engine sequence number: 0x%x", initial_sequence_number);

    registers::GraphicsMode::write(register_io(), mmio_base_,
                                   registers::GraphicsMode::kExeclistEnable,
                                   registers::GraphicsMode::kExeclistEnable);
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
    void write_ring_head_pointer(uint32_t head)
    {
        state_[4] = mmio_base_ + 0x34;
        state_[5] = head;
    }

    // RING_BUFFER_TAIL - Ring Buffer Tail
    void write_ring_tail_pointer(uint32_t tail)
    {
        state_[6] = mmio_base_ + 0x30;
        state_[7] = tail;
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

bool EngineCommandStreamer::InitContextBuffer(MsdIntelBuffer* buffer, Ringbuffer* ringbuffer) const
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
    helper.write_ring_head_pointer(ringbuffer->head());
    // Ring buffer tail and start is patched in later (see UpdateContext).
    helper.write_ring_tail_pointer(0);
    helper.write_ring_buffer_start(~0);
    helper.write_ring_buffer_control(ringbuffer->size());
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

bool EngineCommandStreamer::SubmitContext(MsdIntelContext* context)
{
    if (!UpdateContext(context))
        return DRETF(false, "UpdateContext failed");
    SubmitExeclists(context);
    return true;
}

bool EngineCommandStreamer::UpdateContext(MsdIntelContext* context)
{
    gpu_addr_t gpu_addr;
    if (!context->GetRingbufferGpuAddress(id(), &gpu_addr))
        return DRETF(false, "failed to get ringbuffer gpu address");

    void* cpu_addr;
    if (!context->get_context_buffer(id())->platform_buffer()->MapPageCpu(1, &cpu_addr))
        return DRETF(false, "failed to map context page 1");

    RegisterStateHelper helper(id(), mmio_base_, reinterpret_cast<uint32_t*>(cpu_addr));

    uint32_t tail = context->get_ringbuffer(id())->tail();

    DLOG("UpdateContext ringbuffer gpu_addr 0x%llx tail 0x%x", gpu_addr, tail);

    helper.write_ring_tail_pointer(tail);
    helper.write_ring_buffer_start(gpu_addr);

    if (!context->get_context_buffer(id())->platform_buffer()->UnmapPageCpu(1))
        DLOG("UnmapPageCpu failed");

    return true;
}

void EngineCommandStreamer::SubmitExeclists(MsdIntelContext* context)
{
    gpu_addr_t gpu_addr;
    if (!context->GetGpuAddress(id(), &gpu_addr)) {
        // Shouldn't happen.
        DASSERT(false);
        gpu_addr = kInvalidGpuAddr;
    }

    // Use significant bits of context gpu_addr as globally unique context id
    DASSERT(PAGE_SIZE == 4096);
    uint64_t descriptor0 =
        registers::ExeclistSubmitPort::context_descriptor(gpu_addr, gpu_addr >> 12, false);
    uint64_t descriptor1 = 0;

    registers::ExeclistSubmitPort::write(register_io(), mmio_base_, descriptor1, descriptor0);
}

uint64_t EngineCommandStreamer::GetActiveHeadPointer()
{
    return registers::ActiveHeadPointer::read(register_io(), mmio_base_);
}

///////////////////////////////////////////////////////////////////////////////

std::unique_ptr<RenderEngineCommandStreamer>
RenderEngineCommandStreamer::Create(EngineCommandStreamer::Owner* owner,
                                    AddressSpace* address_space, uint32_t device_id)
{
    std::unique_ptr<RenderInitBatch> batch;
    if (DeviceId::is_gen8(device_id)) {
        batch = std::unique_ptr<RenderInitBatch>(new RenderInitBatchGen8());
    } else if (DeviceId::is_gen9(device_id)) {
        batch = std::unique_ptr<RenderInitBatch>(new RenderInitBatchGen9());
    } else {
        return DRETP(nullptr, "unhandled device id");
    }

    auto buffer = std::unique_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(batch->size()));
    if (!buffer)
        return DRETP(nullptr, "failed to allocate render init buffer");

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

bool RenderEngineCommandStreamer::RenderInit(std::shared_ptr<MsdIntelContext> context)
{
    DASSERT(init_batch_);
    uint32_t sequence_number;
    std::unique_ptr<SimpleMappedBatch> mapped_batch(
        new SimpleMappedBatch(context, init_batch_->buffer()));
    return ExecBatch(std::move(mapped_batch), 0, &sequence_number);
}

bool RenderEngineCommandStreamer::ExecBatch(std::unique_ptr<MappedBatch> mapped_batch,
                                            uint32_t pipe_control_flags,
                                            uint32_t* sequence_number_out)
{
    MsdIntelContext* context = mapped_batch->GetContext();

    uint32_t sequence_number = sequencer()->next_sequence_number();

    DLOG("Executing batch with sequence number 0x%x", sequence_number);

    mapped_batch->SetSequenceNumber(sequence_number);

    gpu_addr_t gpu_addr;
    if (!mapped_batch->GetGpuAddress(ADDRESS_SPACE_GTT, &gpu_addr))
        return DRETF(false, "coudln't get batch gpu address");

    if (!StartBatchBuffer(context, gpu_addr, ADDRESS_SPACE_GTT))
        return DRETF(false, "failed to emit batch");

    if (!PipeControl(context, pipe_control_flags))
        return DRETF(false, "FlushInvalidate failed");

    if (!WriteSequenceNumber(context, sequence_number))
        return DRETF(false, "failed to finish batch buffer");

    if (!SubmitContext(context))
        return DRETF(false, "SubmitContext failed");

    uint32_t ringbuffer_offset = context->get_ringbuffer(id())->tail();

    inflight_command_sequences_.emplace(
        InflightCommandSequence(sequence_number, ringbuffer_offset, std::move(mapped_batch)));

    *sequence_number_out = sequence_number;

    return true;
}

bool RenderEngineCommandStreamer::ExecuteCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf)
{
    DLOG("preparing command buffer for execution");

    if (!cmd_buf->PrepareForExecution(this))
        return DRETF(false, "Failed to prepare command buffer for execution");

    uint32_t pipe_control_flags = MiPipeControl::kIndirectStatePointersDisable |
                                  MiPipeControl::kCommandStreamerStallEnableBit;
    uint32_t sequence_number;

    if (!ExecBatch(std::move(cmd_buf), pipe_control_flags, &sequence_number))
        return DRETF(false, "ExecBatch failed");

    return true;
}

bool RenderEngineCommandStreamer::WaitRendering(std::shared_ptr<MsdIntelBuffer> buf)
{
    DASSERT(buf);
    // early out if we never rendered to this buffer or rendering has already completed
    if (buf->sequence_number() == Sequencer::kInvalidSequenceNumber)
        return true;
    return WaitRendering(buf->sequence_number());
}

bool RenderEngineCommandStreamer::WaitRendering(uint32_t sequence_number)
{
    DLOG("WaitRendering on sequence number 0x%x", sequence_number);

    // Dont wait for something you havent executed
    DASSERT(!inflight_command_sequences_.empty());
    DASSERT(sequence_number != Sequencer::kInvalidSequenceNumber);
    DASSERT(sequence_number <= inflight_command_sequences_.back().sequence_number());

    // TODO: Handle sequence number wrapping
    if (sequence_number < inflight_command_sequences_.front().sequence_number())
        return true;

    HardwareStatusPage* status_page =
        inflight_command_sequences_.front().GetContext()->hardware_status_page(id());

    constexpr uint32_t kTimeOutMs = 100;
    auto start = std::chrono::high_resolution_clock::now();

    while (!inflight_command_sequences_.empty()) {
        uint32_t last_completed_sequence = status_page->read_sequence_number();
        DLOG("WaitRendering loop last_completed_sequence: 0x%x", last_completed_sequence);

        // pop all completed command buffers
        while (!inflight_command_sequences_.empty() &&
               inflight_command_sequences_.front().sequence_number() <= last_completed_sequence) {

            InflightCommandSequence& sequence = inflight_command_sequences_.front();

            DLOG("WaitRendering popping inflight command sequence with sequence_number 0x%x "
                 "ringbuffer_start_offset 0x%x",
                 sequence.sequence_number(), sequence.ringbuffer_offset());

            sequence.GetContext()->get_ringbuffer(id())->update_head(sequence.ringbuffer_offset());

            inflight_command_sequences_.pop();

            start = std::chrono::high_resolution_clock::now();
        }

        if (last_completed_sequence >= sequence_number)
            return true; // were done here

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        if (elapsed.count() > kTimeOutMs) {
            DLOG("WaitRendering timeout");
            return false;
        }

        std::this_thread::yield();
    }

    DASSERT(false); // the asserts at the top should stop us from getting here
    return false;
}

bool EngineCommandStreamer::PipeControl(MsdIntelContext* context, uint32_t flags)
{
    if (flags) {
        auto ringbuffer = context->get_ringbuffer(id());

        if (!ringbuffer->HasSpace(MiPipeControl::kDwordCount * sizeof(uint32_t)))
            return DRETF(false, "ringbuffer has insufficient space");

        MiPipeControl::write(ringbuffer, flags);
    }

    return true;
}

bool RenderEngineCommandStreamer::StartBatchBuffer(MsdIntelContext* context, gpu_addr_t gpu_addr,
                                                   AddressSpaceId address_space_id)
{
    auto ringbuffer = context->get_ringbuffer(id());

    uint32_t dword_count = MiBatchBufferStart::kDwordCount + MiNoop::kDwordCount;

    if (!ringbuffer->HasSpace(dword_count * sizeof(uint32_t)))
        return DRETF(false, "ringbuffer has insufficient space");

    MiBatchBufferStart::write_ringbuffer(ringbuffer, gpu_addr, address_space_id);
    MiNoop::write_ringbuffer(ringbuffer);

    return true;
}

bool RenderEngineCommandStreamer::WriteSequenceNumber(MsdIntelContext* context,
                                                      uint32_t sequence_number)
{
    auto ringbuffer = context->get_ringbuffer(id());

    uint32_t dword_count = MiStoreDataImmediate::kDwordCount + MiNoop::kDwordCount;

    if (!ringbuffer->HasSpace(dword_count * sizeof(uint32_t)))
        return DRETF(false, "ringbuffer has insufficient space");

    gpu_addr_t gpu_addr =
        context->hardware_status_page(id())->gpu_addr() + HardwareStatusPage::kSequenceNumberOffset;

    DLOG("writing sequence number update to 0x%x", sequence_number);

    MiStoreDataImmediate::write_ringbuffer(ringbuffer, sequence_number, gpu_addr,
                                           ADDRESS_SPACE_GTT);
    MiNoop::write_ringbuffer(ringbuffer);

    return true;
}
