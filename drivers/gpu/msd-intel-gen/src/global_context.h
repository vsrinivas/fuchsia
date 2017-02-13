// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLOBAL_CONTEXT_H
#define GLOBAL_CONTEXT_H

#include "hardware_status_page.h"
#include "msd_intel_context.h"

// GlobalContext, provides the global (per engine) hardware status page for all client contexts.
class GlobalContext : public MsdIntelContext, public HardwareStatusPage::Owner {
public:
    GlobalContext() : MsdIntelContext(false) {}

    bool Map(std::shared_ptr<AddressSpace> address_space, EngineCommandStreamerId id) override;
    bool Unmap(AddressSpaceId address_space_id, EngineCommandStreamerId id) override;

    HardwareStatusPage* hardware_status_page(EngineCommandStreamerId id)
    {
        auto iter = status_page_map_.find(id);
        DASSERT(iter != status_page_map_.end());
        return iter->second.status_page_.get();
    }

private:
    // HardwareStatusPage::Owner
    void* hardware_status_page_cpu_addr(EngineCommandStreamerId id) override
    {
        auto iter = status_page_map_.find(id);
        DASSERT(iter != status_page_map_.end());
        DASSERT(iter->second.cpu_addr);
        return iter->second.cpu_addr;
    }
    gpu_addr_t hardware_status_page_gpu_addr(EngineCommandStreamerId id) override
    {
        auto iter = status_page_map_.find(id);
        DASSERT(iter != status_page_map_.end());
        return iter->second.gpu_addr;
    }

    struct PerEngineHardwareStatus {
        gpu_addr_t gpu_addr;
        void* cpu_addr;
        std::unique_ptr<HardwareStatusPage> status_page_;
    };

    std::map<EngineCommandStreamerId, PerEngineHardwareStatus> status_page_map_;
};

#endif // GLOBAL_CONTEXT_H
