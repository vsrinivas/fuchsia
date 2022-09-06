// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_status_page.h"

#include "magma_util/dlog.h"
#include "platform_barriers.h"

void GlobalHardwareStatusPage::ReadContextStatus(uint64_t& read_index,
                                                 std::optional<bool>* idle_out) {
  auto context_status_ptr = reinterpret_cast<volatile uint64_t*>(cpu_addr_) + 8;

  uint64_t last_written_status_index = context_status_ptr[7] >> 32;
  DASSERT((last_written_status_index & ~0x7) == 0);

  int64_t count = last_written_status_index - read_index;

  if (count < 0) {
    count += 6;
  }

  for (int64_t i = 0; i < count; i++) {
    uint32_t index = (read_index + 1 + i) % 6;

    uint64_t status = context_status_ptr[index];

    if (status & 1)
      *idle_out = false;

    else if (status & (1 << 3))
      *idle_out = true;
  }

  read_index = last_written_status_index;
}

constexpr uint32_t kStatusSentinel = ~0u;

void GlobalHardwareStatusPage::InitContextStatusGen12() {
  // Clear context status entries to a sentinel value
  for (uint32_t offset = kContextStatusStartOffset; offset <= kContextStatusEndOffsetGen12;
       offset += sizeof(uint64_t)) {
    write_context_status_gen12(offset, {kStatusSentinel, kStatusSentinel});
  }

  reinterpret_cast<volatile uint32_t*>(cpu_addr_)[kLastWrittenContextStatusOffsetGen12 >> 2] =
      kStatusQwordsGen12 - 1;

  // See "Workaround" below
  mapping_->buffer()->platform_buffer()->CleanCache(
      kContextStatusStartOffset, kLastWrittenContextStatusOffsetGen12 - kContextStatusStartOffset,
      true);
}

// Context Status
// https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol02d-commandreference-structures_0.pdf
// p.279
void GlobalHardwareStatusPage::ReadContextStatusGen12(uint64_t& read_index,
                                                      std::optional<bool>* idle_out) {
  uint64_t last_written_status_index =
      reinterpret_cast<volatile uint32_t*>(cpu_addr_)[kLastWrittenContextStatusOffsetGen12 >> 2];
  DASSERT((last_written_status_index & ~0xF) == 0);

  int64_t count = last_written_status_index - read_index;

  if (count < 0) {
    count += kStatusQwordsGen12;
  }

  auto name = id() == RENDER_COMMAND_STREAMER ? "RCS" : "VCS";

  DLOG("%s count %ld last_written_status_index %lu read_index %lu", name, count,
       last_written_status_index, read_index);

  magma::barriers::ReadBarrier();

  for (int64_t i = 0; i < count; i++) {
    // Each entry is two dwords (64 bits)
    uint32_t qword_index = (read_index + 1 + i) % kStatusQwordsGen12;

    uint32_t offset = kContextStatusStartOffset + qword_index * sizeof(uint64_t);

    std::pair<uint32_t, uint32_t> status = read_context_status_gen12(offset);

    uint16_t context_id_next = (status.first >> 15) & 0x7FF;
    uint16_t context_id_prev = (status.second >> 15) & 0x7FF;

    DLOG("%s: read context status[%u] 0x%08x 0x%08x context_id_prev 0x%x context_id_next 0x%x",
         name, qword_index, status.first, status.second, context_id_prev, context_id_next);

    if (status.first == kStatusSentinel || status.second == kStatusSentinel) {
      MAGMA_LOG(WARNING, "%s: got sentinel status[%u] 0x%08x 0x%08x", name, qword_index,
                status.first, status.second);
      continue;
    }

    write_context_status_gen12(offset, {kStatusSentinel, kStatusSentinel});

    constexpr uint16_t kContextIdIdle = 0x7FF;
    *idle_out = (context_id_next == kContextIdIdle);
  }

  read_index = last_written_status_index;

  // Workaround for HW issue "CSB data in hw status page may be stale..."
  // https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-tgl-vol14-workarounds.pdf
  // p.28
  mapping_->buffer()->platform_buffer()->CleanCache(
      kContextStatusStartOffset, kLastWrittenContextStatusOffsetGen12 - kContextStatusStartOffset,
      true);
}
