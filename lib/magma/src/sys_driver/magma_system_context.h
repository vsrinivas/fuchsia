// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_CONTEXT_H_
#define _MAGMA_SYSTEM_CONTEXT_H_

#include <functional>
#include <memory>

#include "msd.h"

class MagmaSystemConnection;

using msd_context_unique_ptr_t = std::unique_ptr<msd_context, decltype(&msd_context_destroy)>;

static inline msd_context_unique_ptr_t MsdContextUniquePtr(msd_context* context)
{
    return msd_context_unique_ptr_t(context, &msd_context_destroy);
}

class MagmaSystemContext {
public:
    static std::unique_ptr<MagmaSystemContext> Create(MagmaSystemConnection* connection);

private:
    MagmaSystemContext(msd_context_unique_ptr_t msd_ctx) : msd_ctx_(std::move(msd_ctx)) {}

    msd_context_unique_ptr_t msd_ctx_;
};

#endif // _MAGMA_SYSTEM_CONTEXT_H_