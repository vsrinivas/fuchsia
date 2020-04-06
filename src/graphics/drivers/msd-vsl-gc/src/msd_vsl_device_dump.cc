// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include "command_buffer.h"
#include "msd_vsl_context.h"
#include "msd_vsl_device.h"
#include "registers.h"

namespace {

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

void MsdVslDevice::Dump(DumpState* dump_out, bool fault_present) {
  dump_out->max_completed_sequence_number = max_completed_sequence_number_;
  dump_out->next_sequence_number = next_sequence_number_;
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

void MsdVslDevice::DumpToString(std::vector<std::string>* dump_out, bool fault_present) {
  DumpState dump_state;
  Dump(&dump_state, fault_present);
  FormatDump(&dump_state, dump_out);
}

void MsdVslDevice::OutputFormattedString(std::vector<std::string>* dump_out, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int size = std::vsnprintf(nullptr, 0, fmt, args);
  std::vector<char> buf(size + 1);
  std::vsnprintf(buf.data(), buf.size(), fmt, args);
  dump_out->push_back(&buf[0]);
  va_end(args);
}

void MsdVslDevice::FormatDump(DumpState* dump_state, std::vector<std::string>* dump_out) {
  dump_out->clear();

  const char* build = magma::kDebug ? "DEBUG" : "RELEASE";
  const char* fmt =
      "---- GPU dump begin ----\n"
      "%s build\n"
      "Device id: 0x%x Revision: 0x%x\n"
      "max_completed_sequence_number: %lu\n"
      "next_sequence_number: %lu\n"
      "idle: %s";
  OutputFormattedString(dump_out, fmt, build, device_id(), revision(),
                        dump_state->max_completed_sequence_number, dump_state->next_sequence_number,
                        dump_state->idle ? "true" : "false");

  // We are only interested in the execution address if the device has started executing batches
  // and the page table arrays have been enabled.
  if (dump_state->page_table_arrays_enabled) {
    fmt = "current_execution_address: 0x%x";
    OutputFormattedString(dump_out, fmt, dump_state->exec_addr);
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

  dump_out->push_back("---- GPU dump end ----");
}
