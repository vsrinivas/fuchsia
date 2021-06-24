// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_vsi_connection.h"

#include "address_space_layout.h"
#include "msd_vsi_buffer.h"
#include "msd_vsi_context.h"

void msd_connection_close(msd_connection_t* connection) {
  delete MsdVsiAbiConnection::cast(connection);
}

msd_context_t* msd_connection_create_context(msd_connection_t* abi_connection) {
  auto connection = MsdVsiAbiConnection::cast(abi_connection)->ptr();

  auto context =
      MsdVsiContext::Create(connection, connection->address_space(), connection->GetRingbuffer());
  if (!context) {
    return DRETP(nullptr, "failed to create new context");
  }
  return new MsdVsiAbiContext(context);
}

magma_status_t msd_connection_map_buffer_gpu(msd_connection_t* abi_connection,
                                             msd_buffer_t* abi_buffer, uint64_t gpu_va,
                                             uint64_t page_offset, uint64_t page_count,
                                             uint64_t flags) {
  auto connection = MsdVsiAbiConnection::cast(abi_connection)->ptr();
  auto buffer = MsdVsiAbiBuffer::cast(abi_buffer)->ptr();
  magma::Status status = connection->MapBufferGpu(buffer, gpu_va, page_offset, page_count);
  return status.get();
}

magma::Status MsdVsiConnection::MapBufferGpu(std::shared_ptr<MsdVsiBuffer> buffer, uint64_t gpu_va,
                                             uint64_t page_offset, uint64_t page_count) {
  uint64_t end_gpu_va = gpu_va + (page_count * magma::page_size());
  if (!AddressSpaceLayout::IsValidClientGpuRange(gpu_va, end_gpu_va)) {
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS,
                    "failed to map buffer to [0x%lx, 0x%lx), lies outside client region", gpu_va,
                    end_gpu_va);
  }
  std::shared_ptr<GpuMapping> mapping;
  magma::Status status = AddressSpace::MapBufferGpu(address_space(), buffer, gpu_va, page_offset,
                                                    page_count, &mapping);
  if (!status.ok()) {
    return DRET_MSG(status.get(), "MapBufferGpu failed");
  }
  address_space_dirty_ = true;

  if (!address_space()->AddMapping(mapping)) {
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to add mapping");
  }
  return MAGMA_STATUS_OK;
}

magma_status_t msd_connection_unmap_buffer_gpu(msd_connection_t* abi_connection,
                                               msd_buffer_t* abi_buffer, uint64_t gpu_va) {
  if (!MsdVsiAbiConnection::cast(abi_connection)
           ->ptr()
           ->ReleaseMapping(MsdVsiAbiBuffer::cast(abi_buffer)->ptr()->platform_buffer(), gpu_va)) {
    return DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "failed to remove mapping");
  }
  return MAGMA_STATUS_OK;
}

void msd_connection_release_buffer(msd_connection_t* abi_connection, msd_buffer_t* abi_buffer) {
  MsdVsiAbiConnection::cast(abi_connection)
      ->ptr()
      ->ReleaseBuffer(MsdVsiAbiBuffer::cast(abi_buffer)->ptr()->platform_buffer());
}

magma_status_t msd_connection_commit_buffer(msd_connection_t* abi_connection,
                                            msd_buffer_t* abi_buffer, uint64_t page_offset,
                                            uint64_t page_count) {
  return MAGMA_STATUS_UNIMPLEMENTED;
}

void msd_connection_set_notification_callback(struct msd_connection_t* connection,
                                              msd_connection_notification_callback_t callback,
                                              void* token) {
  MsdVsiAbiConnection::cast(connection)->ptr()->SetNotificationCallback(callback, token);
}

void MsdVsiConnection::QueueReleasedMappings(std::vector<std::shared_ptr<GpuMapping>> mappings) {
  bool killed = false;
  for (const auto& mapping : mappings) {
    size_t use_count = mapping.use_count();
    if (use_count == 1) {
      // Bus mappings are held in the connection and passed through the command stream to
      // ensure the memory isn't released until the tlbs are invalidated, which happens
      // when the MappingReleaseBatch completes.
      std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>> bus_mappings;
      mapping->Release(&bus_mappings);
      for (uint32_t i = 0; i < bus_mappings.size(); i++) {
        mappings_to_release_.emplace_back(std::move(bus_mappings[i]));
      }
    } else {
      // It's an error to release a buffer while it has inflight mappings, as that
      // can fault the gpu.
      DMESSAGE("buffer %lu mapping use_count %zd", mapping->BufferId(), use_count);
      if (!killed) {
        SendContextKilled();
        killed = true;
      }
    }
  }
}

bool MsdVsiConnection::ReleaseMapping(magma::PlatformBuffer* buffer, uint64_t gpu_va) {
  std::shared_ptr<GpuMapping> mapping;
  if (!address_space()->ReleaseMapping(buffer, gpu_va, &mapping)) {
    return DRETF(false, "failed to remove mapping");
  }
  std::vector<std::shared_ptr<GpuMapping>> mappings = {std::move(mapping)};
  QueueReleasedMappings(std::move(mappings));
  address_space_dirty_ = true;

  return true;
}

void MsdVsiConnection::ReleaseBuffer(magma::PlatformBuffer* buffer) {
  std::vector<std::shared_ptr<GpuMapping>> mappings;
  address_space()->ReleaseBuffer(buffer, &mappings);
  QueueReleasedMappings(std::move(mappings));
}

bool MsdVsiConnection::SubmitPendingReleaseMappings(std::shared_ptr<MsdVsiContext> context) {
  if (!mappings_to_release_.empty()) {
    magma::Status status =
        SubmitBatch(std::make_unique<MappingReleaseBatch>(context, std::move(mappings_to_release_)),
                    true /* do_flush */);
    mappings_to_release_.clear();
    if (!status.ok()) {
      return DRETF(false, "Failed to submit mapping release batch: %d", status.get());
    }
  }
  return true;
}
