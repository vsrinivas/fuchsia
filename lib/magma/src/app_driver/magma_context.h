// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_CONTEXT_H_
#define _MAGMA_CONTEXT_H_

#include "magma.h"
#include "magma_system.h"

class MagmaContext : public magma_context {
public:
    MagmaContext(MagmaConnection* connection, uint32_t context_id)
        : connection_(connection), context_id_(context_id)
    {
        magic_ = kMagic;
    }

    uint32_t context_id() { return context_id_; }

    MagmaConnection* connection() { return connection_; }

    static MagmaContext* cast(magma_context* context)
    {
        DASSERT(context);
        DASSERT(context->magic_ == kMagic);
        return static_cast<MagmaContext*>(context);
    }

private:
    MagmaConnection* connection_;
    uint32_t context_id_;

    static const uint32_t kMagic = 0x63747874; // "ctxt" (Context)
};

#endif // _MAGMA_CONTEXT_H_