// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_connection.h"

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "msd_intel_context.h"
#include "ppgtt.h"

void msd_connection_close(msd_connection_t* connection) {
  delete MsdIntelAbiConnection::cast(connection);
}

msd_context_t* msd_connection_create_context(msd_connection_t* abi_connection) {
  auto connection = MsdIntelAbiConnection::cast(abi_connection)->ptr();

  return new MsdIntelAbiContext(MsdIntelConnection::CreateContext(connection));
}

// static
std::shared_ptr<MsdIntelContext> MsdIntelConnection::CreateContext(
    std::shared_ptr<MsdIntelConnection> connection) {
  auto context = std::make_shared<MsdIntelContext>(connection->per_process_gtt(), connection);

  connection->context_list_.push_front(context);

  return context;
}

void MsdIntelConnection::DestroyContext(std::shared_ptr<MsdIntelContext> context) {
  context->Shutdown();

  auto iter = std::find(context_list_.begin(), context_list_.end(), context);
  DASSERT(iter != context_list_.end());
  context_list_.erase(iter);

  return owner_->DestroyContext(std::move(context));
}

void msd_connection_set_notification_callback(struct msd_connection_t* connection,
                                              msd_connection_notification_callback_t callback,
                                              void* token) {
  MsdIntelAbiConnection::cast(connection)->ptr()->SetNotificationCallback(callback, token);
}

magma_status_t msd_connection_map_buffer_gpu(msd_connection_t* abi_connection,
                                             msd_buffer_t* abi_buffer, uint64_t gpu_addr,
                                             uint64_t page_offset, uint64_t page_count,
                                             uint64_t flags) {
  auto connection = MsdIntelAbiConnection::cast(abi_connection)->ptr();
  auto buffer = MsdIntelAbiBuffer::cast(abi_buffer)->ptr();
  magma::Status status = connection->MapBufferGpu(buffer, gpu_addr, page_offset, page_count);
  return status.get();
}

magma::Status MsdIntelConnection::MapBufferGpu(std::shared_ptr<MsdIntelBuffer> buffer,
                                               uint64_t gpu_addr, uint64_t page_offset,
                                               uint64_t page_count) {
  std::shared_ptr<GpuMapping> mapping = per_process_gtt()->FindGpuMapping(gpu_addr);

  if (mapping && mapping->BufferId() != buffer->platform_buffer()->id()) {
    // Since we don't implement unmap, its possible for the client driver
    // to reuse an address before releasing the buffer.
    // If the mapping is not currently in use (use_count 2, because we're holding one ref here),
    // we can release it.
    if (mapping.use_count() > 2) {
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Mapping in use, buffer mismatch %lu != %lu",
                      mapping->BufferId(), buffer->platform_buffer()->id());
    }
    auto platform_buffer = mapping->buffer()->platform_buffer();
    DLOG("Reusing gpu_addr 0x%lx by releasing buffer %lu", gpu_addr, platform_buffer->id());
    mapping = nullptr;
    ReleaseBuffer(platform_buffer);
  }

  if (mapping) {
    if (mapping->offset() != page_offset * magma::page_size())
      return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Existing mapping offset mismatch %lu != %lu",
                      page_offset * magma::page_size(), mapping->offset());

    if (mapping->length() >= page_count * magma::page_size())
      return MAGMA_STATUS_OK;

    magma::Status status = per_process_gtt()->GrowMapping(
        mapping.get(), page_count - mapping->length() / magma::page_size());
    if (!status.ok())
      return DRET_MSG(status.get(), "GrowMapping failed");

    return MAGMA_STATUS_OK;
  }

  magma::Status status = AddressSpace::MapBufferGpu(per_process_gtt(), buffer, gpu_addr,
                                                    page_offset, page_count, &mapping);
  if (!status.ok())
    return DRET_MSG(status.get(), "MapBufferGpu failed");

  if (!per_process_gtt()->AddMapping(mapping))
    return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "failed to add mapping");

  DLOG("MapBufferGpu %lu addr 0x%lx", mapping->BufferId(), gpu_addr);

  return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_unmap_buffer_gpu(msd_connection_t* abi_connection,
                                               msd_buffer_t* abi_buffer, uint64_t gpu_va) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

void msd_connection_release_buffer(msd_connection_t* connection, msd_buffer_t* buffer) {
  MsdIntelAbiConnection::cast(connection)
      ->ptr()
      ->ReleaseBuffer(MsdIntelAbiBuffer::cast(buffer)->ptr()->platform_buffer());
}

void MsdIntelConnection::ReleaseBuffer(magma::PlatformBuffer* buffer) {
  ReleaseBuffer(buffer, [](magma::PlatformEvent* event, uint32_t timeout_ms) {
    return event->Wait(timeout_ms);
  });
}

void MsdIntelConnection::ReleaseBuffer(
    magma::PlatformBuffer* buffer,
    std::function<magma::Status(magma::PlatformEvent* event, uint32_t timeout_ms)> wait_callback) {
  std::vector<std::shared_ptr<GpuMapping>> mappings;
  per_process_gtt()->ReleaseBuffer(buffer, &mappings);

  DLOG("ReleaseBuffer %lu\n", buffer->id());

  size_t excess_use_count = 0;

  for (const auto& mapping : mappings) {
    size_t use_count = mapping.use_count();

    if (use_count > 1) {
      // It's an error to release a buffer while it has inflight mappings, as that can fault the
      // GPU. However Mesa/Anvil no longer exactly tracks the user buffers that are associated
      // with each command buffer, instead it snapshots all user buffers currently allocated on
      // the device, which can include buffers from other threads.
      // This can happen when apps continually allocate and free device memory. Intel says
      // a) apps should be sub-allocating instead b) their DRM system driver will stall to handle
      // this case, so we do the same.
      DLOG("ReleaseBuffer %lu mapping has use count %zu", mapping->BufferId(), use_count);
    }
    DASSERT(use_count > 0);
    excess_use_count += use_count - 1;
  }

  if (excess_use_count) {
    uint64_t start_ns = magma::get_monotonic_ns();

    // Send pipeline fence batch for each context which may have queued command buffers.
    for (auto& context : context_list_) {
      if (!context->GetTargetCommandStreamer())
        continue;

      auto event = std::shared_ptr<magma::PlatformEvent>(magma::PlatformEvent::Create());

      context->SubmitBatch(std::make_unique<PipelineFenceBatch>(context, event));

      // Wait for the event to signal.  There can be lots of work queued up and it can take an
      // unpredictable amount of time for it to complete because other contexts may be competing
      // for the hardware, so we wait forever (unless there's a stuck command buffer).
      while (true) {
        {
          TRACE_DURATION("magma", "stall on release");
          constexpr uint32_t kStallMaxMs = 1000;
          auto status = wait_callback(event.get(), kStallMaxMs);
          if (status.ok()) {
            // Event signaled
            break;
          }
        }

        uint64_t stall_ns = magma::get_monotonic_ns() - start_ns;

        excess_use_count = 0;
        for (const auto& mapping : mappings) {
          excess_use_count += mapping.use_count() - 1;
        }

        // If queue has size > 0 after the stall, there's probably a stuck command buffer that
        // will prevent the pipeline fence batch from ever completing.
        size_t queue_size = context->GetQueueSize();

        if (queue_size) {
          MAGMA_LOG(WARNING,
                    "ReleaseBuffer %lu excess_use_count %zd after stall (%lu us) context queue "
                    "size %zd - probable stuck command buffer, closing connection",
                    buffer->id(), excess_use_count, stall_ns / 1000, queue_size);
          if (!sent_context_killed())
            SendContextKilled();
          return;
        }

        DMESSAGE("ReleaseBuffer %lu excess_use_count %zd after stall (%lu us)", buffer->id(),
                 excess_use_count, stall_ns / 1000);
      }
    }
  }

  for (const auto& mapping : mappings) {
    size_t use_count = mapping.use_count();

    if (use_count == 1) {
      // Bus mappings are held in the connection and passed through the command stream to
      // ensure the memory isn't released until the tlbs are invalidated, which happens
      // implicitly on every pipeline flush.
      std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>> bus_mappings;
      mapping->Release(&bus_mappings);
      for (uint32_t i = 0; i < bus_mappings.size(); i++) {
        mappings_to_release_.emplace_back(std::move(bus_mappings[i]));
      }
    } else {
      // If there are no contexts, the connection is in the process of shutting down.
      // Otherwise, since all events have signaled, all inflight mappings should be destroyed so
      // there should be no excess use count.
      DASSERT(context_list_.empty());
    }
  }
}

bool MsdIntelConnection::SubmitPendingReleaseMappings(std::shared_ptr<MsdIntelContext> context) {
  if (!mappings_to_release_.empty()) {
    SubmitBatch(std::make_unique<MappingReleaseBatch>(context, std::move(mappings_to_release_)));
    mappings_to_release_.clear();
  }
  return true;
}

std::unique_ptr<MsdIntelConnection> MsdIntelConnection::Create(Owner* owner,
                                                               msd_client_id_t client_id) {
  return std::unique_ptr<MsdIntelConnection>(
      new MsdIntelConnection(owner, PerProcessGtt::Create(owner), client_id));
}
