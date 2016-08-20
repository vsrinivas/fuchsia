// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_context.h"
#include "magma_buffer.h"
#include "magma_connection.h"

// TODO(MA-67) test this function
bool MagmaContext::SubmitCommandBuffer(MagmaBuffer* batch_buffer, uint32_t used_batch_len,
                                       uint32_t flags)
{
    bool ret = magma_system_submit_command_buffer(
        batch_buffer->connection()->sys_connection(),
        batch_buffer->PrepareForExecution()->abi_cmd_buf(), context_id());

    return DRETF(ret, "magma_system_submit_command_buffer failed");
}