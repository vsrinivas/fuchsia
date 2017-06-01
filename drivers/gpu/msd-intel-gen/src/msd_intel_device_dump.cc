// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_device.h"
#include "registers.h"
#include <memory>
#include <string>

void MsdIntelDevice::Dump(DumpState* dump_out)
{
    dump_out->render_cs.sequence_number =
        global_context_->hardware_status_page(render_engine_cs_->id())->read_sequence_number();
    dump_out->render_cs.active_head_pointer = render_engine_cs_->GetActiveHeadPointer();
    dump_out->render_cs.inflight_batches = render_engine_cs_->GetInflightBatches();

    DumpFault(dump_out, registers::AllEngineFault::read(register_io_.get()));

    dump_out->fault_gpu_address = kInvalidGpuAddr;
    if (dump_out->fault_present)
        DumpFaultAddress(dump_out, register_io_.get());
}

void MsdIntelDevice::DumpFault(DumpState* dump_out, uint32_t fault)
{
    dump_out->fault_present = registers::AllEngineFault::valid(fault);
    dump_out->fault_engine = registers::AllEngineFault::engine(fault);
    dump_out->fault_src = registers::AllEngineFault::src(fault);
    dump_out->fault_type = registers::AllEngineFault::type(fault);
}

void MsdIntelDevice::DumpFaultAddress(DumpState* dump_out, RegisterIo* register_io)
{
    dump_out->fault_gpu_address = registers::FaultTlbReadData::addr(register_io);
}

void MsdIntelDevice::DumpToString(std::string& dump_out)
{
    DumpState dump_state;
    Dump(&dump_state);

    const char* build = magma::kDebug ? "DEBUG" : "RELEASE";
    const char* fmt = "---- device dump begin ----\n"
                      "%s build\n"
                      "Device id: 0x%x\n"
                      "RENDER_COMMAND_STREAMER\n"
                      "sequence_number 0x%x\n"
                      "active head pointer: 0x%llx\n";
    int size =
        std::snprintf(nullptr, 0, fmt, build, device_id(), dump_state.render_cs.sequence_number,
                      dump_state.render_cs.active_head_pointer);
    std::vector<char> buf(size + 1);
    std::snprintf(&buf[0], buf.size(), fmt, build, device_id(),
                  dump_state.render_cs.sequence_number, dump_state.render_cs.active_head_pointer);
    dump_out.append(&buf[0]);

    if (dump_state.fault_present) {
        fmt = "ENGINE FAULT DETECTED\n"
              "engine 0x%x src 0x%x type 0x%x gpu_address 0x%lx\n";
        size = std::snprintf(nullptr, 0, fmt, dump_state.fault_engine, dump_state.fault_src,
                             dump_state.fault_type, dump_state.fault_gpu_address);
        std::vector<char> buf(size + 1);
        std::snprintf(&buf[0], buf.size(), fmt, dump_state.fault_engine, dump_state.fault_src,
                      dump_state.fault_type, dump_state.fault_gpu_address);
        dump_out.append(&buf[0]);
    } else {
        dump_out.append("No engine faults detected.\n");
    }

    bool is_mapped = false;
    std::shared_ptr<GpuMapping> fault_mapping;
    std::shared_ptr<GpuMapping> closest_mapping;
    GpuMapping* faulted_batch_mapping = nullptr;
    uint64_t closest_mapping_distance = UINT64_MAX;

    if (!dump_state.render_cs.inflight_batches.empty()) {
        dump_out.append("Inflight Batches:\n");
        for (auto batch : dump_state.render_cs.inflight_batches) {
            fmt = "  Batch %p, context %p\n";
            size = std::snprintf(nullptr, 0, fmt, batch, batch->GetContext().lock().get());
            std::vector<char> buf(size + 1);
            std::snprintf(&buf[0], buf.size(), fmt, batch, batch->GetContext().lock().get());
            dump_out.append(&buf[0]);

            auto batch_mapping = batch->GetBatchMapping();
            if (dump_state.render_cs.active_head_pointer >= batch_mapping->gpu_addr() &&
                dump_state.render_cs.active_head_pointer <
                    batch_mapping->gpu_addr() + batch_mapping->length()) {
                dump_out.append("  FAULTING BATCH (active head ptr within this batch)\n");
                faulted_batch_mapping = batch_mapping;
            }

            if (batch->IsSimple())
                continue;

            auto cmd_buf = static_cast<CommandBuffer*>(batch);

            for (auto mapping : cmd_buf->exec_resource_mappings()) {
                fmt = "    Mapping %p, aspace %p, buffer 0x%lx, gpu addr range [0x%lx, 0x%lx), "
                      "mapping length 0x%lx\n";
                gpu_addr_t mapping_start = mapping->gpu_addr();
                gpu_addr_t mapping_end = mapping->gpu_addr() + mapping->length();
                size = std::snprintf(nullptr, 0, fmt, mapping.get(),
                                     mapping->address_space().lock().get(),
                                     mapping->buffer()->platform_buffer()->id(), mapping_start,
                                     mapping_end, mapping->length());
                std::vector<char> buf(size + 1);
                std::snprintf(&buf[0], buf.size(), fmt, mapping.get(),
                              mapping->address_space().lock().get(),
                              mapping->buffer()->platform_buffer()->id(), mapping_start,
                              mapping_end, mapping->length());
                dump_out.append(&buf[0]);
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
        fmt = "Fault address appears to be within mapping %p addr [0x%lx, 0x%lx)\n";
        size = std::snprintf(nullptr, 0, fmt, fault_mapping.get(), fault_mapping->gpu_addr(),
                             fault_mapping->gpu_addr() + fault_mapping->length());
        std::vector<char> buf(size + 1);
        std::snprintf(&buf[0], buf.size(), fmt, fault_mapping.get(), fault_mapping->gpu_addr(),
                      fault_mapping->gpu_addr() + fault_mapping->length());
        dump_out.append(&buf[0]);
    } else {
        dump_out.append("Fault address does not appear to be mapped for any outstanding batch\n");
        if (closest_mapping_distance < UINT64_MAX) {
            fmt = "Fault address is 0x%lx past the end of mapping %p addr [0x%08lx, 0x%08lx), size "
                  "0x%lx, buffer size 0x%lx\n";
            size = std::snprintf(nullptr, 0, fmt, closest_mapping_distance, closest_mapping.get(),
                                 closest_mapping->gpu_addr(),
                                 closest_mapping->gpu_addr() + closest_mapping->length(),
                                 closest_mapping->length(),
                                 closest_mapping->buffer()->platform_buffer()->size());
            std::vector<char> buf(size + 1);
            std::snprintf(&buf[0], buf.size(), fmt, closest_mapping_distance, closest_mapping.get(),
                          closest_mapping->gpu_addr(),
                          closest_mapping->gpu_addr() + closest_mapping->length(),
                          closest_mapping->length(),
                          closest_mapping->buffer()->platform_buffer()->size());
            dump_out.append(&buf[0]);
        }
    }

    if (faulted_batch_mapping) {
        dump_out.append("Batch instructions immediately surrounding the active head:\n");
        uint32_t* batch_data = nullptr;
        // dont early out because we always want to print the "dump end" line
        if (faulted_batch_mapping->buffer()->platform_buffer()->MapCpu(
                reinterpret_cast<void**>(&batch_data))) {
            uint64_t active_head_offset =
                dump_state.render_cs.active_head_pointer - faulted_batch_mapping->gpu_addr();
            DASSERT(active_head_offset <= faulted_batch_mapping->length());
            DASSERT(active_head_offset % sizeof(uint32_t) == 0);
            uint64_t total_dwords = faulted_batch_mapping->length() / sizeof(uint32_t);
            uint64_t active_head_dword = active_head_offset / sizeof(uint32_t);
            // number of dwords printed before and after the active head
            constexpr uint64_t pad_dwords = 32;
            int64_t start_dword = active_head_dword - pad_dwords;
            if (start_dword < 0)
                start_dword = 0;
            uint64_t end_dword = active_head_dword + pad_dwords + 1;
            if (end_dword >= total_dwords)
                end_dword = total_dwords - 1;

            int newline_modulo = 0;
            for (uint64_t i = start_dword; i < end_dword; i++) {

                const char* prefix = "";
                const char* suffix = "";
                const uint32_t dwords_per_line = 4;
                if (i == active_head_dword) {
                    prefix = "\n\n";
                    suffix = " <-- ACTIVE HEAD\n";
                    newline_modulo = -1;
                } else if (newline_modulo % dwords_per_line == 0) {
                    prefix = "\n";
                    suffix = ", ";
                } else {
                    suffix = ", ";
                }
                newline_modulo++;

                fmt = "%s0x%08lx%s";
                size = std::snprintf(nullptr, 0, fmt, prefix, batch_data[i], suffix);
                std::vector<char> buf(size + 1);
                std::snprintf(&buf[0], buf.size(), fmt, prefix, batch_data[i], suffix);
                dump_out.append(&buf[0]);
            }
            dump_out.append("\n\n");
        } else {
            dump_out.append("Failed to map batch data\n");
        }
    }

    fmt = "mapping cache footprint %.1f MB cap %.1f MB\n";
    double footprint = (double)mapping_cache()->memory_footprint() / 1024 / 1024;
    double cap = (double)mapping_cache()->memory_cap() / 1024 / 1024;
    size = std::snprintf(nullptr, 0, fmt, footprint, cap);
    buf = std::vector<char>(size + 1);
    std::snprintf(buf.data(), buf.size(), fmt, footprint, cap);
    dump_out.append(buf.data());

    dump_out.append("---- device dump end ----");
}
