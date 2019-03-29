// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/graph/payloads/payload_manager.h"

#include "src/lib/fxl/logging.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"

namespace media_player {
namespace {

static uint64_t AlignUp(uint64_t size, uint64_t alignment) {
  return size = (size + alignment - 1) & ~(alignment - 1);
}

}  // namespace

// TODO(dalesat): Handle insufficient 'provided' vmos.
// TODO(dalesat): Make outputs declare whether they will use
// AllocatePayloadBufferForOutput or chop up VMOs their own way. That latter is
// incompatible with an input that provides an allocate callback.
// TODO(dalesat): Ensure we have the signalling we need for dynamic config
// changes.

void PayloadManager::Dump(std::ostream& os) const {
  std::lock_guard<std::mutex> locker(mutex_);
  DumpInternal(os);
}

void PayloadManager::DumpInternal(std::ostream& os) const {
  if (!ready_locked()) {
    os << "not ready";
    return;
  }

  os << fostr::Indent;
  if (output_.local_memory_allocator_) {
    os << fostr::NewLine << "output local memory allocator: ";
    output_.local_memory_allocator_->Dump(os);
  } else if (output_.vmo_allocator_) {
    os << fostr::NewLine << "output vmo allocator: ";
    output_.vmo_allocator_->Dump(os);
  }

  if (input_.local_memory_allocator_) {
    os << fostr::NewLine << "input local memory allocator: ";
    input_.local_memory_allocator_->Dump(os);
  } else if (input_.vmo_allocator_) {
    os << fostr::NewLine << "input vmo allocator: ";
    input_.vmo_allocator_->Dump(os);
  }

  os << fostr::Outdent;
}

void PayloadManager::ApplyOutputConfiguration(const PayloadConfig& config,
                                              zx::handle bti_handle) {
  FXL_DCHECK(config.mode_ != PayloadMode::kNotConfigured);
  FXL_DCHECK(!config.physically_contiguous_ ||
             (config.mode_ == PayloadMode::kUsesVmos));
  FXL_DCHECK(config.physically_contiguous_ == bti_handle.is_valid());
  std::lock_guard<std::mutex> locker(mutex_);

  if (output_.config_.mode_ == PayloadMode::kProvidesVmos &&
      config.mode_ != PayloadMode::kProvidesVmos) {
    // The output was supplying VMOs but will no longer be doing so. Remove any
    // VMOs it left behind.
    output_external_vmos().RemoveAllVmos();
  }

  output_.config_ = config;
  output_.bti_handle_ = std::move(bti_handle);

  if (input_.config_.mode_ != PayloadMode::kNotConfigured) {
    UpdateAllocators();
  }
}

void PayloadManager::ApplyInputConfiguration(
    const PayloadConfig& config, zx::handle bti_handle,
    AllocateCallback allocate_callback) {
  FXL_DCHECK(config.mode_ != PayloadMode::kNotConfigured);
  FXL_DCHECK(config.mode_ != PayloadMode::kProvidesLocalMemory);
  FXL_DCHECK(!config.physically_contiguous_ ||
             (config.mode_ == PayloadMode::kUsesVmos));
  FXL_DCHECK(config.physically_contiguous_ == bti_handle.is_valid());
  FXL_DCHECK(allocate_callback == nullptr ||
             config.mode_ == PayloadMode::kUsesVmos ||
             config.mode_ == PayloadMode::kProvidesVmos);
  std::lock_guard<std::mutex> locker(mutex_);

  if (input_.config_.mode_ == PayloadMode::kProvidesVmos &&
      config.mode_ != PayloadMode::kProvidesVmos) {
    // The input was supplying VMOs but will no longer be doing so. Remove any
    // VMOs it left behind.
    input_external_vmos().RemoveAllVmos();
  }

  input_.config_ = config;
  input_.bti_handle_ = std::move(bti_handle);
  allocate_callback_ = std::move(allocate_callback);

  if (output_.config_.mode_ != PayloadMode::kNotConfigured) {
    UpdateAllocators();
  }
}

bool PayloadManager::ready() const {
  std::lock_guard<std::mutex> locker(mutex_);
  return ready_locked();
}

fbl::RefPtr<PayloadBuffer> PayloadManager::AllocatePayloadBufferForOutput(
    uint64_t size) const {
  std::lock_guard<std::mutex> locker(mutex_);
  FXL_DCHECK(ready_locked());
  FXL_DCHECK(output_.config_.mode_ != PayloadMode::kProvidesLocalMemory);

  PayloadAllocator* allocator = output_.payload_allocator();

  if (allocate_callback_ && allocator == nullptr) {
    // The input side has provided a callback to do the actual allocation.
    // We know this applies to allocation for output rather than for copies,
    // because there is no allocator associated with the output (|allocator| is
    // null).
    return AllocateUsingAllocateCallback(size);
  }

  // If there is no allocator associated with the output, the output is sharing
  // the allocator associated with the input.
  if (allocator == nullptr) {
    allocator = input_.payload_allocator();
  }

  FXL_DCHECK(allocator);

  return allocator->AllocatePayloadBuffer(size);
}

PayloadVmos& PayloadManager::input_vmos() const {
  std::lock_guard<std::mutex> locker(mutex_);
  FXL_DCHECK(ready_locked());
  FXL_DCHECK(input_.config_.mode_ == PayloadMode::kUsesVmos ||
             input_.config_.mode_ == PayloadMode::kProvidesVmos);

  PayloadVmos* result = input_.vmo_allocator_ ? input_.vmo_allocator_.get()
                                              : output_.vmo_allocator_.get();
  FXL_DCHECK(result);

  return *result;
}

PayloadVmoProvision& PayloadManager::input_external_vmos() const {
  std::lock_guard<std::mutex> locker(mutex_);
  FXL_DCHECK(ready_locked());
  FXL_DCHECK(input_.config_.mode_ == PayloadMode::kProvidesVmos);

  PayloadVmoProvision* result = input_.vmo_allocator_
                                    ? input_.vmo_allocator_.get()
                                    : output_.vmo_allocator_.get();

  FXL_DCHECK(result);

  return *result;
}

PayloadVmos& PayloadManager::output_vmos() const {
  std::lock_guard<std::mutex> locker(mutex_);
  FXL_DCHECK(ready_locked());
  FXL_DCHECK(output_.config_.mode_ == PayloadMode::kUsesVmos ||
             output_.config_.mode_ == PayloadMode::kProvidesVmos);

  PayloadVmos* result = output_.vmo_allocator_ ? output_.vmo_allocator_.get()
                                               : input_.vmo_allocator_.get();
  FXL_DCHECK(result);

  return *result;
}

PayloadVmoProvision& PayloadManager::output_external_vmos() const {
  std::lock_guard<std::mutex> locker(mutex_);
  FXL_DCHECK(ready_locked());
  FXL_DCHECK(output_.config_.mode_ == PayloadMode::kProvidesVmos);

  PayloadVmoProvision* result = output_.vmo_allocator_
                                    ? output_.vmo_allocator_.get()
                                    : input_.vmo_allocator_.get();
  FXL_DCHECK(result);

  return *result;
}

bool PayloadManager::MaybeAllocatePayloadBufferForCopy(
    uint64_t size, fbl::RefPtr<PayloadBuffer>* payload_buffer_out) const {
  FXL_DCHECK(payload_buffer_out || (size == 0));

  std::lock_guard<std::mutex> locker(mutex_);
  FXL_DCHECK(ready_locked());

  if (!copy_) {
    // Don't need to copy.
    return false;
  }

  FXL_DCHECK(input_.payload_allocator());

  if (size == 0) {
    // Need to copy, but the size is zero, so we don't need a destination
    // buffer.
    *payload_buffer_out = nullptr;
    return true;
  }

  if (allocate_callback_) {
    // The input side has provided a callback to do the actual allocation.
    // We'll use that.
    *payload_buffer_out = AllocateUsingAllocateCallback(size);
    return true;
  }

  // Allocate from the input's allocator.
  *payload_buffer_out = input_.payload_allocator()->AllocatePayloadBuffer(size);
  return true;
}

void PayloadManager::OnDisconnect() {
  std::lock_guard<std::mutex> locker(mutex_);

  // This |PayloadManager| remains associated with the input, so we clear
  // only the output configuration.
  output_.config_.mode_ = PayloadMode::kNotConfigured;
  output_.EnsureNoAllocator();
  input_.EnsureNoAllocator();
  copy_ = false;
}

bool PayloadManager::ready_locked() const {
  return input_.config_.mode_ != PayloadMode::kNotConfigured &&
         output_.config_.mode_ != PayloadMode::kNotConfigured;
}

void PayloadManager::UpdateAllocators() {
  FXL_DCHECK(output_.config_.mode_ != PayloadMode::kNotConfigured);
  FXL_DCHECK(input_.config_.mode_ != PayloadMode::kNotConfigured);
  FXL_DCHECK(input_.config_.mode_ != PayloadMode::kProvidesLocalMemory);

  // We may set this to true again.
  copy_ = false;

  switch (input_.config_.mode_) {
    case PayloadMode::kUsesLocalMemory:
      switch (output_.config_.mode_) {
        case PayloadMode::kUsesLocalMemory:
          // The output will allocate from its local memory allocator.
          // The input will read from local memory.
          output_.EnsureLocalMemoryAllocator();
          input_.EnsureNoAllocator();
          break;
        case PayloadMode::kProvidesLocalMemory:
          // The output itself will allocate local memory.
          // The input will read from local memory.
          output_.EnsureNoAllocator();
          input_.EnsureNoAllocator();
          break;
        case PayloadMode::kUsesVmos:
          // The output will have a VMO allocator with VMOs provided here.
          // The input will read from the mapped VMOs.
          ProvideVmosForSharedAllocator(output_.EnsureVmoAllocator());
          input_.EnsureNoAllocator();
          break;
        case PayloadMode::kProvidesVmos:
          // The output will provide VMOs to its own VMO allocator.
          // The input will read from the mapped VMOs.
          // If the output doesn't provide enough VMO memory, we may need to
          // give the input its own local memory allocator and perform copies.
          PrepareSharedAllocatorForExternalVmos(output_.EnsureVmoAllocator());
          input_.EnsureNoAllocator();
          break;
        default:
          FXL_DCHECK(false);
          break;
      }
      break;
    case PayloadMode::kUsesVmos:
      switch (output_.config_.mode_) {
        case PayloadMode::kUsesLocalMemory:
          // The output will allocate from the input's allocator.
          // The input will have a VMO allocator with VMOs provided here.
          output_.EnsureNoAllocator();
          ProvideVmosForSharedAllocator(input_.EnsureVmoAllocator());
          break;
        case PayloadMode::kProvidesLocalMemory:
          // The output itslef will allocate local memory.
          // The input will have a VMO allocator with VMOs provided here.
          // Payloads will be copied.
          output_.EnsureNoAllocator();
          ProvideVmos(input_.EnsureVmoAllocator(), input_.config_,
                      output_.config_.max_payload_size_,
                      input_.bti_handle_ ? &input_.bti_handle_ : nullptr);
          copy_ = true;
          break;
        case PayloadMode::kUsesVmos:
          // The input and the output share an allocator, which we associate
          // with the input by default. The input will have a VMO allocator
          // with VMOs provided here. The output will have access to those VMOs.
          output_.EnsureNoAllocator();
          ProvideVmosForSharedAllocator(input_.EnsureVmoAllocator());
          break;
        case PayloadMode::kProvidesVmos:
          // The connectors can share an allocator if their configurations are
          // compatible, and the input doesn't want to do its own allocations.
          // If the input wants to do its own allocations, we can't ask it to
          // do those allocations from VMOs provided by the output.
          if (ConfigsAreCompatible() && !allocate_callback_) {
            // The output will provide VMOs to its own VMO allocator.
            // The input will have access to those VMOs.
            // If the output doesn't provide enough VMO memory, we may need to
            // give the input its own VMO allocator and perform copies. See the
            // TODO(dalesat): at the top of this file.
            PrepareSharedAllocatorForExternalVmos(output_.EnsureVmoAllocator());
            input_.EnsureNoAllocator();
          } else {
            // The output will provide VMOs to its own VMO allocator.
            // The input will have a VMO allocator with VMOs provided here.
            PrepareForExternalVmos(output_.EnsureVmoAllocator(),
                                   output_.config_);
            ProvideVmos(input_.EnsureVmoAllocator(), input_.config_,
                        output_.config_.max_payload_size_,
                        input_.bti_handle_ ? &input_.bti_handle_ : nullptr);
            copy_ = true;
          }
          break;
        default:
          FXL_DCHECK(false);
          break;
      }
      break;
    case PayloadMode::kProvidesVmos:
      switch (output_.config_.mode_) {
        case PayloadMode::kUsesLocalMemory:
          // The output will allocate from the input's allocator.
          // The input will provide VMOs to its own VMO allocator.
          // If the input doesn't provide enough VMO memory, we may need to
          // give the output its own local memory allocator and perform copies.
          output_.EnsureNoAllocator();
          PrepareSharedAllocatorForExternalVmos(input_.EnsureVmoAllocator());
          break;
        case PayloadMode::kProvidesLocalMemory:
          // The output will allocate its own local memory.
          // The input will provide VMOs to its own VMO allocator.
          // Payloads will be copied.
          output_.EnsureNoAllocator();
          PrepareForExternalVmos(input_.EnsureVmoAllocator(), input_.config_);
          copy_ = true;
          break;
        case PayloadMode::kUsesVmos:
          if (ConfigsAreCompatible()) {
            // The output will allocate from the input's allocator.
            // The input will provide VMOs to its own VMO allocator.
            // If the input doesn't provide enough VMO memory, we may need to
            // give the output its own VMO allocator and perform copies.
            output_.EnsureNoAllocator();
            PrepareSharedAllocatorForExternalVmos(input_.EnsureVmoAllocator());
          } else {
            // The output will allocate from its own VMOs provided here.
            // The input will provide VMOs to its own VMO allocator.
            // Payloads will be copied.
            ProvideVmos(output_.EnsureVmoAllocator(), output_.config_, 0,
                        output_.bti_handle_ ? &output_.bti_handle_ : nullptr);
            PrepareForExternalVmos(input_.EnsureVmoAllocator(), input_.config_);
            copy_ = true;
          }
          break;
        case PayloadMode::kProvidesVmos:
          // The output will provide VMOs to its own VMO allocator.
          // The input will provide VMOs to its own VMO allocator.
          // Payloads will be copied.
          PrepareForExternalVmos(output_.EnsureVmoAllocator(), output_.config_);
          PrepareForExternalVmos(input_.EnsureVmoAllocator(), input_.config_);
          copy_ = true;
          break;
        default:
          // Input never has PayloadMode::kProvidesLocalMemory.
          FXL_DCHECK(false);
          break;
      }
      break;
    default:
      FXL_DCHECK(false);
      break;
  }
}

bool PayloadManager::ConfigsAreCompatible() const {
  FXL_DCHECK(ConfigModesAreCompatible());

  if (output_.config_.vmo_allocation_ == VmoAllocation::kSingleVmo &&
      input_.config_.vmo_allocation_ == VmoAllocation::kVmoPerBuffer) {
    // |vmo_allocation_| values are incompatible.
    return false;
  }

  if (output_.config_.vmo_allocation_ == VmoAllocation::kVmoPerBuffer &&
      input_.config_.vmo_allocation_ == VmoAllocation::kSingleVmo) {
    // |vmo_allocation_| values are incompatible.
    return false;
  }

  if ((output_.config_.mode_ == PayloadMode::kProvidesVmos) &&
      (output_.config_.vmo_allocation_ == VmoAllocation::kUnrestricted) &&
      (input_.config_.vmo_allocation_ != VmoAllocation::kUnrestricted)) {
    // The output will provide VMOS and makes no promises about VMO allocation.
    // The input has specific VMO allocation needs.
    return false;
  }

  if ((input_.config_.mode_ == PayloadMode::kProvidesVmos) &&
      (input_.config_.vmo_allocation_ == VmoAllocation::kUnrestricted) &&
      (output_.config_.vmo_allocation_ != VmoAllocation::kUnrestricted)) {
    // The input will provide VMOS and makes no promises about VMO allocation.
    // The output has specific VMO allocation needs.
    return false;
  }

  if ((output_.config_.mode_ == PayloadMode::kProvidesVmos) &&
      !output_.config_.physically_contiguous_ &&
      input_.config_.physically_contiguous_) {
    // The output will provide non-contiguous VMOS, but the input wants them
    // to be contiguous.
    return false;
  }

  if ((input_.config_.mode_ == PayloadMode::kProvidesVmos) &&
      !input_.config_.physically_contiguous_ &&
      output_.config_.physically_contiguous_) {
    // The input will provide non-contiguous VMOS, but the output wants them
    // to be contiguous.
    return false;
  }

  return true;
}

bool PayloadManager::ConfigModesAreCompatible() const {
  if (output_.config_.mode_ == PayloadMode::kProvidesLocalMemory) {
    if (input_.config_.mode_ == PayloadMode::kUsesVmos ||
        input_.config_.mode_ == PayloadMode::kProvidesVmos) {
      // The output is allocating local memory externally, and the input needs
      // VMOs.
      return false;
    }
  } else if (output_.config_.mode_ == PayloadMode::kProvidesVmos) {
    if (input_.config_.mode_ == PayloadMode::kProvidesVmos) {
      // Input and output both want to provide VMOs.
      return false;
    }
  }

  return true;
}

VmoAllocation PayloadManager::CombinedVmoAllocation() const {
  switch (output_.config_.vmo_allocation_) {
    case VmoAllocation::kNotApplicable:
      FXL_DCHECK(input_.config_.vmo_allocation_ !=
                 VmoAllocation::kNotApplicable);
      // Falls through.
    case VmoAllocation::kUnrestricted:
      if (input_.config_.vmo_allocation_ == VmoAllocation::kSingleVmo ||
          input_.config_.vmo_allocation_ == VmoAllocation::kVmoPerBuffer) {
        return input_.config_.vmo_allocation_;
      }

      return VmoAllocation::kUnrestricted;

    case VmoAllocation::kSingleVmo:
      FXL_DCHECK(input_.config_.vmo_allocation_ !=
                 VmoAllocation::kVmoPerBuffer);
      return VmoAllocation::kSingleVmo;

    case VmoAllocation::kVmoPerBuffer:
      FXL_DCHECK(input_.config_.vmo_allocation_ != VmoAllocation::kSingleVmo);
      return VmoAllocation::kVmoPerBuffer;
  }
}

void PayloadManager::ProvideVmos(VmoPayloadAllocator* allocator,
                                 const PayloadConfig& config,
                                 uint64_t other_connector_max_payload_size,
                                 const zx::handle* bti_handle) const {
  FXL_DCHECK(allocator);
  FXL_DCHECK(config.vmo_allocation_ != VmoAllocation::kNotApplicable);
  FXL_DCHECK(config.max_aggregate_payload_size_ != 0 ||
             (config.max_payload_count_ != 0 &&
              (config.max_payload_size_ != 0 ||
               other_connector_max_payload_size != 0)))
      << "other_connector_max_payload_size " << other_connector_max_payload_size
      << ", config: " << config;

  // TODO(dalesat): Reuse VMOs?
  allocator->RemoveAllVmos();

  // We want to use the larger of two max payload sizes.
  uint64_t max_payload_size =
      std::max(other_connector_max_payload_size, config.max_payload_size_);

  // Calculate a max aggregate size from the larger max payload size and the
  // payload count.
  uint64_t max_aggregate_payload_size =
      max_payload_size * config.max_payload_count_;

  // Use |config.max_aggregate_payload_size_| instead, if it's larger.
  if (max_aggregate_payload_size < config.max_aggregate_payload_size_) {
    max_aggregate_payload_size = config.max_aggregate_payload_size_;

    if (config.max_payload_size_ != 0) {
      // Align up |max_aggregate_payload_size| to the nearest
      // |config.max_payload_size_| boundary.
      max_aggregate_payload_size =
          AlignUp(max_aggregate_payload_size, config.max_payload_size_);
    }
  }

  // If allocation is unrestricted, choose between |kSingleVmo| and
  // |kVmoPerBuffer|. We use |kSingleVmo| unless physically-contiguous buffers
  // are required.
  VmoAllocation vmo_allocation = config.vmo_allocation_;
  if (vmo_allocation == VmoAllocation::kUnrestricted) {
    vmo_allocation = (bti_handle == nullptr) ? VmoAllocation::kSingleVmo
                                             : VmoAllocation::kVmoPerBuffer;
  }

  if (allocator->vmo_allocation() != vmo_allocation) {
    // TODO(dalesat): Make sure we never call this twice on the same allocator,
    // or make |VmoPayloadAllocator| support that.
    allocator->SetVmoAllocation(vmo_allocation);
  }

  if (vmo_allocation == VmoAllocation::kVmoPerBuffer) {
    FXL_DCHECK(max_aggregate_payload_size >= max_payload_size);
    FXL_DCHECK(max_payload_size != 0);

    // Allocate a VMO for each payload.
    for (uint64_t i = 0; i < max_aggregate_payload_size / max_payload_size;
         ++i) {
      allocator->AddVmo(PayloadVmo::Create(max_payload_size, bti_handle));
    }
  } else {
    FXL_DCHECK(vmo_allocation == VmoAllocation::kSingleVmo ||
               vmo_allocation == VmoAllocation::kUnrestricted);
    FXL_DCHECK(max_aggregate_payload_size != 0);

    // Create a single VMO from which to allocate all payloads.
    allocator->AddVmo(
        PayloadVmo::Create(max_aggregate_payload_size, bti_handle));
  }
}

void PayloadManager::ProvideVmosForSharedAllocator(
    VmoPayloadAllocator* allocator) const {
  FXL_DCHECK(allocator);

  PayloadConfig config;

  config.max_payload_size_ = std::max(output_.config_.max_payload_size_,
                                      input_.config_.max_payload_size_);
  config.max_payload_count_ =
      output_.config_.max_payload_count_ + input_.config_.max_payload_count_;

  // We can't simply add the |max_aggregate_payload_size_| from the two
  // connectors to get the combined value, because they may be using different
  // methods of expressing their requirements. If one connector is using
  // |max_aggregate_payload_size_| and the other is using the count/size values,
  // we can get a situation where we satisfy the max of their requirements
  // rather than the sum. For this reason, we artificially adjust the
  // |max_aggregate_payload_size_| values before adding them. If the
  // |max_payload_count_| for a given connector times the combined
  // |max_payload_size_| value is greater than that connector's
  // |max_aggregate_payload_size_|, we use that instead.
  uint64_t output_max_aggregate_payload_size =
      std::max(output_.config_.max_aggregate_payload_size_,
               config.max_payload_size_ * output_.config_.max_payload_count_);
  uint64_t input_max_aggregate_payload_size =
      std::max(input_.config_.max_aggregate_payload_size_,
               config.max_payload_size_ * input_.config_.max_payload_count_);

  config.max_aggregate_payload_size_ =
      output_max_aggregate_payload_size + input_max_aggregate_payload_size;

  config.vmo_allocation_ = CombinedVmoAllocation();
  config.physically_contiguous_ = output_.config_.physically_contiguous_ ||
                                  input_.config_.physically_contiguous_;

  const zx::handle* bti_handle = nullptr;
  if (output_.bti_handle_) {
    bti_handle = &output_.bti_handle_;
    // It's ok if both connector provide a bti handle. We can use either one.
  } else if (input_.bti_handle_) {
    bti_handle = &input_.bti_handle_;
  }

  ProvideVmos(allocator, config, 0, bti_handle);
}

void PayloadManager::PrepareForExternalVmos(VmoPayloadAllocator* allocator,
                                            const PayloadConfig& config) const {
  FXL_DCHECK(allocator);
  FXL_DCHECK(config.vmo_allocation_ != VmoAllocation::kNotApplicable);

  if (allocator->vmo_allocation() != config.vmo_allocation_) {
    allocator->SetVmoAllocation(config.vmo_allocation_);
  }
}

void PayloadManager::PrepareSharedAllocatorForExternalVmos(
    VmoPayloadAllocator* allocator) const {
  FXL_DCHECK(allocator);

  VmoAllocation vmo_allocation = CombinedVmoAllocation();
  if (allocator->vmo_allocation() != vmo_allocation) {
    allocator->SetVmoAllocation(vmo_allocation);
  }
}

fbl::RefPtr<PayloadBuffer> PayloadManager::AllocateUsingAllocateCallback(
    uint64_t size) const {
  FXL_DCHECK(allocate_callback_);
  FXL_DCHECK(input_.vmo_allocator_);

  // The input side has provided a callback to do the actual allocation.
  // In addition to the size, it needs the |PayloadVmos| interface from
  // the VMO allocator associated with the input.
  return allocate_callback_(size, *input_.vmo_allocator_);
}

///////////////////////////////////////////////////////////////////////////////
// PayloadManager::Connector implementation.

PayloadAllocator* PayloadManager::Connector::payload_allocator() const {
  if (local_memory_allocator_) {
    FXL_DCHECK(!vmo_allocator_);
    return local_memory_allocator_.get();
  }

  return vmo_allocator_.get();
}

void PayloadManager::Connector::EnsureNoAllocator() {
  local_memory_allocator_.reset();
  vmo_allocator_.reset();
}

void PayloadManager::Connector::EnsureLocalMemoryAllocator() {
  vmo_allocator_.reset();

  if (!local_memory_allocator_) {
    local_memory_allocator_ = LocalMemoryPayloadAllocator::Create();
  }
}

VmoPayloadAllocator* PayloadManager::Connector::EnsureVmoAllocator() {
  local_memory_allocator_.reset();

  if (!vmo_allocator_) {
    vmo_allocator_ = VmoPayloadAllocator::Create();
  }

  return vmo_allocator_.get();
}

}  // namespace media_player
