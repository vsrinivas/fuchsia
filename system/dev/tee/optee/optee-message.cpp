// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "optee-message.h"

#include <fbl/limits.h>

namespace optee {

OpenSessionMessage OpenSessionMessage::Create(SharedMemoryManager::DriverMemoryPool* pool,
                                              const UuidView& trusted_app,
                                              const UuidView& client_app,
                                              uint32_t client_login,
                                              uint32_t cancel_id,
                                              const fbl::Array<MessageParam>& params) {
    static constexpr size_t kNumFixedOpenSessionParams = 2;
    const size_t num_params = params.size() + kNumFixedOpenSessionParams;
    ZX_DEBUG_ASSERT(num_params <= fbl::numeric_limits<uint32_t>::max());

    // Allocate from pool
    OpenSessionMessage::SharedMemoryPtr memory;
    pool->Allocate(CalculateSize(num_params), &memory);

    OpenSessionMessage message(fbl::move(memory));

    message.header()->command = Command::kOpenSession;
    message.header()->cancel_id = cancel_id;
    message.header()->num_params = static_cast<uint32_t>(num_params);

    auto current_param = message.params().begin();

    // Param 0 is the trusted app UUID
    current_param->attribute = MessageParam::kAttributeTypeMeta |
                               MessageParam::kAttributeTypeValueInput;
    trusted_app.ToUint64Pair(&current_param->payload.value.generic.a,
                             &current_param->payload.value.generic.b);
    current_param++;

    // Param 1 is the client app UUID and login
    current_param->attribute = MessageParam::kAttributeTypeMeta |
                               MessageParam::kAttributeTypeValueInput;
    client_app.ToUint64Pair(&current_param->payload.value.generic.a,
                            &current_param->payload.value.generic.b);
    current_param->payload.value.generic.c = client_login;
    current_param++;

    // Copy input params in
    for (const auto& param : params) {
        *current_param = param;
        current_param++;
    }

    return message;
}

} // namespace optee
