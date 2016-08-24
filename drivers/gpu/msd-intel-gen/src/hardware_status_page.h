// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_STATUS_PAGE_H
#define HARDWARE_STATUS_PAGE_H

#include "magma_util/macros.h"
#include "msd_intel_buffer.h"

class HardwareStatusPage {
public:
    class Owner {
    public:
        virtual void* hardware_status_page_cpu_addr(EngineCommandStreamerId id) = 0;
        virtual gpu_addr_t hardware_status_page_gpu_addr(EngineCommandStreamerId id) = 0;
    };

    HardwareStatusPage(Owner* owner, EngineCommandStreamerId id)
        : owner_(owner), engine_command_streamer_id_(id)
    {
    }

    gpu_addr_t gpu_addr()
    {
        return owner_->hardware_status_page_gpu_addr(engine_command_streamer_id_);
    }

    void write_sequence_number(uint32_t val)
    {
        write_general_purpose_offset(val, kSequenceNumberOffset);
    }

    uint32_t read_sequence_number() { return read_general_purpose_offset(kSequenceNumberOffset); }

    // from Intel-GFX-BSpec-SuperNDA-BDW-20140919-b70387-r74244-Web
    // Render Logical Context Data - The Per-Process Hardware Status Page
    static constexpr uint32_t kSequenceNumberOffset = 0x20;

private:
    void write_general_purpose_offset(uint32_t val, uint32_t offset)
    {
        DASSERT((offset & 0x3) == 0);
        DASSERT(offset >= 0x20 && offset <= 0x3FC);
        void* cpu_addr = owner_->hardware_status_page_cpu_addr(engine_command_streamer_id_);
        reinterpret_cast<uint32_t*>(cpu_addr)[offset >> 2] = val;
    }

    uint32_t read_general_purpose_offset(uint32_t offset)
    {
        DASSERT((offset & 0x3) == 0);
        DASSERT(offset >= 0x20 && offset <= 0x3FC);
        void* cpu_addr = owner_->hardware_status_page_cpu_addr(engine_command_streamer_id_);
        return reinterpret_cast<uint32_t*>(cpu_addr)[offset >> 2];
    }

    Owner* owner_;
    EngineCommandStreamerId engine_command_streamer_id_;
};

#endif // HARDWARE_STATUS_PAGE_H
