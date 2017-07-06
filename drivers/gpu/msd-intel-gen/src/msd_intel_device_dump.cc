// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_device.h"
#include "registers.h"
#include <memory>
#include <string>

class InstructionDecoder {
public:
    enum Id {
        NOOP = 0x0,
        MI_BATCH_BUFFER_END = 0x0500,
        LOAD_REGISTER_IMM = 0x1100,
        _3DSTATE_CLEAR_PARAMS = 0x7804,
        _3DSTATE_DEPTH_BUFFER = 0x7805,
        _3DSTATE_STENCIL_BUFFER = 0x7806,
        _3DSTATE_HIER_DEPTH_BUFFER = 0x7807,
        _3DSTATE_VERTEX_BUFFERS = 0x7808,
        _3DSTATE_VERTEX_ELEMENTS = 0x7809,
        _3DSTATE_MULTISAMPLE = 0x780d,
        _3DSTATE_INDEX_BUFFER = 0x780a,
        _3DSTATE_VF = 0x780c,
        _3DSTATE_SCISSOR_STATE_POINTERS = 0x780f,
        _3DSTATE_VS = 0x7810,
        _3DSTATE_GS = 0x7811,
        _3DSTATE_CLIP = 0x7812,
        _3DSTATE_SF = 0x7813,
        _3DSTATE_WM = 0x7814,
        _3DSTATE_CONSTANT_VS = 0x7815,
        _3DSTATE_CONSTANT_GS = 0x7816,
        _3DSTATE_CONSTANT_PS = 0x7817,
        _3DSTATE_SAMPLE_MASK = 0x7818,
        _3DSTATE_CONSTANT_HS = 0x7819,
        _3DSTATE_CONSTANT_DS = 0x781a,
        _3DSTATE_HS = 0x781b,
        _3DSTATE_TE = 0x781c,
        _3DSTATE_DS = 0x781d,
        _3DSTATE_STREAMOUT = 0x781e,
        _3DSTATE_SBE = 0x781f,
        _3DSTATE_PS = 0x7820,
        _3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP = 0x7821,
        _3DSTATE_VIEWPORT_STATE_POINTERS_CC = 0x7823,
        _3DSTATE_BINDING_TABLE_POINTERS_VS = 0x7826,
        _3DSTATE_BINDING_TABLE_POINTERS_HS = 0x7827,
        _3DSTATE_BINDING_TABLE_POINTERS_DS = 0x7828,
        _3DSTATE_BINDING_TABLE_POINTERS_GS = 0x7829,
        _3DSTATE_BINDING_TABLE_POINTERS_PS = 0x782a,
        _3DSTATE_SAMPLER_STATE_POINTERS_PS = 0x782f,
        _3DSTATE_CC_STATE_POINTERS = 0x780e,
        _3DSTATE_BLEND_STATE_POINTERS = 0x7824,
        _3DSTATE_URB_VS = 0x7830,
        _3DSTATE_URB_HS = 0x7831,
        _3DSTATE_URB_DS = 0x7832,
        _3DSTATE_URB_GS = 0x7833,
        _3DSTATE_VF_INSTANCING = 0x7849,
        _3DSTATE_VF_SGVS = 0x784a,
        _3DSTATE_VF_TOPOLOGY = 0x784b,
        _3DSTATE_PS_BLEND = 0x784d,
        _3DSTATE_WM_DEPTH_STENCIL = 0x784e,
        _3DSTATE_PS_EXTRA = 0x784f,
        _3DSTATE_RASTER = 0x7850,
        _3DSTATE_SBE_SWIZ = 0x7851,
        _3DSTATE_WM_HZ_OP = 0x7852,
        _3DSTATE_PUSH_CONSTANT_ALLOC_VS = 0x7912,
        _3DSTATE_PUSH_CONSTANT_ALLOC_HS = 0x7913,
        _3DSTATE_PUSH_CONSTANT_ALLOC_DS = 0x7914,
        _3DSTATE_PUSH_CONSTANT_ALLOC_GS = 0x7915,
        _3DSTATE_PUSH_CONSTANT_ALLOC_PS = 0x7916,
        PIPE_CONTROL = 0x7a00,
        _3DPRIMITIVE = 0x7b00,
        STATE_BASE_ADDRESS = 0x6101,
        PIPELINE_SELECT = 0x6904,
    };

    static const char* name(Id id)
    {
        switch (id) {
            case _3DSTATE_VERTEX_BUFFERS:
                return "3DSTATE_VERTEX_BUFFERS";
            case _3DSTATE_VERTEX_ELEMENTS:
                return "3DSTATE_VERTEX_ELEMENTS";
            case LOAD_REGISTER_IMM:
                return "LOAD_REGISTER_IMM";
            case PIPE_CONTROL:
                return "PIPE_CONTROL";
            case PIPELINE_SELECT:
                return "PIPELINE_SELECT";
            case STATE_BASE_ADDRESS:
                return "STATE_BASE_ADDRESS";
            case _3DSTATE_VF_SGVS:
                return "3DSTATE_VF_SGVS";
            case _3DSTATE_VF_INSTANCING:
                return "3DSTATE_VF_INSTANCING";
            case _3DSTATE_VF_TOPOLOGY:
                return "3DSTATE_VF_TOPOLOGY";
            case _3DSTATE_URB_VS:
                return "3DSTATE_URB_VS";
            case _3DSTATE_URB_HS:
                return "3DSTATE_URB_HS";
            case _3DSTATE_URB_DS:
                return "3DSTATE_URB_DS";
            case _3DSTATE_URB_GS:
                return "3DSTATE_URB_GS";
            case _3DSTATE_BLEND_STATE_POINTERS:
                return "3DSTATE_BLEND_STATE_POINTERS";
            case _3DSTATE_PS_BLEND:
                return "3DSTATE_PS_BLEND";
            case _3DSTATE_CC_STATE_POINTERS:
                return "3DSTATE_CC_STATE_POINTERS";
            case _3DSTATE_WM_DEPTH_STENCIL:
                return "3DSTATE_WM_DEPTH_STENCIL";
            case _3DSTATE_CONSTANT_VS:
                return "3DSTATE_CONSTANT_VS";
            case _3DSTATE_CONSTANT_HS:
                return "3DSTATE_CONSTANT_HS";
            case _3DSTATE_CONSTANT_DS:
                return "3DSTATE_CONSTANT_DS";
            case _3DSTATE_CONSTANT_GS:
                return "3DSTATE_CONSTANT_GS";
            case _3DSTATE_CONSTANT_PS:
                return "3DSTATE_CONSTANT_PS";
            case _3DSTATE_BINDING_TABLE_POINTERS_VS:
                return "3DSTATE_BINDING_TABLE_POINTERS_VS";
            case _3DSTATE_BINDING_TABLE_POINTERS_HS:
                return "3DSTATE_BINDING_TABLE_POINTERS_HS";
            case _3DSTATE_BINDING_TABLE_POINTERS_DS:
                return "3DSTATE_BINDING_TABLE_POINTERS_DS";
            case _3DSTATE_BINDING_TABLE_POINTERS_GS:
                return "3DSTATE_BINDING_TABLE_POINTERS_GS";
            case _3DSTATE_BINDING_TABLE_POINTERS_PS:
                return "3DSTATE_BINDING_TABLE_POINTERS_PS";
            case _3DSTATE_SAMPLER_STATE_POINTERS_PS:
                return "3DSTATE_SAMPLER_STATE_POINTERS_PS";
            case _3DSTATE_MULTISAMPLE:
                return "3DSTATE_MULTISAMPLE";
            case _3DSTATE_SAMPLE_MASK:
                return "3DSTATE_SAMPLE_MASK";
            case _3DSTATE_VS:
                return "3DSTATE_VS";
            case _3DSTATE_HS:
                return "3DSTATE_HS";
            case _3DSTATE_TE:
                return "3DSTATE_TE";
            case _3DSTATE_DS:
                return "3DSTATE_DS";
            case _3DSTATE_STREAMOUT:
                return "3DSTATE_STREAMOUT";
            case _3DSTATE_GS:
                return "3DSTATE_GS";
            case _3DSTATE_CLIP:
                return "3DSTATE_CLIP";
            case _3DSTATE_SF:
                return "3DSTATE_SF";
            case _3DSTATE_RASTER:
                return "3DSTATE_RASTER";
            case _3DSTATE_SBE:
                return "3DSTATE_SBE";
            case _3DSTATE_WM:
                return "3DSTATE_WM";
            case _3DSTATE_PS:
                return "3DSTATE_PS";
            case _3DSTATE_PS_EXTRA:
                return "3DSTATE_PS_EXTRA";
            case _3DSTATE_VIEWPORT_STATE_POINTERS_CC:
                return "3DSTATE_VIEWPORT_STATE_POINTERS_CC";
            case _3DSTATE_DEPTH_BUFFER:
                return "3DSTATE_DEPTH_BUFFER";
            case _3DSTATE_HIER_DEPTH_BUFFER:
                return "3DSTATE_HIER_DEPTH_BUFFER";
            case _3DSTATE_STENCIL_BUFFER:
                return "3DSTATE_STENCIL_BUFFER";
            case _3DSTATE_CLEAR_PARAMS:
                return "3DSTATE_CLEAR_PARAMS";
            case _3DPRIMITIVE:
                return "3DPRIMITIVE";
            case _3DSTATE_INDEX_BUFFER:
                return "3DSTATE_INDEX_BUFFER";
            case _3DSTATE_SBE_SWIZ:
                return "3DSTATE_SBE_SWIZ";
            case _3DSTATE_PUSH_CONSTANT_ALLOC_VS:
                return "3DSTATE_PUSH_CONSTANT_ALLOC_VS";
            case _3DSTATE_PUSH_CONSTANT_ALLOC_HS:
                return "3DSTATE_PUSH_CONSTANT_ALLOC_HS";
            case _3DSTATE_PUSH_CONSTANT_ALLOC_DS:
                return "3DSTATE_PUSH_CONSTANT_ALLOC_DS";
            case _3DSTATE_PUSH_CONSTANT_ALLOC_GS:
                return "3DSTATE_PUSH_CONSTANT_ALLOC_GS";
            case _3DSTATE_PUSH_CONSTANT_ALLOC_PS:
                return "3DSTATE_PUSH_CONSTANT_ALLOC_PS";
            case _3DSTATE_WM_HZ_OP:
                return "3DSTATE_WM_HZ_OP";
            case _3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP:
                return "3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP";
            case _3DSTATE_SCISSOR_STATE_POINTERS:
                return "3DSTATE_SCISSOR_STATE_POINTERS";
            case _3DSTATE_VF:
                return "3DSTATE_VF";
            case MI_BATCH_BUFFER_END:
                return "MI_BATCH_BUFFER_END";
            case NOOP:
                return "NOOP";
        }
        return "UNKNOWN";
    }

    static bool Decode(uint32_t dword, Id* id_out, uint32_t* dword_count_out)
    {
        if (dword == 0) {
            *id_out = NOOP;
            *dword_count_out = 1;
            return true;
        }

        uint16_t id = dword >> 16;
        switch (id) {
            case PIPELINE_SELECT:
            case MI_BATCH_BUFFER_END:
                *dword_count_out = 1;
                break;
            case LOAD_REGISTER_IMM:
                *dword_count_out = 3;
                break;
            case _3DSTATE_BLEND_STATE_POINTERS:
            case _3DSTATE_CC_STATE_POINTERS:
            case _3DSTATE_VIEWPORT_STATE_POINTERS_CC:
                *dword_count_out = 2;
                break;
            case _3DSTATE_VF:
            case _3DSTATE_SCISSOR_STATE_POINTERS:
            case _3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP:
            case _3DSTATE_PUSH_CONSTANT_ALLOC_PS:
            case _3DSTATE_PUSH_CONSTANT_ALLOC_GS:
            case _3DSTATE_PUSH_CONSTANT_ALLOC_DS:
            case _3DSTATE_PUSH_CONSTANT_ALLOC_HS:
            case _3DSTATE_PUSH_CONSTANT_ALLOC_VS:
            case _3DSTATE_SBE_SWIZ:
            case _3DSTATE_INDEX_BUFFER:
            case _3DPRIMITIVE:
            case _3DSTATE_CLEAR_PARAMS:
            case _3DSTATE_STENCIL_BUFFER:
            case _3DSTATE_HIER_DEPTH_BUFFER:
            case _3DSTATE_DEPTH_BUFFER:
            case _3DSTATE_PS_EXTRA:
            case _3DSTATE_PS:
            case _3DSTATE_WM:
            case _3DSTATE_SBE:
            case _3DSTATE_RASTER:
            case _3DSTATE_SF:
            case _3DSTATE_CLIP:
            case _3DSTATE_GS:
            case _3DSTATE_STREAMOUT:
            case _3DSTATE_DS:
            case _3DSTATE_TE:
            case _3DSTATE_VS:
            case _3DSTATE_HS:
            case _3DSTATE_SAMPLE_MASK:
            case _3DSTATE_MULTISAMPLE:
            case _3DSTATE_SAMPLER_STATE_POINTERS_PS:
            case _3DSTATE_BINDING_TABLE_POINTERS_PS:
            case _3DSTATE_BINDING_TABLE_POINTERS_GS:
            case _3DSTATE_BINDING_TABLE_POINTERS_DS:
            case _3DSTATE_BINDING_TABLE_POINTERS_HS:
            case _3DSTATE_BINDING_TABLE_POINTERS_VS:
            case _3DSTATE_CONSTANT_PS:
            case _3DSTATE_CONSTANT_GS:
            case _3DSTATE_CONSTANT_DS:
            case _3DSTATE_CONSTANT_HS:
            case _3DSTATE_CONSTANT_VS:
            case _3DSTATE_WM_DEPTH_STENCIL:
            case _3DSTATE_PS_BLEND:
            case _3DSTATE_URB_GS:
            case _3DSTATE_URB_DS:
            case _3DSTATE_URB_HS:
            case _3DSTATE_URB_VS:
            case _3DSTATE_VF_TOPOLOGY:
            case _3DSTATE_VF_INSTANCING:
            case _3DSTATE_VF_SGVS:
            case _3DSTATE_VERTEX_BUFFERS:
            case _3DSTATE_VERTEX_ELEMENTS:
            case _3DSTATE_WM_HZ_OP:
            case PIPE_CONTROL:
            case STATE_BASE_ADDRESS:
                *dword_count_out = (dword & 0xFF) + 2;
                break;
        }
        *id_out = static_cast<Id>(id);
        return true;
    }
};

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
                      "offset 0x%lx, mapping length 0x%lx\n";
                gpu_addr_t mapping_start = mapping->gpu_addr();
                gpu_addr_t mapping_end = mapping->gpu_addr() + mapping->length();
                size = std::snprintf(nullptr, 0, fmt, mapping.get(),
                                     mapping->address_space().lock().get(),
                                     mapping->buffer()->platform_buffer()->id(), mapping_start,
                                     mapping_end, mapping->offset(), mapping->length());
                std::vector<char> buf(size + 1);
                std::snprintf(&buf[0], buf.size(), fmt, mapping.get(),
                              mapping->address_space().lock().get(),
                              mapping->buffer()->platform_buffer()->id(), mapping_start,
                              mapping_end, mapping->offset(), mapping->length());
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
            uint64_t start_dword = faulted_batch_mapping->offset();
            uint64_t end_dword = total_dwords - 1;

            uint32_t dwords_remaining = 0;
            bool end_of_batch = false;
            for (uint64_t i = start_dword; i < end_dword; i++) {

                if (dwords_remaining == 0) {
                    InstructionDecoder::Id id;
                    bool decoded =
                        InstructionDecoder::Decode(batch_data[i], &id, &dwords_remaining);
                    if (decoded) {
                        fmt = "\n%s: ";
                        size = std::snprintf(nullptr, 0, fmt, InstructionDecoder::name(id));
                        buf = std::vector<char>(size + 1);
                        std::snprintf(&buf[0], buf.size(), fmt, InstructionDecoder::name(id));
                        dump_out.append(&buf[0]);
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
                dump_out.append(&buf[0]);

                if (end_of_batch)
                    break;
            }
            dump_out.append("\n\n");
        } else {
            dump_out.append("Failed to map batch data\n");
        }
    }

#if MSD_INTEL_ENABLE_MAPPING_CACHE
    dump_out.append("mapping cache: ENABLED\n");
#else
    dump_out.append("mapping cache: DISABLED\n");
#endif

    dump_out.append("---- device dump end ----");
}
