// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include "address_space_layout.h"
#include "command_buffer.h"
#include "msd_vsi_context.h"
#include "msd_vsi_device.h"
#include "registers.h"

namespace {

class InstructionDecoder {
 public:
  enum Command {
    LOAD_STATE = 0x801,
    END = 0x1000,
    WAIT = 0x3800,
    LINK = 0x4000,
    STALL = 0x4800,
  };

  enum RegisterIndex {
    EVENT = 0xE01,
    SEMAPHORE = 0xE02,
  };

  static const char* name(Command command, uint16_t value) {
    switch (command) {
      case END:
        return "END";
      case LINK:
        return "LINK";
      case LOAD_STATE:
        switch (value) {
          case EVENT:
            return "EVENT";
          case SEMAPHORE:
            return "SEMAPHORE";
        }
        return "LOAD_STATE";
      case STALL:
        return "STALL";
      case WAIT:
        return "WAIT";
    }
    return "UNKNOWN";
  }

  static void Decode(uint32_t dword, Command* command_out, uint16_t* value_out,
                     uint32_t* dword_count_out) {
    uint16_t command = dword >> 16;
    uint16_t value = dword & 0xffff;
    // Currently all supported instructions appear to be 8-byte aligned.
    *dword_count_out = 2;
    *command_out = static_cast<Command>(command);
    *value_out = value;
  }
};

const char* FaultTypeToString(uint32_t mmu_status) {
  switch (mmu_status) {
    case 1:
      return "slave not present";
    case 2:
      return "page not present";
    case 3:
      return "write violation";
    case 4:
      return "out of bound";
    case 5:
      return "read security violation";
    case 6:
      return "write security violation";
  }
  return "unknown mmu status";
}

}  // namespace

void MsdVsiDevice::Dump(DumpState* dump_out, bool fault_present) {
  dump_out->last_completed_sequence_number = progress_->last_completed_sequence_number();
  dump_out->last_submitted_sequence_number = progress_->last_submitted_sequence_number();
  dump_out->idle = IsIdle();
  dump_out->page_table_arrays_enabled = page_table_arrays_->IsEnabled(register_io());
  dump_out->exec_addr = registers::DmaAddress::Get().ReadFrom(register_io_.get()).reg_value();
  dump_out->inflight_batches = GetInflightBatches();

  dump_out->fault_present = fault_present;
  if (fault_present) {
    dump_out->fault_type =
        registers::MmuSecureStatus::Get().ReadFrom(register_io_.get()).reg_value();
    dump_out->fault_gpu_address =
        registers::MmuSecureExceptionAddress::Get().ReadFrom(register_io_.get()).reg_value();
  }
}

void MsdVsiDevice::DumpToString(std::vector<std::string>* dump_out, bool fault_present) {
  DumpState dump_state;
  Dump(&dump_state, fault_present);
  FormatDump(&dump_state, dump_out);
}

void MsdVsiDevice::OutputFormattedString(std::vector<std::string>* dump_out, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int size = std::vsnprintf(nullptr, 0, fmt, args);
  std::vector<char> buf(size + 1);
  std::vsnprintf(buf.data(), buf.size(), fmt, args);
  dump_out->push_back(&buf[0]);
  va_end(args);
}

void MsdVsiDevice::DumpDecodedBuffer(std::vector<std::string>* dump_out, uint32_t* buf,
                                     uint32_t buf_size_dwords, uint32_t start_dword,
                                     uint32_t dword_count, uint32_t active_head_dword) {
  DASSERT(buf_size_dwords > 0);

  uint32_t dwords_remaining = 0;
  const char* fmt = "";
  for (unsigned int i = 0; i < dword_count; i++) {
    unsigned int buf_idx = start_dword + i;
    // Support circular buffers.
    if (buf_idx >= buf_size_dwords) {
      buf_idx -= buf_size_dwords;
    }
    if (dwords_remaining == 0) {
      InstructionDecoder::Command command;
      uint16_t value;
      InstructionDecoder::Decode(buf[buf_idx], &command, &value, &dwords_remaining);
      fmt = "%-25s [0x%x]";
      OutputFormattedString(dump_out, fmt, InstructionDecoder::name(command, value),
                            buf_idx * sizeof(uint32_t));
    }

    const char* prefix = "";
    const char* suffix = ",";
    if (buf_idx == active_head_dword) {
      prefix = "===> ";
      suffix = " <===,";
    }
    if (dwords_remaining) {
      --dwords_remaining;
    }
    fmt = "  %s0x%08lx%s";
    OutputFormattedString(dump_out, fmt, prefix, buf[buf_idx], suffix);
  }
}

void MsdVsiDevice::FormatDump(DumpState* dump_state, std::vector<std::string>* dump_out) {
  dump_out->clear();

  const char* build = magma::kDebug ? "DEBUG" : "RELEASE";
  const char* fmt = "---- GPU dump begin ----";
  OutputFormattedString(dump_out, fmt);
  fmt = "%s build";
  OutputFormattedString(dump_out, fmt, build);
  fmt = "Device id: 0x%x Revision: 0x%x";
  OutputFormattedString(dump_out, fmt, device_id());
  fmt = "last_completed_sequence_number: %lu";
  OutputFormattedString(dump_out, fmt, dump_state->last_completed_sequence_number);
  fmt = "last_submitted_sequence_number: %lu";
  OutputFormattedString(dump_out, fmt, dump_state->last_submitted_sequence_number);
  fmt = "idle: %s";
  OutputFormattedString(dump_out, fmt, dump_state->idle ? "true" : "false");

  const char* gpu_addr_location_desc = "client address";
  bool in_ringbuffer = false;
  if (!AddressSpaceLayout::IsValidClientGpuRange(dump_state->exec_addr, dump_state->exec_addr)) {
    uint32_t offset = dump_state->exec_addr - AddressSpaceLayout::system_gpu_addr_base();
    if (offset < AddressSpaceLayout::ringbuffer_size()) {
      in_ringbuffer = true;
      gpu_addr_location_desc = "in ringbuffer";
    } else {
      gpu_addr_location_desc = "past end of ringbuffer";
    }
  }

  // We are only interested in the execution address if the device has started executing batches
  // and the page table arrays have been enabled.
  if (dump_state->page_table_arrays_enabled) {
    fmt = "current_execution_address: 0x%x (%s)";
    OutputFormattedString(dump_out, fmt, dump_state->exec_addr, gpu_addr_location_desc);
  } else {
    dump_out->push_back("current_execution_address: N/A (page table arrays not yet enabled)");
  }

  if (dump_state->fault_present) {
    fmt =
        "MMU EXCEPTION DETECTED\n"
        "type 0x%x (%s) gpu_address 0x%lx";
    OutputFormattedString(dump_out, fmt, dump_state->fault_type,
                          FaultTypeToString(dump_state->fault_type), dump_state->fault_gpu_address);
  } else {
    dump_out->push_back("No mmu exception detected.");
  }

  std::vector<GpuMappingView*> mappings;
  GpuMappingView* fault_mapping = nullptr;
  GpuMappingView* closest_mapping = nullptr;
  uint64_t closest_mapping_distance = UINT64_MAX;

  if (!dump_state->inflight_batches.empty()) {
    dump_out->push_back("Inflight Batches:");
    for (auto batch : dump_state->inflight_batches) {
      fmt = "  Batch %lu (%s) %p, context %p, connection client_id %lu";
      auto batch_type = batch->IsCommandBuffer() ? "Command" : "Event";
      auto context = batch->GetContext().lock().get();
      auto connection = context ? context->connection().lock() : nullptr;
      OutputFormattedString(dump_out, fmt, batch->GetSequenceNumber(), batch_type, batch, context,
                            connection ? connection->client_id() : 0u);

      auto batch_mapping = batch->GetBatchMapping();
      if (!batch_mapping) {
        continue;
      }

      if (dump_state->fault_present && dump_state->exec_addr >= batch_mapping->gpu_addr() &&
          dump_state->exec_addr < batch_mapping->gpu_addr() + batch_mapping->length()) {
        dump_out->push_back("  FAULTING BATCH (current exec addr within this batch)");
      }

      if (!batch->IsCommandBuffer()) {
        continue;
      }

      auto cmd_buf = static_cast<CommandBuffer*>(batch);

      fmt = "    Exec Gpu Address 0x%lx";
      OutputFormattedString(dump_out, fmt, cmd_buf->GetGpuAddress());

      cmd_buf->GetMappings(&mappings);
      for (const auto& mapping : mappings) {
        fmt =
            "    Mapping %p, buffer 0x%lx, gpu addr range [0x%lx, 0x%lx), "
            "offset 0x%lx, mapping length 0x%lx";
        uint64_t mapping_start = mapping->gpu_addr();
        uint64_t mapping_end = mapping->gpu_addr() + mapping->length();
        OutputFormattedString(dump_out, fmt, mapping, mapping->BufferId(), mapping_start,
                              mapping_end, mapping->offset(), mapping->length());

        if (!dump_state->fault_present) {
          continue;
        }

        if (dump_state->fault_gpu_address >= mapping_start &&
            dump_state->fault_gpu_address < mapping_end) {
          fault_mapping = mapping;
        } else if (dump_state->fault_gpu_address > mapping_end &&
                   dump_state->fault_gpu_address - mapping_end < closest_mapping_distance) {
          closest_mapping_distance = dump_state->fault_gpu_address - mapping_end;
          closest_mapping = mapping;
        }
      }
    }
  }

  if (fault_mapping) {
    fmt = "Fault address appears to be within mapping %p addr [0x%lx, 0x%lx)";
    OutputFormattedString(dump_out, fmt, fault_mapping, fault_mapping->gpu_addr(),
                          fault_mapping->gpu_addr() + fault_mapping->length());
  } else if (dump_state->fault_present) {
    dump_out->push_back("Fault address does not appear to be mapped for any outstanding batch");
    if (closest_mapping_distance < UINT64_MAX) {
      fmt =
          "Fault address is 0x%lx past the end of mapping %p addr [0x%08lx, 0x%08lx), size "
          "0x%lx, buffer size 0x%lx";
      OutputFormattedString(dump_out, fmt, closest_mapping_distance, closest_mapping,
                            closest_mapping->gpu_addr(),
                            closest_mapping->gpu_addr() + closest_mapping->length(),
                            closest_mapping->length(), closest_mapping->BufferSize());
    }
  }

  if (in_ringbuffer) {
    dump_out->push_back("Ringbuffer dump from last completed event:");

    uint32_t rb_offset = dump_state->exec_addr - AddressSpaceLayout::system_gpu_addr_base();
    DASSERT(rb_offset % sizeof(uint32_t) == 0);
    uint32_t active_head_dword = rb_offset / sizeof(uint32_t);

    uint32_t dword_count = ringbuffer_->UsedSize() / sizeof(uint32_t);
    fmt = "(base 0x%x, dump starts at offset 0x%x)";
    OutputFormattedString(dump_out, fmt, AddressSpaceLayout::system_gpu_addr_base(),
                          ringbuffer_->head());
    DumpDecodedBuffer(dump_out, ringbuffer_->Buffer(), ringbuffer_->size() / sizeof(uint32_t),
                      ringbuffer_->head() / sizeof(uint32_t) /* start_dword */, dword_count,
                      active_head_dword);
  }

  dump_out->push_back("---- GPU dump end ----");
}
