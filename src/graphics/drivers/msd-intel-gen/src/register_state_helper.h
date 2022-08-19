// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_INTEL_GEN_SRC_REGISTER_STATE_HELPER_H
#define SRC_GRAPHICS_DRIVERS_MSD_INTEL_GEN_SRC_REGISTER_STATE_HELPER_H

#include "device_id.h"
#include "instructions.h"
#include "magma_util/macros.h"
#include "types.h"

// Helper classes for initializing the register state in a context state buffer.
// RegisterStateHelper has state initialization which is common to supported hardware;
// derived classes below provide everything needed for particular hardware.
struct RegisterStateHelper {
  static void* register_context_base(void* context_buffer) {
    return static_cast<uint8_t*>(context_buffer) + magma::page_size();
  }

  RegisterStateHelper(EngineCommandStreamerId id, uint32_t mmio_base, uint32_t* state)
      : id_(id), mmio_base_(mmio_base), state_(state) {}

  virtual ~RegisterStateHelper() = default;

  virtual void write_load_register_immediate_headers() { DASSERT(false); }
  virtual void write_second_level_batch_buffer_upper_head_pointer() { DASSERT(false); }
  virtual void write_second_level_batch_buffer_head_pointer() { DASSERT(false); }
  virtual void write_second_level_batch_buffer_state() { DASSERT(false); }
  virtual void write_batch_buffer_per_context_pointer() { DASSERT(false); }
  virtual void write_indirect_context_pointer(uint32_t gpu_addr, uint32_t size) { DASSERT(false); }
  virtual void write_indirect_context_offset(uint32_t context_offset) { DASSERT(false); }
  virtual void write_ccid() { DASSERT(false); }
  virtual void write_semaphore_token() { DASSERT(false); }

  // CTXT_SR_CTL - Context Save/Restore Control Register
  void write_context_save_restore_control() {
    state_[0x2] = mmio_base_ + 0x244;

    constexpr uint32_t kInhibitSyncContextSwitchBit = 1 << 3;
    // The helper only populates part of the context state image; the rest is stored
    // on context save, and that part should be not be loaded initially.
    constexpr uint32_t kContextRestoreInhibitBit = 1;
    constexpr uint32_t kBits = kInhibitSyncContextSwitchBit | kContextRestoreInhibitBit;

    state_[0x3] = (kBits << 16) | kBits;
  }

  // RING_BUFFER_HEAD - Ring Buffer Head
  void write_ring_head_pointer(uint32_t head) {
    state_[0x4] = mmio_base_ + 0x34;
    state_[0x5] = head;
  }

  // RING_BUFFER_TAIL - Ring Buffer Tail
  void write_ring_tail_pointer(uint32_t tail) {
    state_[0x6] = mmio_base_ + 0x30;
    state_[0x7] = tail;
  }

  // RING_BUFFER_START - Ring Buffer Start
  void write_ring_buffer_start(uint32_t gtt_ring_buffer_start) {
    DASSERT(magma::is_page_aligned(gtt_ring_buffer_start));
    state_[0x8] = mmio_base_ + 0x38;
    state_[0x9] = gtt_ring_buffer_start;
  }

  // RING_BUFFER_CTL - Ring Buffer Control
  void write_ring_buffer_control(uint32_t ringbuffer_size) {
    constexpr uint32_t kRingValid = 1;
    DASSERT(ringbuffer_size >= magma::page_size() && ringbuffer_size <= 512 * magma::page_size());
    DASSERT(magma::is_page_aligned(ringbuffer_size));
    state_[0xA] = mmio_base_ + 0x3C;
    // This register assumes 4k pages
    DASSERT(magma::page_size() == 4096);
    state_[0xB] = (ringbuffer_size - magma::page_size()) | kRingValid;
  }

  // BB_ADDR_UDW - Batch Buffer Upper Head Pointer Register
  void write_batch_buffer_upper_head_pointer() {
    state_[0xC] = mmio_base_ + 0x168;
    state_[0xD] = 0;
  }

  // BB_ADDR - Batch Buffer Head Pointer Register
  void write_batch_buffer_head_pointer() {
    state_[0xE] = mmio_base_ + 0x140;
    state_[0xF] = 0;
  }

  // BB_STATE - Batch Buffer State Register
  void write_batch_buffer_state() {
    constexpr uint32_t kAddressSpacePpgtt = 1 << 5;
    state_[0x10] = mmio_base_ + 0x110;
    state_[0x11] = kAddressSpacePpgtt;
  }

  // CS_CTX_TIMESTAMP - CS Context Timestamp Count
  void write_context_timestamp() {
    state_[0x22] = mmio_base_ + 0x3A8;
    state_[0x23] = 0;
  }

  void write_pdp3_upper(uint64_t pdp_bus_addr) {
    state_[0x24] = mmio_base_ + 0x28C;
    state_[0x25] = magma::upper_32_bits(pdp_bus_addr);
  }

  void write_pdp3_lower(uint64_t pdp_bus_addr) {
    state_[0x26] = mmio_base_ + 0x288;
    state_[0x27] = magma::lower_32_bits(pdp_bus_addr);
  }

  void write_pdp2_upper(uint64_t pdp_bus_addr) {
    state_[0x28] = mmio_base_ + 0x284;
    state_[0x29] = magma::upper_32_bits(pdp_bus_addr);
  }

  void write_pdp2_lower(uint64_t pdp_bus_addr) {
    state_[0x2A] = mmio_base_ + 0x280;
    state_[0x2B] = magma::lower_32_bits(pdp_bus_addr);
  }

  void write_pdp1_upper(uint64_t pdp_bus_addr) {
    state_[0x2C] = mmio_base_ + 0x27C;
    state_[0x2D] = magma::upper_32_bits(pdp_bus_addr);
  }

  void write_pdp1_lower(uint64_t pdp_bus_addr) {
    state_[0x2E] = mmio_base_ + 0x278;
    state_[0x2F] = magma::lower_32_bits(pdp_bus_addr);
  }

  void write_pdp0_upper(uint64_t pdp_bus_addr) {
    state_[0x30] = mmio_base_ + 0x274;
    state_[0x31] = magma::upper_32_bits(pdp_bus_addr);
  }

  void write_pdp0_lower(uint64_t pdp_bus_addr) {
    state_[0x32] = mmio_base_ + 0x270;
    state_[0x33] = magma::lower_32_bits(pdp_bus_addr);
  }

  // R_PWR_CLK_STATE - Render Power Clock State Register
  void write_render_power_clock_state() {
    DASSERT(id_ == RENDER_COMMAND_STREAMER);
    state_[0x42] = mmio_base_ + 0x0C8;
    state_[0x43] = 0;
  }

  EngineCommandStreamerId id_;
  uint32_t mmio_base_;
  uint32_t* state_;
};

// Render command streamer pp.25:
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-kbl-vol07-3d_media_gpgpu.pdf
// Video command streamer pp.15:
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-kbl-vol03-gpu_overview.pdf
struct RegisterStateHelperGen9 : public RegisterStateHelper {
  // From INDIRECT_CTX_OFFSET register, p.1070:
  // https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-kbl-vol02c-commandreference-registers-part1.pdf
  static constexpr uint64_t kIndirectContextOffsetGen9 = 0x26;

  RegisterStateHelperGen9(EngineCommandStreamerId id, uint32_t mmio_base, uint32_t* state)
      : RegisterStateHelper(id, mmio_base, state) {}

  void write_load_register_immediate_headers() override {
    // Loads are at odd indices because no-op precedes each.
    state_[0x1] = MiLoadDataImmediate::header(14, /*force_posted=*/true);
    DASSERT(state_[0x1] == 0x1100101B);

    state_[0x21] = MiLoadDataImmediate::header(9, /*force_posted=*/true);
    DASSERT(state_[0x21] == 0x11001011);

    switch (id_) {
      case RENDER_COMMAND_STREAMER:
        state_[0x41] = MiLoadDataImmediate::header(1, /*force_posted=*/false);
        DASSERT(state_[0x41] == 0x11000001);
        break;
      case VIDEO_COMMAND_STREAMER:
        break;
    }
  }

  // SBB_ADDR_UDW - Second Level Batch Buffer Upper Head Pointer Register
  void write_second_level_batch_buffer_upper_head_pointer() override {
    state_[0x12] = mmio_base_ + 0x11C;
    state_[0x13] = 0;
  }

  // SBB_ADDR - Second Level Batch Buffer Head Pointer Register
  void write_second_level_batch_buffer_head_pointer() override {
    state_[0x14] = mmio_base_ + 0x114;
    state_[0x15] = 0;
  }

  // SBB_STATE - Second Level Batch Buffer State Register
  void write_second_level_batch_buffer_state() override {
    state_[0x16] = mmio_base_ + 0x118;
    state_[0x17] = 0;
  }

  // BB_PER_CTX_PTR - Batch Buffer Per Context Pointer
  void write_batch_buffer_per_context_pointer() override {
    state_[0x18] = mmio_base_ + 0x1C0;
    state_[0x19] = 0;
  }

  // INDIRECT_CTX - Indirect Context Pointer
  void write_indirect_context_pointer(uint32_t gpu_addr, uint32_t size) override {
    DASSERT((gpu_addr & 0x3F) == 0);
    uint32_t size_in_cache_lines = size / DeviceId::cache_line_size();
    DASSERT(size_in_cache_lines < 64);
    state_[0x1A] = mmio_base_ + 0x1C4;
    state_[0x1B] = gpu_addr | size_in_cache_lines;
  }

  // INDIRECT_CTX_OFFSET - Indirect Context Offset
  void write_indirect_context_offset(uint32_t context_offset) override {
    DASSERT((context_offset & ~0x3FF) == 0);
    state_[0x1C] = mmio_base_ + 0x1C8;
    state_[0x1D] = context_offset << 6;
  }

  void write_ccid() override {}
  void write_semaphore_token() override {}
};

// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol08-command_stream_programming_0.pdf
// p.49
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol09-renderengine_0.pdf
// p.54
struct RegisterStateHelperGen12 : public RegisterStateHelper {
  // From INDIRECT_CTX_OFFSET register, p.1245:
  // https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02c-commandreference-registers-part1_0.pdf
  static constexpr uint64_t kIndirectContextOffsetGen12 = 0x0D;

  RegisterStateHelperGen12(EngineCommandStreamerId id, uint32_t mmio_base, uint32_t* state)
      : RegisterStateHelper(id, mmio_base, state) {}

  void write_load_register_immediate_headers() override {
    // Loads are at odd indices because no-op precedes each.
    state_[0x1] =
        MiLoadDataImmediate::header(13, /*force_posted=*/true) | MiLoadDataImmediate::kAddMmioBase;
    DASSERT(state_[0x1] == 0x11081019);

    state_[0x21] =
        MiLoadDataImmediate::header(9, /*force_posted=*/true) | MiLoadDataImmediate::kAddMmioBase;
    DASSERT(state_[0x21] == 0x11081011);

    switch (id_) {
      case RENDER_COMMAND_STREAMER:
        state_[0x41] = MiLoadDataImmediate::header(1, /*force_posted=*/false) |
                       MiLoadDataImmediate::kAddMmioBase;
        DASSERT(state_[0x41] == 0x11080001);
        break;
      case VIDEO_COMMAND_STREAMER:
        break;
    }
  }

  // BB_PER_CTX_PTR - Batch Buffer Per Context Pointer
  void write_batch_buffer_per_context_pointer() override {
    state_[0x12] = mmio_base_ + 0x1C0;
    state_[0x13] = 0;
  }

  // INDIRECT_CTX - Indirect Context Pointer
  void write_indirect_context_pointer(uint32_t gpu_addr, uint32_t size) override {
    DASSERT((gpu_addr & 0x3F) == 0);
    uint32_t size_in_cache_lines = size / DeviceId::cache_line_size();
    DASSERT(size_in_cache_lines < 64);
    state_[0x14] = mmio_base_ + 0x1C4;
    state_[0x15] = gpu_addr | size_in_cache_lines;
  }

  // INDIRECT_CTX_OFFSET - Indirect Context Offset
  void write_indirect_context_offset(uint32_t context_offset) override {
    DASSERT((context_offset & ~0x3FF) == 0);
    state_[0x16] = mmio_base_ + 0x1C8;
    state_[0x17] = context_offset << 6;
  }

  // CCID
  void write_ccid() override {
    state_[0x18] = mmio_base_ + 0x180;
    state_[0x19] = 0;
  }

  // SEMAPHORE_TOKEN
  void write_semaphore_token() override {
    state_[0x1A] = mmio_base_ + 0x2B4;
    state_[0x1B] = 0;
  }

  void write_second_level_batch_buffer_upper_head_pointer() override {}
  void write_second_level_batch_buffer_head_pointer() override {}
  void write_second_level_batch_buffer_state() override {}
};

#endif
