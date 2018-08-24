// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/fidl/buffer_set.h"

#include "garnet/bin/mediaplayer/graph/formatting.h"

namespace media_player {

zx_status_t BufferVmo::CreateAndMap(uint64_t size, uint32_t map_flags,
                                    zx_rights_t vmo_rights,
                                    const zx::handle& bti_handle) {
  FXL_DCHECK(size != 0);
  FXL_DCHECK(start_ == nullptr);

  zx_status_t status;
  if (bti_handle) {
    status = zx_vmo_create_contiguous(bti_handle.get(), size, 0,
                                      vmo_.reset_and_get_address());
  } else {
    status = zx::vmo::create(size, 0, &vmo_);
  }

  if (status != ZX_OK) {
    return status;
  }

  zx_handle_t vmar_handle = zx::vmar::root_self()->get();
  uintptr_t tmp;
  status = zx_vmar_map_old(vmar_handle, 0, vmo_.get(), 0, size, map_flags, &tmp);
  if (status != ZX_OK) {
    return status;
  }

  start_ = reinterpret_cast<void*>(tmp);
  size_ = size;

  if (vmo_rights != ZX_RIGHT_SAME_RIGHTS) {
    status = vmo_.replace(vmo_rights, &vmo_);
    if (status != ZX_OK) {
      Reset();
      return status;
    }
  }

  return ZX_OK;
}

void BufferVmo::Reset() {
  if (start_ != nullptr) {
    FXL_DCHECK(size_ != 0);
    zx_handle_t vmar_handle = zx::vmar::root_self()->get();
    zx_status_t status =
        zx_vmar_unmap(vmar_handle, reinterpret_cast<uintptr_t>(start_), size_);
    FXL_DCHECK(status == ZX_OK);
  }

  vmo_.reset();
  start_ = nullptr;
  size_ = 0;
}

// static
std::unique_ptr<BufferSet> BufferSet::Create(
    const fuchsia::mediacodec::CodecPortBufferSettings& settings,
    uint64_t buffer_lifetime_ordinal, bool single_vmo,
    const zx::handle& bti_handle) {
  return std::make_unique<BufferSet>(settings, buffer_lifetime_ordinal,
                                     single_vmo, bti_handle);
}

BufferSet::BufferSet(
    const fuchsia::mediacodec::CodecPortBufferSettings& settings,
    uint64_t buffer_lifetime_ordinal, bool single_vmo,
    const zx::handle& bti_handle)
    : settings_(settings),
      owners_by_index_(settings_.packet_count_for_codec +
                       settings_.packet_count_for_client) {
  free_buffer_count_ = buffer_count();
  settings_.buffer_lifetime_ordinal = buffer_lifetime_ordinal;

  if (single_vmo) {
    uint64_t vmo_size = settings_.per_packet_buffer_bytes * buffer_count();
    zx_status_t status = single_buffer_vmo_.CreateAndMap(
        vmo_size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
        ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER |
            ZX_RIGHT_DUPLICATE,
        bti_handle);
    if (status != ZX_OK) {
      FXL_LOG(FATAL) << "Failed to create and map vmo, status " << status;
    }
  } else {
    uint64_t vmo_size = settings_.per_packet_buffer_bytes;
    buffer_vmos_by_index_ = std::make_unique<BufferVmo[]>(buffer_count());

    for (uint32_t buffer_index = 0; buffer_index << buffer_count();
         ++buffer_index) {
      BufferVmo& vmo = buffer_vmo(buffer_index);
      zx_status_t status = vmo.CreateAndMap(
          vmo_size, ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
          ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER |
              ZX_RIGHT_DUPLICATE,
          bti_handle);
      if (status != ZX_OK) {
        FXL_LOG(FATAL) << "Failed to create and map vmo, status " << status;
      }
    }
  }
}

fuchsia::mediacodec::CodecBuffer BufferSet::GetBufferDescriptor(
    uint32_t buffer_index, bool writeable) const {
  FXL_DCHECK(buffer_index < buffer_count());

  fuchsia::mediacodec::CodecBuffer buffer;
  buffer.buffer_lifetime_ordinal = settings_.buffer_lifetime_ordinal;
  buffer.buffer_index = buffer_index;

  fuchsia::mediacodec::CodecBufferDataVmo buffer_data_vmo;
  const BufferVmo& vmo = buffer_vmo(buffer_index);

  zx_status_t status =
      vmo.vmo().duplicate(ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER |
                              (writeable ? ZX_RIGHT_WRITE : 0),
                          &buffer_data_vmo.vmo_handle);
  if (status != ZX_OK) {
    FXL_LOG(FATAL) << "Failed to duplicate vmo, status " << status;
  }

  buffer_data_vmo.vmo_usable_start =
      buffer_index * settings_.per_packet_buffer_bytes;
  buffer_data_vmo.vmo_usable_size = settings_.per_packet_buffer_bytes;

  buffer.data.set_vmo(std::move(buffer_data_vmo));

  return buffer;
}

void* BufferSet::GetBufferData(uint32_t buffer_index) const {
  FXL_DCHECK(buffer_index < buffer_count());
  const BufferVmo& vmo = buffer_vmo(buffer_index);
  return reinterpret_cast<uint8_t*>(vmo.start()) +
         buffer_index * settings_.per_packet_buffer_bytes;
}

uint32_t BufferSet::AllocateBuffer(uint8_t party) {
  FXL_DCHECK(free_buffer_count_ != 0);
  FXL_DCHECK(suggest_next_to_allocate_ < owners_by_index_.size());

  while (owners_by_index_[suggest_next_to_allocate_] != 0) {
    suggest_next_to_allocate_ =
        (suggest_next_to_allocate_ + 1) % owners_by_index_.size();
  }

  uint32_t result = suggest_next_to_allocate_;
  owners_by_index_[result] = party;

  suggest_next_to_allocate_ =
      (suggest_next_to_allocate_ + 1) % owners_by_index_.size();

  --free_buffer_count_;

  return result;
}

void BufferSet::TransferBuffer(uint32_t buffer_index, uint8_t party) {
  if (buffer_index >= owners_by_index_.size()) {
    FXL_LOG(ERROR)
        << "Attempt to transfer buffer index out of range, lifetime ordinal "
        << lifetime_ordinal() << ", index " << buffer_index << ".";
    return;
  }

  if (owners_by_index_[buffer_index] == 0) {
    FXL_LOG(ERROR) << "Attempt to transfer buffer not currently allocated, "
                      "lifetime ordinal "
                   << lifetime_ordinal() << ", index " << buffer_index << ".";
    return;
  }

  if (owners_by_index_[buffer_index] == party) {
    FXL_LOG(ERROR) << "Attempt to transfer buffer to same party (" << party
                   << "), lifetime ordinal " << lifetime_ordinal() << ", index "
                   << buffer_index << ".";
    return;
  }

  owners_by_index_[buffer_index] = party;
}

void BufferSet::FreeBuffer(uint32_t buffer_index) {
  FXL_CHECK(buffer_index < owners_by_index_.size())
      << "Attempt to free buffer index out of range, lifetime ordinal "
      << lifetime_ordinal() << ", index " << buffer_index << ".";

  FXL_CHECK(owners_by_index_[buffer_index] != 0)
      << "Attempt to free buffer not currently allocated, lifetime ordinal "
      << lifetime_ordinal() << ", index " << buffer_index << ".";

  ++free_buffer_count_;

  owners_by_index_[buffer_index] = 0;
}

void BufferSet::AllocateAllFreeBuffers(uint8_t party) {
  for (auto& owner : owners_by_index_) {
    if (owner == 0) {
      owner = party;
    }
  }

  free_buffer_count_ = 0;
}

void BufferSet::FreeAllBuffersOwnedBy(uint8_t party) {
  for (auto& owner : owners_by_index_) {
    if (owner == party) {
      ++free_buffer_count_;
      owner = 0;
    }
  }
}

void BufferSetManager::ApplyConstraints(
    const fuchsia::mediacodec::CodecBufferConstraints& constraints,
    bool single_vmo) {
  uint64_t lifetime_ordinal = 1;

  if (current_set_) {
    lifetime_ordinal = current_set_->lifetime_ordinal() + 2;
    if (current_set_->free_buffer_count() == current_set_->buffer_count()) {
      current_set_.reset();
    } else {
      auto lifetime_ordinal = current_set_->lifetime_ordinal();
      old_sets_by_ordinal_.emplace(lifetime_ordinal, std::move(current_set_));
    }
  }

  if (constraints.is_physically_contiguous_required) {
    if (!constraints.very_temp_kludge_bti_handle) {
      FXL_LOG(ERROR) << "Contiguous VMOs requested, but no bti handle supplied";
    }
  } else {
    if (constraints.very_temp_kludge_bti_handle) {
      FXL_LOG(ERROR)
          << "Contiguous VMOs not requested, but bti handle supplied";
    }
  }

  current_set_ = BufferSet::Create(constraints.default_settings,
                                   lifetime_ordinal, single_vmo,
                                   constraints.very_temp_kludge_bti_handle);
}

bool BufferSetManager::FreeBuffer(uint64_t lifetime_ordinal, uint32_t index) {
  if (current_set_ && lifetime_ordinal == current_set_->lifetime_ordinal()) {
    // Free a buffer from the current set.
    current_set_->FreeBuffer(index);
    return current_set_->free_buffer_count() ==
           current_set_->buffer_count() - 1;
  }

  // Free a buffer from an old set.
  auto iter = old_sets_by_ordinal_.find(lifetime_ordinal);
  if (iter == old_sets_by_ordinal_.end()) {
    FXL_LOG(ERROR)
        << "Tried to free buffer with unrecognized lifetime ordinal: "
        << lifetime_ordinal;
    return false;
  }

  iter->second->FreeBuffer(index);
  if (iter->second->free_buffer_count() == iter->second->buffer_count()) {
    old_sets_by_ordinal_.erase(iter);
  }

  return false;
}

}  // namespace media_player
