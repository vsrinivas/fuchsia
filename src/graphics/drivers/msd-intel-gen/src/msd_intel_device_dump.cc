// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include "instruction_decoder.h"
#include "msd_intel_device.h"
#include "registers.h"

void MsdIntelDevice::Dump(DumpState* dump_out) {
  dump_out->render_cs.sequence_number =
      global_context_->hardware_status_page(render_engine_cs_->id())->read_sequence_number();
  dump_out->render_cs.active_head_pointer = render_engine_cs_->GetActiveHeadPointer();
  dump_out->render_cs.inflight_batches = render_engine_cs_->GetInflightBatches();

  DumpFault(dump_out, registers::AllEngineFault::read(register_io_.get()));

  dump_out->fault_gpu_address = kInvalidGpuAddr;
  dump_out->global = false;
  if (dump_out->fault_present)
    DumpFaultAddress(dump_out, register_io_.get());
}

void MsdIntelDevice::DumpFault(DumpState* dump_out, uint32_t fault) {
  dump_out->fault_present = registers::AllEngineFault::valid(fault);
  dump_out->fault_engine = registers::AllEngineFault::engine(fault);
  dump_out->fault_src = registers::AllEngineFault::src(fault);
  dump_out->fault_type = registers::AllEngineFault::type(fault);
}

void MsdIntelDevice::DumpFaultAddress(DumpState* dump_out, magma::RegisterIo* register_io) {
  uint64_t val = registers::FaultTlbReadData::read(register_io);
  dump_out->fault_gpu_address = registers::FaultTlbReadData::addr(val);
  dump_out->global = registers::FaultTlbReadData::is_ggtt(val);
}

void MsdIntelDevice::DumpToString(std::vector<std::string>& dump_out) {
  DumpState dump_state;
  Dump(&dump_state);
  FormatDump(dump_state, dump_out);
}

void MsdIntelDevice::FormatDump(DumpState& dump_state, std::vector<std::string>& dump_out) {
  dump_out.clear();

  const char* build = magma::kDebug ? "DEBUG" : "RELEASE";
  const char* fmt =
      "---- GPU dump begin ----\n"
      "%s build\n"
      "Device id: 0x%x Revision: 0x%x\n"
      "RENDER_COMMAND_STREAMER\n"
      "sequence_number 0x%x\n"
      "active head pointer: 0x%llx";
  int size =
      std::snprintf(nullptr, 0, fmt, build, device_id(), revision(),
                    dump_state.render_cs.sequence_number, dump_state.render_cs.active_head_pointer);
  std::vector<char> buf(size + 1);
  std::snprintf(&buf[0], buf.size(), fmt, build, device_id(), revision(),
                dump_state.render_cs.sequence_number, dump_state.render_cs.active_head_pointer);
  dump_out.push_back(&buf[0]);

  if (dump_state.fault_present) {
    fmt =
        "ENGINE FAULT DETECTED\n"
        "engine 0x%x src 0x%x type 0x%x gpu_address 0x%lx global %d";
    size = std::snprintf(nullptr, 0, fmt, dump_state.fault_engine, dump_state.fault_src,
                         dump_state.fault_type, dump_state.fault_gpu_address, dump_state.global);
    std::vector<char> buf(size + 1);
    std::snprintf(&buf[0], buf.size(), fmt, dump_state.fault_engine, dump_state.fault_src,
                  dump_state.fault_type, dump_state.fault_gpu_address, dump_state.global);
    dump_out.push_back(&buf[0]);
  } else {
    dump_out.push_back("No engine faults detected.");
  }

  bool is_mapped = false;
  std::vector<GpuMappingView*> mappings;
  GpuMappingView* fault_mapping;
  GpuMappingView* closest_mapping;
  const GpuMappingView* faulted_batch_mapping = nullptr;
  uint64_t closest_mapping_distance = UINT64_MAX;

  if (!dump_state.render_cs.inflight_batches.empty()) {
    dump_out.push_back("Inflight Batches:");
    for (auto batch : dump_state.render_cs.inflight_batches) {
      fmt = "  Batch %p, context %p, connection client_id %lu";
      auto context = batch->GetContext().lock().get();
      auto connection = context ? context->connection().lock() : nullptr;
      size =
          std::snprintf(nullptr, 0, fmt, batch, context, connection ? connection->client_id() : 0u);
      std::vector<char> buf(size + 1);
      std::snprintf(&buf[0], buf.size(), fmt, batch, context,
                    connection ? connection->client_id() : 0u);
      dump_out.push_back(&buf[0]);

      auto batch_mapping = batch->GetBatchMapping();
      if (!batch_mapping)
        continue;

      if (dump_state.render_cs.active_head_pointer >= batch_mapping->gpu_addr() &&
          dump_state.render_cs.active_head_pointer <
              batch_mapping->gpu_addr() + batch_mapping->length()) {
        dump_out.push_back("  FAULTING BATCH (active head ptr within this batch)");
        faulted_batch_mapping = batch_mapping;
      }

      if (!batch->IsCommandBuffer())
        continue;

      auto cmd_buf = static_cast<CommandBuffer*>(batch);
      cmd_buf->GetMappings(&mappings);
      for (const auto& mapping : mappings) {
        fmt =
            "    Mapping %p, buffer 0x%lx, gpu addr range [0x%lx, 0x%lx), "
            "offset 0x%lx, mapping length 0x%lx";
        gpu_addr_t mapping_start = mapping->gpu_addr();
        gpu_addr_t mapping_end = mapping->gpu_addr() + mapping->length();
        size = std::snprintf(nullptr, 0, fmt, mapping, mapping->BufferId(), mapping_start,
                             mapping_end, mapping->offset(), mapping->length());
        std::vector<char> buf(size + 1);
        std::snprintf(&buf[0], buf.size(), fmt, mapping, mapping->BufferId(), mapping_start,
                      mapping_end, mapping->offset(), mapping->length());
        dump_out.push_back(&buf[0]);
        if (dump_state.fault_gpu_address >= mapping_start &&
            dump_state.fault_gpu_address < mapping_end) {
          is_mapped = true;
          fault_mapping = mapping;
        } else if (dump_state.fault_gpu_address > mapping_end &&
                   dump_state.fault_gpu_address - mapping_end < closest_mapping_distance) {
          closest_mapping_distance = dump_state.fault_gpu_address - mapping_end;
          closest_mapping = mapping;
        }
      }
    }
  }

  if (is_mapped) {
    fmt = "Fault address appears to be within mapping %p addr [0x%lx, 0x%lx)";
    size = std::snprintf(nullptr, 0, fmt, fault_mapping, fault_mapping->gpu_addr(),
                         fault_mapping->gpu_addr() + fault_mapping->length());
    std::vector<char> buf(size + 1);
    std::snprintf(&buf[0], buf.size(), fmt, fault_mapping, fault_mapping->gpu_addr(),
                  fault_mapping->gpu_addr() + fault_mapping->length());
    dump_out.push_back(&buf[0]);
  } else {
    dump_out.push_back("Fault address does not appear to be mapped for any outstanding batch");
    if (closest_mapping_distance < UINT64_MAX) {
      fmt =
          "Fault address is 0x%lx past the end of mapping %p addr [0x%08lx, 0x%08lx), size "
          "0x%lx, buffer size 0x%lx";
      size = std::snprintf(nullptr, 0, fmt, closest_mapping_distance, closest_mapping,
                           closest_mapping->gpu_addr(),
                           closest_mapping->gpu_addr() + closest_mapping->length(),
                           closest_mapping->length(), closest_mapping->BufferSize());
      std::vector<char> buf(size + 1);
      std::snprintf(&buf[0], buf.size(), fmt, closest_mapping_distance, closest_mapping,
                    closest_mapping->gpu_addr(),
                    closest_mapping->gpu_addr() + closest_mapping->length(),
                    closest_mapping->length(), closest_mapping->BufferSize());
      dump_out.push_back(&buf[0]);
    }
  }

  if (faulted_batch_mapping) {
    dump_out.push_back("Batch instructions immediately surrounding the active head:");
    std::vector<uint32_t> batch_data;
    // dont early out because we always want to print the "dump end" line
    if (faulted_batch_mapping->Copy(&batch_data)) {
      uint64_t active_head_offset =
          dump_state.render_cs.active_head_pointer - faulted_batch_mapping->gpu_addr();
      DASSERT(active_head_offset <= faulted_batch_mapping->length());
      DASSERT(active_head_offset % sizeof(uint32_t) == 0);
      uint64_t total_dwords = faulted_batch_mapping->length() / sizeof(uint32_t);
      uint64_t active_head_dword = active_head_offset / sizeof(uint32_t);
      uint64_t start_dword = faulted_batch_mapping->offset();
      uint64_t end_dword = total_dwords - 1;

      uint32_t dwords_remaining = 0;
      bool end_of_batch = false;
      for (uint64_t i = start_dword; i < end_dword; i++) {
        if (dwords_remaining == 0) {
          InstructionDecoder::Id id;
          bool decoded = InstructionDecoder::Decode(batch_data[i], &id, &dwords_remaining);
          if (decoded) {
            fmt = "%s: ";
            size = std::snprintf(nullptr, 0, fmt, InstructionDecoder::name(id));
            buf = std::vector<char>(size + 1);
            std::snprintf(&buf[0], buf.size(), fmt, InstructionDecoder::name(id));
            dump_out.push_back(&buf[0]);
            end_of_batch = id == InstructionDecoder::Id::MI_BATCH_BUFFER_END;
          }
        }

        const char* prefix = "";
        const char* suffix = ",";
        if (i == active_head_dword) {
          prefix = "===>";
          suffix = "<===,";
        }
        if (dwords_remaining)
          --dwords_remaining;

        fmt = "%s0x%08lx%s";
        size = std::snprintf(nullptr, 0, fmt, prefix, batch_data[i], suffix);
        std::vector<char> buf(size + 1);
        std::snprintf(&buf[0], buf.size(), fmt, prefix, batch_data[i], suffix);
        dump_out.push_back(&buf[0]);

        if (end_of_batch)
          break;
      }
    } else {
      dump_out.push_back("Failed to map batch data");
    }
  }

  dump_out.push_back("---- GPU dump end ----");
}
