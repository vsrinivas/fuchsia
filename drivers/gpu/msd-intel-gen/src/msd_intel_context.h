// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTEXT_H
#define CONTEXT_H

#include "msd.h"
#include "msd_intel_buffer.h"
#include "types.h"
#include <map>
#include <memory>

class MsdIntelContext : public msd_context {
public:
    MsdIntelContext();

    void InitEngine(EngineCommandStreamerId id, std::unique_ptr<MsdIntelBuffer> context_buffer);

private:
    MsdIntelBuffer* get_buffer(EngineCommandStreamerId id)
    {
        auto iter = context_buffer_map_.find(id);
        return iter == context_buffer_map_.end() ? nullptr : iter->second.get();
    }

    std::map<EngineCommandStreamerId, std::unique_ptr<MsdIntelBuffer>> context_buffer_map_;

    friend class TestContext;

    static const uint32_t kMagic = 0x63747874; // "ctxt"
};

#endif // CONTEXT_H
