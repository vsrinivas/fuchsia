// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/fidl/buffer_set.h"

#include "src/media/playback/mediaplayer/graph/formatting.h"

namespace media_player {

// static
fbl::RefPtr<BufferSet> BufferSet::Create(const fuchsia::media::StreamBufferSettings& settings,
                                         uint64_t buffer_lifetime_ordinal, bool single_vmo) {
  if (!settings.has_buffer_constraints_version_ordinal()) {
    FX_LOGS(ERROR) << "Settings missing buffer_constraints_version_ordinal.";
    return nullptr;
  }

  if (!settings.has_single_buffer_mode()) {
    FX_LOGS(ERROR) << "Settings missing single_buffer_mode.";
    return nullptr;
  }

  if (!settings.has_packet_count_for_client()) {
    FX_LOGS(ERROR) << "Settings missing packet_count_for_client.";
    return nullptr;
  }

  if (!settings.has_packet_count_for_server()) {
    FX_LOGS(ERROR) << "Settings missing packet_count_for_server.";
    return nullptr;
  }

  if (!settings.has_per_packet_buffer_bytes()) {
    FX_LOGS(ERROR) << "Settings missing per_packet_buffer_bytes.";
    return nullptr;
  }

  return fbl::MakeRefCounted<BufferSet>(settings, buffer_lifetime_ordinal, single_vmo);
}

BufferSet::BufferSet(const fuchsia::media::StreamBufferSettings& settings,
                     uint64_t buffer_lifetime_ordinal, bool single_vmo)
    : lifetime_ordinal_(buffer_lifetime_ordinal),
      single_vmo_(single_vmo),
      // The existence of these values in |settings| was checked in |BufferSet::Create|.
      buffer_constraints_version_ordinal_(settings.buffer_constraints_version_ordinal()),
      single_buffer_mode_(settings.single_buffer_mode()),
      packet_count_for_server_(settings.packet_count_for_server()),
      packet_count_for_client_(settings.packet_count_for_client()),
      buffer_size_(settings.per_packet_buffer_bytes()) {}

BufferSet::~BufferSet() {
  // Release all the |PayloadBuffers| before |buffers_| is deleted.
  ReleaseAllProcessorOwnedBuffers();
}

void BufferSet::SetBufferCount(uint32_t buffer_count) {
  FX_DCHECK(buffer_count > 0);
  std::lock_guard<std::mutex> locker(mutex_);
  buffers_.resize(buffer_count);
  free_buffer_count_ = buffer_count;
}

fuchsia::media::StreamBufferPartialSettings BufferSet::PartialSettings(
    fuchsia::sysmem::BufferCollectionTokenPtr token) const {
  FX_DCHECK(token);
  std::lock_guard<std::mutex> locker(mutex_);
  fuchsia::media::StreamBufferPartialSettings partial_settings;
  partial_settings.set_buffer_lifetime_ordinal(lifetime_ordinal_);
  partial_settings.set_buffer_constraints_version_ordinal(buffer_constraints_version_ordinal_);
  partial_settings.set_single_buffer_mode(single_buffer_mode_);
  partial_settings.set_packet_count_for_server(packet_count_for_server_);
  partial_settings.set_packet_count_for_client(packet_count_for_client_);
  partial_settings.set_sysmem_token(std::move(token));
  return partial_settings;
}

fbl::RefPtr<PayloadBuffer> BufferSet::AllocateBuffer(uint64_t size,
                                                     const PayloadVmos& payload_vmos) {
  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(!buffers_.empty());
  FX_DCHECK(size <= buffer_size_);
  FX_DCHECK(free_buffer_count_ != 0);
  FX_DCHECK(suggest_next_to_allocate_ < buffers_.size());

  std::vector<fbl::RefPtr<PayloadVmo>> vmos = payload_vmos.GetVmos();
  FX_DCHECK(vmos.size() == buffers_.size());

  FX_DCHECK(single_vmo_ ? (vmos.size() == 1) : (vmos.size() == buffers_.size()));

  uint32_t index = suggest_next_to_allocate_;
  while (!buffers_[index].free_) {
    index = (index + 1) % buffers_.size();
    if (index == suggest_next_to_allocate_) {
      FX_LOGS(WARNING) << "AllocateBuffer: ran out of buffers";
      return nullptr;
    }
  }

  FX_DCHECK(buffers_[index].processor_ref_ == nullptr);
  FX_DCHECK(buffers_[index].free_);
  buffers_[index].free_ = false;

  suggest_next_to_allocate_ = (index + 1) % buffers_.size();

  return CreateBuffer(index, vmos);
}

void BufferSet::AddRefBufferForProcessor(uint32_t buffer_index,
                                         fbl::RefPtr<PayloadBuffer> payload_buffer) {
  FX_DCHECK(payload_buffer);
  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(buffer_index < buffers_.size());
  FX_DCHECK(!buffers_[buffer_index].free_);
  FX_DCHECK(!buffers_[buffer_index].processor_ref_);

  buffers_[buffer_index].processor_ref_ = payload_buffer;
}

fbl::RefPtr<PayloadBuffer> BufferSet::TakeBufferFromProcessor(uint32_t buffer_index) {
  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(buffer_index < buffers_.size());
  FX_DCHECK(!buffers_[buffer_index].free_);
  FX_DCHECK(buffers_[buffer_index].processor_ref_);

  auto result = buffers_[buffer_index].processor_ref_;
  buffers_[buffer_index].processor_ref_ = nullptr;

  return result;
}

fbl::RefPtr<PayloadBuffer> BufferSet::GetProcessorOwnedBuffer(uint32_t buffer_index) {
  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(buffer_index < buffers_.size());
  // Buffer must already be owned by the processor.
  FX_DCHECK(!buffers_[buffer_index].free_);
  FX_DCHECK(buffers_[buffer_index].processor_ref_);

  return buffers_[buffer_index].processor_ref_;
}

void BufferSet::AllocateAllBuffersForProcessor(const PayloadVmos& payload_vmos) {
  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(!buffers_.empty());

  std::vector<fbl::RefPtr<PayloadVmo>> vmos = payload_vmos.GetVmos();
  FX_DCHECK(vmos.size() == buffers_.size());

  for (size_t index = 0; index < buffers_.size(); ++index) {
    FX_DCHECK(buffers_[index].free_);
    FX_DCHECK(!buffers_[index].processor_ref_);

    buffers_[index].free_ = false;
    buffers_[index].processor_ref_ = CreateBuffer(index, vmos);
  }

  free_buffer_count_ = 0;
}

void BufferSet::ReleaseAllProcessorOwnedBuffers() {
  std::vector<fbl::RefPtr<PayloadBuffer>> buffers_to_release_;

  {
    std::lock_guard<std::mutex> locker(mutex_);

    for (size_t index = 0; index < buffers_.size(); ++index) {
      if (buffers_[index].processor_ref_) {
        buffers_to_release_.push_back(buffers_[index].processor_ref_);
        buffers_[index].processor_ref_ = nullptr;
      }
    }
  }

  // Buffers get released here (with the lock not taken) when
  // |buffers_to_release_| goes out of scope.
}

bool BufferSet::HasFreeBuffer(fit::closure callback) {
  std::lock_guard<std::mutex> locker(mutex_);
  if (free_buffer_count_ != 0) {
    return true;
  }

  free_buffer_callback_ = std::move(callback);

  return false;
}

void BufferSet::Decommission() {
  // This was probably taken care of by the processor, but let's make sure. Any
  // processor-owned buffers left behind will cause this |BufferSet| to leak.
  ReleaseAllProcessorOwnedBuffers();

  std::lock_guard<std::mutex> locker(mutex_);
  free_buffer_callback_ = nullptr;
}

fbl::RefPtr<PayloadBuffer> BufferSet::CreateBuffer(
    uint32_t buffer_index, const std::vector<fbl::RefPtr<PayloadVmo>>& payload_vmos) {
  FX_DCHECK(single_vmo_ ? (payload_vmos.size() == 1) : (buffer_index < payload_vmos.size()));

  fbl::RefPtr<PayloadVmo> payload_vmo =
      (single_vmo_ ? payload_vmos[0] : payload_vmos[buffer_index]);
  uint64_t offset_in_vmo = single_vmo_ ? buffer_index * buffer_size_ : 0;

  // The recycler used here captures an |fbl::RefPtr| to |this| in case this
  // buffer set is no longer current when the buffer is recycled.
  fbl::RefPtr<PayloadBuffer> payload_buffer = PayloadBuffer::Create(
      buffer_size_, payload_vmo->at_offset(offset_in_vmo), payload_vmo, offset_in_vmo,
      [this, buffer_index, this_ref = fbl::RefPtr(this)](PayloadBuffer* payload_buffer) {
        fit::closure free_buffer_callback;

        {
          std::lock_guard<std::mutex> locker(mutex_);
          FX_DCHECK(buffer_index < buffers_.size());
          FX_DCHECK(!buffers_[buffer_index].free_);
          FX_DCHECK(!buffers_[buffer_index].processor_ref_);

          buffers_[buffer_index].free_ = true;
          ++free_buffer_count_;

          free_buffer_callback = std::move(free_buffer_callback_);
        }

        if (free_buffer_callback) {
          free_buffer_callback();
        }
      });

  payload_buffer->SetId(buffer_index);
  payload_buffer->SetBufferConfig(lifetime_ordinal_);
  --free_buffer_count_;

  return payload_buffer;
}

bool BufferSetManager::ApplyConstraints(const fuchsia::media::StreamBufferConstraints& constraints,
                                        bool prefer_single_vmo) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);

  if (!constraints.has_default_settings()) {
    FX_LOGS(ERROR) << "FIDL buffer constraints do not have default settings.";
    return false;
  }

  uint64_t lifetime_ordinal = 1;

  if (current_set_) {
    lifetime_ordinal = current_set_->lifetime_ordinal() + 2;
    current_set_->Decommission();
  }

  current_set_ = BufferSet::Create(constraints.default_settings(), lifetime_ordinal,
                                   prefer_single_vmo && constraints.single_buffer_mode_allowed());

  if (current_set_ == nullptr) {
    FX_LOGS(ERROR) << "Could not create bufferset from FIDL buffer settings.";
    return false;
  }

  return true;
}

void BufferSetManager::ReleaseBufferForProcessor(uint64_t lifetime_ordinal, uint32_t index) {
  FIT_DCHECK_IS_THREAD_VALID(thread_checker_);

  if (current_set_ && lifetime_ordinal == current_set_->lifetime_ordinal()) {
    // Release the buffer from the current set.
    current_set_->TakeBufferFromProcessor(index);
    return;
  }

  // The buffer is from an old set and has already been released for the
  // processor.
}

}  // namespace media_player
