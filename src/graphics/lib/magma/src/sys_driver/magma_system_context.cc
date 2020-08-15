// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_context.h"

#include <memory>
#include <unordered_set>
#include <vector>

#include "magma_system_connection.h"
#include "magma_system_device.h"
#include "magma_util/macros.h"
#include "platform_trace.h"

magma::Status MagmaSystemContext::ExecuteCommandBufferWithResources(
    std::unique_ptr<magma_system_command_buffer> cmd_buf,
    std::vector<magma_system_exec_resource> resources, std::vector<uint64_t> semaphores) {
  // used to validate that handles are not duplicated
  std::unordered_set<uint32_t> id_set;

  // used to keep resources in scope until msd_context_execute_command_buffer returns
  std::vector<std::shared_ptr<MagmaSystemBuffer>> system_resources;
  system_resources.reserve(cmd_buf->resource_count);

  // the resources to be sent to the MSD driver
  auto msd_resources = std::vector<msd_buffer_t*>();
  msd_resources.reserve(cmd_buf->resource_count);

  // validate batch buffer index
  if (cmd_buf->resource_count > 0 &&
      cmd_buf->batch_buffer_resource_index >= cmd_buf->resource_count)
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                    "ExecuteCommandBuffer: batch buffer resource index invalid");

  // validate exec resources
  for (uint32_t i = 0; i < cmd_buf->resource_count; i++) {
    uint64_t id = resources[i].buffer_id;

    auto buf = owner_->LookupBufferForContext(id);
    if (!buf)
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                      "ExecuteCommandBuffer: exec resource has invalid buffer handle");

    auto iter = id_set.find(id);
    if (iter != id_set.end())
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "ExecuteCommandBuffer: duplicate exec resource");

    id_set.insert(id);
    system_resources.push_back(buf);
    msd_resources.push_back(buf->msd_buf());

    if (i == cmd_buf->batch_buffer_resource_index) {
      // validate batch start
      if (cmd_buf->batch_start_offset >= buf->size())
        return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "invalid batch start offset 0x%lx",
                        cmd_buf->batch_start_offset);
    }
  }

  // used to keep semaphores in scope until msd_context_execute_command_buffer returns
  std::vector<msd_semaphore_t*> msd_wait_semaphores(cmd_buf->wait_semaphore_count);
  std::vector<msd_semaphore_t*> msd_signal_semaphores(cmd_buf->signal_semaphore_count);

  // validate semaphores
  for (uint32_t i = 0; i < cmd_buf->wait_semaphore_count; i++) {
    auto semaphore = owner_->LookupSemaphoreForContext(semaphores[i]);
    if (!semaphore)
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "wait semaphore id not found 0x%" PRIx64,
                      semaphores[i]);
    msd_wait_semaphores[i] = semaphore->msd_semaphore();
  }
  for (uint32_t i = 0; i < cmd_buf->signal_semaphore_count; i++) {
    auto semaphore =
        owner_->LookupSemaphoreForContext(semaphores[cmd_buf->wait_semaphore_count + i]);
    if (!semaphore)
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "signal semaphore id not found 0x%" PRIx64,
                      semaphores[cmd_buf->wait_semaphore_count + i]);
    msd_signal_semaphores[i] = semaphore->msd_semaphore();
  }

  // submit command buffer to driver
  magma_status_t result = msd_context_execute_command_buffer_with_resources(
      msd_ctx(), cmd_buf.get(), resources.data(), msd_resources.data(), msd_wait_semaphores.data(),
      msd_signal_semaphores.data());

  return DRET_MSG(result, "ExecuteCommandBuffer: msd_context_execute_command_buffer failed: %d",
                  result);
}

magma::Status MagmaSystemContext::ExecuteImmediateCommands(uint64_t commands_size, void* commands,
                                                           uint64_t semaphore_count,
                                                           uint64_t* semaphore_ids) {
  TRACE_DURATION("magma", "MagmaSystemContext::ExecuteImmediateCommands");
  std::vector<msd_semaphore_t*> msd_semaphores(semaphore_count);
  for (uint32_t i = 0; i < semaphore_count; i++) {
    auto semaphore = owner_->LookupSemaphoreForContext(semaphore_ids[i]);
    if (!semaphore)
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "semaphore id not found 0x%" PRIx64,
                      semaphore_ids[i]);
    msd_semaphores[i] = semaphore->msd_semaphore();
    TRACE_FLOW_END("gfx", "semaphore", semaphore_ids[i]);
  }
  magma_status_t result = msd_context_execute_immediate_commands(
      msd_ctx(), commands_size, commands, semaphore_count, msd_semaphores.data());

  return DRET_MSG(result,
                  "ExecuteImmediateCommands: msd_context_execute_immediate_commands failed: %d",
                  result);
}
