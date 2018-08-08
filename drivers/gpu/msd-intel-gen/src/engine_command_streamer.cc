// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine_command_streamer.h"
#include "cache_config.h"
#include "device_id.h"
#include "instructions.h"
#include "magma_util/macros.h"
#include "msd_intel_buffer.h"
#include "msd_intel_connection.h"
#include "platform_trace.h"
#include "registers.h"
#include "render_init_batch.h"
#include "ringbuffer.h"
#include <thread>

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

    std::unique_ptr<MsdIntelBuffer> context_buffer(
        MsdIntelBuffer::Create(context_size, "context-buffer"));
    if (!context_buffer)
        return DRETF(false, "couldn't create context buffer");

    std::unique_ptr<Ringbuffer> ringbuffer(
        new Ringbuffer(MsdIntelBuffer::Create(32 * PAGE_SIZE, "ring-buffer")));

    if (!InitContextBuffer(context_buffer.get(), ringbuffer.get(),
                           context->exec_address_space().get()))
        return DRETF(false, "InitContextBuffer failed");

    // Transfer ownership of context_buffer
    context->SetEngineState(id(), std::move(context_buffer), std::move(ringbuffer));

    return true;
}

bool EngineCommandStreamer::InitContextCacheConfig(std::shared_ptr<MsdIntelContext> context)
{
    auto ringbuffer = context->get_ringbuffer(id());

    if (!ringbuffer->HasSpace(CacheConfig::InstructionBytesRequired()))
        return DRETF(false, "insufficient ringbuffer space for cache config");

    if (!CacheConfig::InitCacheConfig(ringbuffer, id()))
        return DRETF(false, "failed to init cache config buffer");

    return true;
}

void EngineCommandStreamer::InitHardware()
{
    HardwareStatusPage* status_page = hardware_status_page(id());

    registers::HardwareStatusPageAddress::write(register_io(), mmio_base_, status_page->gpu_addr());

    uint32_t initial_sequence_number = sequencer()->next_sequence_number();
    status_page->write_sequence_number(initial_sequence_number);

    DLOG("initialized engine sequence number: 0x%x", initial_sequence_number);

    registers::GraphicsMode::write(register_io(), mmio_base_,
                                   registers::GraphicsMode::kExeclistEnable,
                                   registers::GraphicsMode::kExeclistEnable);

    registers::HardwareStatusMask::write(
        register_io(), mmio_base_, registers::InterruptRegisterBase::RENDER_ENGINE,
        registers::InterruptRegisterBase::USER, registers::InterruptRegisterBase::UNMASK);
    registers::GtInterruptMask0::write(
        register_io(), registers::InterruptRegisterBase::RENDER_ENGINE,
        registers::InterruptRegisterBase::USER, registers::InterruptRegisterBase::UNMASK);
    registers::GtInterruptEnable0::write(register_io(),
                                         registers::InterruptRegisterBase::RENDER_ENGINE,
                                         registers::InterruptRegisterBase::USER, true);

    registers::HardwareStatusMask::write(
        register_io(), mmio_base_, registers::InterruptRegisterBase::RENDER_ENGINE,
        registers::InterruptRegisterBase::CONTEXT_SWITCH, registers::InterruptRegisterBase::UNMASK);
    registers::GtInterruptMask0::write(
        register_io(), registers::InterruptRegisterBase::RENDER_ENGINE,
        registers::InterruptRegisterBase::CONTEXT_SWITCH, registers::InterruptRegisterBase::UNMASK);
    registers::GtInterruptEnable0::write(register_io(),
                                         registers::InterruptRegisterBase::RENDER_ENGINE,
                                         registers::InterruptRegisterBase::CONTEXT_SWITCH, true);

    // WaEnableGapsTsvCreditFix
    registers::ArbiterControl::workaround(register_io());
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
        state_[0x22] = mmio_base_ + 0x3A8;
        state_[0x23] = 0;
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

bool EngineCommandStreamer::InitContextBuffer(MsdIntelBuffer* buffer, Ringbuffer* ringbuffer,
                                              AddressSpace* address_space) const
{
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
    helper.write_pdp3_upper(0);
    helper.write_pdp3_lower(0);
    helper.write_pdp2_upper(0);
    helper.write_pdp2_lower(0);
    helper.write_pdp1_upper(0);
    helper.write_pdp1_lower(0);
    helper.write_pdp0_upper(0);
    helper.write_pdp0_lower(0);
    if (address_space->type() == ADDRESS_SPACE_PPGTT) {
        auto ppgtt = static_cast<PerProcessGtt*>(address_space);
        uint64_t pml4_addr = ppgtt->get_pml4_bus_addr();
        helper.write_pdp0_upper(pml4_addr);
        helper.write_pdp0_lower(pml4_addr);
    }

    if (id() == RENDER_COMMAND_STREAMER) {
        helper.write_render_power_clock_state();
    }

    if (!platform_buf->UnmapCpu())
        return DRETF(false, "Couldn't unmap context buffer");

    return true;
}

bool EngineCommandStreamer::SubmitContext(MsdIntelContext* context, uint32_t tail)
{
    TRACE_DURATION("magma", "SubmitContext");
    if (!UpdateContext(context, tail))
        return DRETF(false, "UpdateContext failed");

    SubmitExeclists(context);
    return true;
}

bool EngineCommandStreamer::UpdateContext(MsdIntelContext* context, uint32_t tail)
{
    gpu_addr_t gpu_addr;
    if (!context->GetRingbufferGpuAddress(id(), &gpu_addr))
        return DRETF(false, "failed to get ringbuffer gpu address");

    void* cpu_addr;
    if (!context->get_context_buffer(id())->platform_buffer()->MapCpu(&cpu_addr))
        return DRETF(false, "failed to map context page 1");

    RegisterStateHelper helper(
        id(), mmio_base_,
        reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(cpu_addr) + PAGE_SIZE));

    DLOG("UpdateContext ringbuffer gpu_addr 0x%lx tail 0x%x", gpu_addr, tail);

    helper.write_ring_tail_pointer(tail);
    helper.write_ring_buffer_start(gpu_addr);

    if (!context->get_context_buffer(id())->platform_buffer()->UnmapCpu())
        DLOG("UnmapPageCpu failed");

    return true;
}

void EngineCommandStreamer::SubmitExeclists(MsdIntelContext* context)
{
    TRACE_DURATION("magma", "SubmitExeclists");
    gpu_addr_t gpu_addr;
    if (!context->GetGpuAddress(id(), &gpu_addr)) {
        // Shouldn't happen.
        DASSERT(false);
        gpu_addr = kInvalidGpuAddr;
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (bool busy = true; busy;) {
        constexpr uint32_t kTimeoutUs = 100;
        uint64_t status = registers::ExeclistStatus::read(register_io(), mmio_base());

        busy = registers::ExeclistStatus::execlist_write_pointer(status) ==
                   registers::ExeclistStatus::execlist_current_pointer(status) &&
               registers::ExeclistStatus::execlist_queue_full(status);
        if (busy) {
            if (std::chrono::duration<double, std::micro>(
                    std::chrono::high_resolution_clock::now() - start)
                    .count() > kTimeoutUs) {
                magma::log(magma::LOG_WARNING, "Timeout waiting for execlist port");
                break;
            }
        }
    }

    DLOG("SubmitExeclists context descriptor id 0x%lx", gpu_addr >> 12);

    // Use most significant bits of context gpu_addr as globally unique context id
    DASSERT(PAGE_SIZE == 4096);
    uint64_t descriptor0 = registers::ExeclistSubmitPort::context_descriptor(
        gpu_addr, gpu_addr >> 12, context->exec_address_space()->type() == ADDRESS_SPACE_PPGTT);
    uint64_t descriptor1 = 0;

    registers::ExeclistSubmitPort::write(register_io(), mmio_base_, descriptor1, descriptor0);
}

uint64_t EngineCommandStreamer::GetActiveHeadPointer()
{
    return registers::ActiveHeadPointer::read(register_io(), mmio_base_);
}

bool EngineCommandStreamer::Reset()
{
    registers::GraphicsDeviceResetControl::Engine engine;

    switch (id()) {
        case RENDER_COMMAND_STREAMER:
            engine = registers::GraphicsDeviceResetControl::RENDER_ENGINE;
            break;
        default:
            return DRETF(false, "Reset for engine id %d not implemented", id());
    }

    registers::ResetControl::request(register_io(), mmio_base());

    constexpr uint32_t kRetryMs = 10;
    constexpr uint32_t kRetryTimeoutMs = 100;

    auto start = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed;

    do {
        if (registers::ResetControl::ready_for_reset(register_io(), mmio_base())) {
            registers::GraphicsDeviceResetControl::initiate_reset(register_io(), engine);

            start = std::chrono::high_resolution_clock::now();
            do {
                if (registers::GraphicsDeviceResetControl::is_reset_complete(register_io(), engine))
                    return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(kRetryMs));
                elapsed = std::chrono::high_resolution_clock::now() - start;

            } while (elapsed.count() < kRetryTimeoutMs);

            return DRETF(false, "reset failed to complete");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kRetryMs));
        elapsed = std::chrono::high_resolution_clock::now() - start;

    } while (elapsed.count() < kRetryTimeoutMs);

    return DRETF(false, "Ready for reset failed");
}

///////////////////////////////////////////////////////////////////////////////

std::unique_ptr<RenderInitBatch>
RenderEngineCommandStreamer::CreateRenderInitBatch(uint32_t device_id)
{
    std::unique_ptr<RenderInitBatch> batch;
    if (DeviceId::is_gen9(device_id)) {
        return std::unique_ptr<RenderInitBatch>(new RenderInitBatchGen9());
    }
    return DRETP(nullptr, "unhandled device id");
}

std::unique_ptr<RenderEngineCommandStreamer>
RenderEngineCommandStreamer::Create(EngineCommandStreamer::Owner* owner)
{
    return std::unique_ptr<RenderEngineCommandStreamer>(new RenderEngineCommandStreamer(owner));
}

RenderEngineCommandStreamer::RenderEngineCommandStreamer(EngineCommandStreamer::Owner* owner)
    : EngineCommandStreamer(owner, RENDER_COMMAND_STREAMER, kRenderEngineMmioBase)
{
    scheduler_ = Scheduler::CreateFifoScheduler();
}

bool RenderEngineCommandStreamer::RenderInit(std::shared_ptr<MsdIntelContext> context,
                                             std::unique_ptr<RenderInitBatch> init_batch,
                                             std::shared_ptr<AddressSpace> address_space)
{
    DASSERT(context);
    DASSERT(init_batch);
    DASSERT(address_space);

    auto buffer = std::unique_ptr<MsdIntelBuffer>(
        MsdIntelBuffer::Create(init_batch->size(), "render-init-batch"));
    if (!buffer)
        return DRETF(false, "failed to allocate render init buffer");

    auto mapping = init_batch->Init(std::move(buffer), address_space);
    if (!mapping)
        return DRETF(false, "batch init failed");

    std::unique_ptr<SimpleMappedBatch> mapped_batch(
        new SimpleMappedBatch(context, std::move(mapping)));

    return ExecBatch(std::move(mapped_batch));
}

bool RenderEngineCommandStreamer::ExecBatch(std::unique_ptr<MappedBatch> mapped_batch)
{
    TRACE_DURATION("magma", "ExecBatch");
    auto context = mapped_batch->GetContext().lock();
    DASSERT(context);

    if (!MoveBatchToInflight(std::move(mapped_batch)))
        return DRETF(false, "WriteBatchToRingbuffer failed");

    SubmitContext(context.get(), inflight_command_sequences_.back().ringbuffer_offset());
    return true;
}

bool RenderEngineCommandStreamer::MoveBatchToInflight(std::unique_ptr<MappedBatch> mapped_batch)
{
    auto context = mapped_batch->GetContext().lock();
    DASSERT(context);

    gpu_addr_t gpu_addr;
    if (!mapped_batch->GetGpuAddress(&gpu_addr))
        return DRETF(false, "couldn't get batch gpu address");

    if (!StartBatchBuffer(context.get(), gpu_addr, context->exec_address_space()->type()))
        return DRETF(false, "failed to emit batch");

    uint32_t sequence_number;
    if (!PipeControl(context.get(), mapped_batch->GetPipeControlFlags(), &sequence_number))
        return DRETF(false, "PipeControl failed");

    auto ringbuffer = context->get_ringbuffer(id());

    // TODO: don't allocate a sequence number if we don't have space for the user interrupt
    if (!ringbuffer->HasSpace(MiUserInterrupt::kDwordCount * sizeof(uint32_t)))
        return DRETF(false, "ringbuffer has insufficient space");

    MiUserInterrupt::write(ringbuffer);

    mapped_batch->SetSequenceNumber(sequence_number);

    uint32_t ringbuffer_offset = context->get_ringbuffer(id())->tail();
    inflight_command_sequences_.emplace(sequence_number, ringbuffer_offset,
                                        std::move(mapped_batch));
    batch_submitted(sequence_number);

    return true;
}

void RenderEngineCommandStreamer::ContextSwitched()
{
    context_switch_pending_ = false;
    ScheduleContext();
}

void RenderEngineCommandStreamer::ScheduleContext()
{
    auto context = scheduler_->ScheduleContext();
    if (!context)
        return;

    while (true) {
        auto mapped_batch = std::move(context->pending_batch_queue().front());
        mapped_batch->scheduled();
        context->pending_batch_queue().pop();

        // TODO(MA-142) - MoveBatchToInflight should not fail.  Scheduler should verify there is
        // sufficient room in the ringbuffer before selecting a context.
        // For now, drop the command buffer and try another context.
        if (!MoveBatchToInflight(std::move(mapped_batch))) {
            magma::log(magma::LOG_WARNING, "ExecBatch failed");
            break;
        }

        // Scheduler returns nullptr when its time to switch contexts
        auto next_context = scheduler_->ScheduleContext();
        if (next_context == nullptr)
            break;
        DASSERT(context == next_context);
    }

    SubmitContext(context.get(), inflight_command_sequences_.back().ringbuffer_offset());
    context_switch_pending_ = true;
}

void RenderEngineCommandStreamer::SubmitCommandBuffer(std::unique_ptr<CommandBuffer> command_buffer)
{
    auto context = command_buffer->GetContext().lock();
    if (!context)
        return;

    context->pending_batch_queue().emplace(std::move(command_buffer));

    scheduler_->CommandBufferQueued(context);

    if (!context_switch_pending_)
        ScheduleContext();
}

bool RenderEngineCommandStreamer::WaitIdle()
{
    constexpr uint32_t kTimeOutMs = 100;
    uint32_t sequence_number = Sequencer::kInvalidSequenceNumber;

    auto start = std::chrono::high_resolution_clock::now();

    while (!inflight_command_sequences_.empty()) {
        uint32_t last_completed_sequence_number =
            hardware_status_page(RENDER_COMMAND_STREAMER)->read_sequence_number();
        ProcessCompletedCommandBuffers(last_completed_sequence_number);

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;

        if (last_completed_sequence_number != sequence_number) {
            sequence_number = last_completed_sequence_number;
            start = end;
        } else if (elapsed.count() > kTimeOutMs) {
            return DRETF(false, "WaitIdle timeout");
        }

        std::this_thread::yield();
    }
    return true;
}

void RenderEngineCommandStreamer::ProcessCompletedCommandBuffers(uint32_t last_completed_sequence)
{
    // pop all completed command buffers
    while (!inflight_command_sequences_.empty() &&
           inflight_command_sequences_.front().sequence_number() <= last_completed_sequence) {

        InflightCommandSequence& sequence = inflight_command_sequences_.front();

        DLOG("ProcessCompletedCommandBuffers popping inflight command sequence with "
             "sequence_number 0x%x "
             "ringbuffer_start_offset 0x%x",
             sequence.sequence_number(), sequence.ringbuffer_offset());

        auto context = sequence.GetContext().lock();
        DASSERT(context);
        context->get_ringbuffer(id())->update_head(sequence.ringbuffer_offset());

        if (sequence.mapped_batch()->was_scheduled())
            scheduler_->CommandBufferCompleted(context);

        inflight_command_sequences_.pop();
    }
}

bool EngineCommandStreamer::PipeControl(MsdIntelContext* context, uint32_t flags,
                                        uint32_t* sequence_number_out)
{
    auto ringbuffer = context->get_ringbuffer(id());

    uint32_t dword_count = MiPipeControl::kDwordCount + MiNoop::kDwordCount;

    if (!ringbuffer->HasSpace(dword_count * sizeof(uint32_t)))
        return DRETF(false, "ringbuffer has insufficient space");

    gpu_addr_t gpu_addr =
        hardware_status_page(id())->gpu_addr() + HardwareStatusPage::kSequenceNumberOffset;

    uint32_t sequence_number = sequencer()->next_sequence_number();
    DLOG("writing sequence number update to 0x%x", sequence_number);

    MiPipeControl::write(ringbuffer, sequence_number, gpu_addr, flags);
    MiNoop::write(ringbuffer);

    *sequence_number_out = sequence_number;

    return true;
}

bool RenderEngineCommandStreamer::StartBatchBuffer(MsdIntelContext* context, gpu_addr_t gpu_addr,
                                                   AddressSpaceType address_space_type)
{
    auto ringbuffer = context->get_ringbuffer(id());

    uint32_t dword_count = MiBatchBufferStart::kDwordCount + MiNoop::kDwordCount;

    if (!ringbuffer->HasSpace(dword_count * sizeof(uint32_t)))
        return DRETF(false, "ringbuffer has insufficient space");

    MiBatchBufferStart::write(ringbuffer, gpu_addr, address_space_type);
    MiNoop::write(ringbuffer);

    DLOG("started batch buffer 0x%lx address_space_type %d", gpu_addr, address_space_type);

    return true;
}

void RenderEngineCommandStreamer::ResetCurrentContext()
{
    DLOG("ResetCurrentContext");

    DASSERT(!inflight_command_sequences_.empty());

    auto context = inflight_command_sequences_.front().GetContext().lock();
    DASSERT(context);

    context->Kill();

    // Cleanup resources for any inflight command sequences on this context
    while (!inflight_command_sequences_.empty()) {
        auto& sequence = inflight_command_sequences_.front();
        if (sequence.mapped_batch()->was_scheduled())
            scheduler_->CommandBufferCompleted(
                inflight_command_sequences_.front().GetContext().lock());
        inflight_command_sequences_.pop();
    }

    // Reset the engine hardware
    EngineCommandStreamer::Reset();
}

std::vector<MappedBatch*> RenderEngineCommandStreamer::GetInflightBatches()
{
    uint32_t num_sequences = inflight_command_sequences_.size();
    std::vector<MappedBatch*> inflight_batches;
    inflight_batches.reserve(num_sequences);
    for (uint32_t i = 0; i < num_sequences; i++) {
        auto sequence = std::move(inflight_command_sequences_.front());
        inflight_batches.push_back(sequence.mapped_batch());

        // Pop off the front and push to the back
        inflight_command_sequences_.pop();
        inflight_command_sequences_.push(std::move(sequence));
    }
    return inflight_batches;
}
