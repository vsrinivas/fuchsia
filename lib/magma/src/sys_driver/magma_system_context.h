// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _MAGMA_SYSTEM_CONTEXT_H_
#define _MAGMA_SYSTEM_CONTEXT_H_

#include <functional>
#include <memory>

#include "msd.h"

struct MagmaSystemConnection;

using msd_context_unique_ptr_t = std::unique_ptr<msd_context, std::function<void(msd_context*)>>;

class MagmaSystemContext {
public:
    static std::unique_ptr<MagmaSystemContext> Create(MagmaSystemConnection* connection);

private:
    MagmaSystemContext(msd_context_unique_ptr_t msd_ctx) : msd_ctx_(std::move(msd_ctx)) {}

    msd_context_unique_ptr_t msd_ctx_;
};

#endif // _MAGMA_SYSTEM_CONTEXT_H_