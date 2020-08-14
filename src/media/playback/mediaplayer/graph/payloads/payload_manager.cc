// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/graph/payloads/payload_manager.h"

#include "lib/fostr/fidl_types.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"

namespace media_player {
namespace {

// Returns the smallest multiple of |alignment| greater than or equal to |size|.
static uint64_t AlignUp(uint64_t size, uint64_t alignment) {
  uint64_t remainder = size % alignment;
  return remainder == 0 ? size : (size + alignment - remainder);
}

// Concretizes a |PayloadConfig| for the purposes of provisioning a VMO allocator with VMOS.
// 1) |kUnrestricted| |VmoAllocation| is replaced with |kSingleVmo|.
// 2) For |kSingleVmo|, |max_aggregate_payload_size_| is set to a good value (non-zero and
//    aligned up to |max_payload_size_| boundary).
// 3) For |kVmoPerBuffer|, |max_payload_size_| and |max_payload_count_| are set to good values
//    (non-zero and the product is at least |max_aggregate_payload_size_|).
PayloadConfig Concretize(PayloadConfig config) {
  FX_DCHECK(config.vmo_allocation_ != VmoAllocation::kNotApplicable);

  // If allocation is unrestricted, choose |kSingleVmo| by default.
  if (config.vmo_allocation_ == VmoAllocation::kUnrestricted) {
    config.vmo_allocation_ = VmoAllocation::kSingleVmo;
  }

  if (config.vmo_allocation_ == VmoAllocation::kVmoPerBuffer) {
    if (config.max_aggregate_payload_size_ == 0) {
      // |max_aggregate_payload_size_| was not provided, so both |max_payload_size_| and
      // |max_payload_count_| must be provided.
      FX_DCHECK(config.max_payload_size_ != 0);
      FX_DCHECK(config.max_payload_count_ != 0);
    } else if (config.max_payload_size_ == 0) {
      // |max_aggregate_payload_size_| was provided, but |max_payload_size_| was not. Calculate
      // |max_payload_size_| from |max_aggregate_payload_size_| and |max_payload_count_|, which must
      // be provided.
      FX_DCHECK(config.max_payload_count_ != 0);
      config.max_payload_size_ =
          (config.max_aggregate_payload_size_ + config.max_payload_count_ - 1) /
          config.max_payload_count_;
    } else if (config.max_payload_count_ == 0) {
      // |max_aggregate_payload_size_| was provided, but |max_payload_count_| was not. Calculate
      // |max_payload_count_| from |max_aggregate_payload_size_| and |max_payload_size_|, which is
      // provided.
      config.max_payload_count_ =
          (config.max_aggregate_payload_size_ + config.max_payload_size_ - 1) /
          config.max_payload_size_;
    }
  } else {
    FX_DCHECK(config.vmo_allocation_ == VmoAllocation::kSingleVmo);

    // Ensure that |max_aggregate_payload_size_| is at least the product of |max_payload_size_| and
    // |max_payload_count_|.
    config.max_aggregate_payload_size_ = std::max(
        config.max_aggregate_payload_size_, config.max_payload_size_ * config.max_payload_count_);

    if (config.max_payload_size_ != 0) {
      // Make sure |max_aggregate_payload_size_| is a multiple of |max_payload_size_|.
      config.max_aggregate_payload_size_ =
          AlignUp(config.max_aggregate_payload_size_, config.max_payload_size_);
    }
  }

  return config;
}

}  // namespace

// TODO(dalesat): Handle insufficient 'provided' vmos.
// TODO(dalesat): Make outputs declare whether they will use
// AllocatePayloadBufferForOutput or chop up VMOs their own way. That latter is
// incompatible with an input that provides an allocate callback.
// TODO(dalesat): Ensure we have the signalling we need for dynamic config
// changes.

PayloadManager::PayloadManager() {
  output_.owner_ = this;
  input_.owner_ = this;
}

void PayloadManager::Dump(std::ostream& os) const {
  std::lock_guard<std::mutex> locker(mutex_);
  DumpInternal(os);
}

void PayloadManager::DumpInternal(std::ostream& os) const {
  os << fostr::Indent;

  if (!ready_locked()) {
    os << fostr::NewLine << "ready: false";
  }

  if (copy_) {
    os << fostr::NewLine << "copy: true";
  }

  if (sysmem_allocator_) {
    os << fostr::NewLine << "sysmem allocator: " << sysmem_allocator_;
  }

  os << fostr::NewLine << "output:";
  os << fostr::Indent;

  if (output_.local_memory_allocator_) {
    os << fostr::NewLine << "local memory allocator: ";
    output_.local_memory_allocator_->Dump(os);
  } else if (output_.vmo_allocator_) {
    os << fostr::NewLine << "vmo allocator: ";
    output_.vmo_allocator_->Dump(os);
  }

  if (output_.sysmem_token_for_node_) {
    os << fostr::NewLine << "sysmem token for node: " << output_.sysmem_token_for_node_;
  }
  if (output_.sysmem_token_for_mate_or_provisioning_) {
    os << fostr::NewLine << "sysmem token for mate or provisioning: "
       << output_.sysmem_token_for_mate_or_provisioning_;
  }

  os << fostr::Outdent;
  os << fostr::NewLine << "input:";
  os << fostr::Indent;

  if (input_.local_memory_allocator_) {
    os << fostr::NewLine << "local memory allocator: ";
    input_.local_memory_allocator_->Dump(os);
  } else if (input_.vmo_allocator_) {
    os << fostr::NewLine << "vmo allocator: ";
    input_.vmo_allocator_->Dump(os);
  }

  if (input_.sysmem_token_for_node_) {
    os << fostr::NewLine << "sysmem token for node: " << input_.sysmem_token_for_node_;
  }
  if (input_.sysmem_token_for_mate_or_provisioning_) {
    os << fostr::NewLine << "sysmem token for mate or provisioning: "
       << input_.sysmem_token_for_mate_or_provisioning_;
  }

  os << fostr::Outdent;
  os << fostr::Outdent;
}

void PayloadManager::RegisterReadyCallbacks(fit::closure output, fit::closure input) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  ready_callback_for_output_ = std::move(output);
  ready_callback_for_input_ = std::move(input);
}

void PayloadManager::RegisterNewSysmemTokenCallbacks(fit::closure output, fit::closure input) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  new_sysmem_token_callback_for_output_ = std::move(output);
  new_sysmem_token_callback_for_input_ = std::move(input);
}

void PayloadManager::ApplyOutputConfiguration(const PayloadConfig& config,
                                              ServiceProvider* service_provider) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FX_DCHECK(config.mode_ != PayloadMode::kNotConfigured);
  FX_DCHECK((config.mode_ == PayloadMode::kUsesSysmemVmos) == !!service_provider);

  bool notify_input = false;

  {
    std::lock_guard<std::mutex> locker(mutex_);

    if (service_provider) {
      service_provider_ = service_provider;
    }

    ++ready_deferrals_;

    if (output_.config_.mode_ == PayloadMode::kProvidesVmos &&
        config.mode_ != PayloadMode::kProvidesVmos) {
      // The output was supplying VMOs but will no longer be doing so. Remove any
      // VMOs it left behind.
      output_external_vmos().RemoveAllVmos();
    }

    output_.config_ = config;

    if (input_.config_.mode_ != PayloadMode::kNotConfigured) {
      auto input_sysmem_token_generation = input_.sysmem_token_generation_;

      // Both connectors are configured, so we can get the allocators set up accordingly. If the
      // output is using |kUsesSysmemVmos|, |UpdateAllocators| will ensure that the output connector
      // has a token to provide to the upstream node.
      UpdateAllocators();

      if (input_sysmem_token_generation != input_.sysmem_token_generation_) {
        notify_input = true;
      }
    } else if (config.mode_ == PayloadMode::kUsesSysmemVmos) {
      // The input isn't configured yet, so we can't set up the allocators. The output is configured
      // to |kUsesSysmemVmos|, so the upstream node expects to be able to grab the sysmem token
      // after this call, so we make sure the tokens are created.
      EnsureBufferCollectionTokens(&output_);
    }
  }

  DecrementReadyDeferrals();

  // Notify the input node that it needs to get its new sysmem token.
  if (notify_input && new_sysmem_token_callback_for_input_) {
    new_sysmem_token_callback_for_input_();
  }
}

void PayloadManager::ApplyInputConfiguration(const PayloadConfig& config,
                                             AllocateCallback allocate_callback,
                                             ServiceProvider* service_provider) {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FX_DCHECK(config.mode_ != PayloadMode::kNotConfigured);
  FX_DCHECK(config.mode_ != PayloadMode::kProvidesLocalMemory);
  FX_DCHECK(allocate_callback == nullptr || config.mode_ == PayloadMode::kUsesVmos ||
            config.mode_ == PayloadMode::kProvidesVmos ||
            config.mode_ == PayloadMode::kUsesSysmemVmos);
  FX_DCHECK((config.mode_ == PayloadMode::kUsesSysmemVmos) == !!service_provider);

  bool notify_output = false;

  {
    std::lock_guard<std::mutex> locker(mutex_);

    if (service_provider) {
      service_provider_ = service_provider;
    }

    ++ready_deferrals_;

    if (input_.config_.mode_ == PayloadMode::kProvidesVmos &&
        config.mode_ != PayloadMode::kProvidesVmos) {
      // The input was supplying VMOs but will no longer be doing so. Remove any
      // VMOs it left behind.
      input_external_vmos().RemoveAllVmos();
    }

    input_.config_ = config;
    allocate_callback_ = std::move(allocate_callback);

    if (output_.config_.mode_ != PayloadMode::kNotConfigured) {
      auto output_sysmem_token_generation = output_.sysmem_token_generation_;

      // Both connectors are configured, so we can get the allocators set up accordingly. If the
      // output is using |kUsesSysmemVmos|, |UpdateAllocators| will ensure that the input connector
      // has a token to provide to the downstream node.
      UpdateAllocators();

      if (output_sysmem_token_generation != output_.sysmem_token_generation_) {
        notify_output = true;
      }
    } else if (config.mode_ == PayloadMode::kUsesSysmemVmos) {
      // The output isn't configured yet, so we can't set up the allocators. The input is configured
      // to |kUsesSysmemVmos|, so the downstream node expects to be able to grab the sysmem token
      // after this call, so we make sure the tokens are created.
      EnsureBufferCollectionTokens(&input_);
    }
  }

  DecrementReadyDeferrals();

  // Notify the output node that it needs to get its new sysmem token.
  if (notify_output && new_sysmem_token_callback_for_output_) {
    new_sysmem_token_callback_for_output_();
  }
}

bool PayloadManager::ready() const {
  std::lock_guard<std::mutex> locker(mutex_);
  return ready_locked();
}

fbl::RefPtr<PayloadBuffer> PayloadManager::AllocatePayloadBufferForOutput(uint64_t size) const {
  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(ready_locked());
  FX_DCHECK(output_.config_.mode_ != PayloadMode::kProvidesLocalMemory);

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

  FX_DCHECK(allocator);

  return allocator->AllocatePayloadBuffer(size);
}

PayloadVmos& PayloadManager::input_vmos() const {
  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(ready_locked());
  FX_DCHECK(input_.config_.mode_ == PayloadMode::kUsesVmos ||
            input_.config_.mode_ == PayloadMode::kProvidesVmos ||
            input_.config_.mode_ == PayloadMode::kUsesSysmemVmos);

  VmoPayloadAllocator* result = input_vmo_payload_allocator_locked();
  FX_DCHECK(result);

  return *result;
}

PayloadVmoProvision& PayloadManager::input_external_vmos() const {
  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(ready_locked());
  FX_DCHECK(input_.config_.mode_ == PayloadMode::kProvidesVmos);

  VmoPayloadAllocator* result = input_vmo_payload_allocator_locked();
  FX_DCHECK(result);

  return *result;
}

fuchsia::sysmem::BufferCollectionTokenPtr PayloadManager::TakeInputSysmemToken() {
  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(input_.config_.mode_ == PayloadMode::kUsesSysmemVmos);
  FX_DCHECK(input_.sysmem_token_for_node_);
  return std::move(input_.sysmem_token_for_node_);
}

PayloadVmos& PayloadManager::output_vmos() const {
  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(ready_locked());
  FX_DCHECK(output_.config_.mode_ == PayloadMode::kUsesVmos ||
            output_.config_.mode_ == PayloadMode::kProvidesVmos ||
            output_.config_.mode_ == PayloadMode::kUsesSysmemVmos);

  VmoPayloadAllocator* result = output_vmo_payload_allocator_locked();
  FX_DCHECK(result);

  return *result;
}

PayloadVmoProvision& PayloadManager::output_external_vmos() const {
  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(ready_locked());
  FX_DCHECK(output_.config_.mode_ == PayloadMode::kProvidesVmos);

  VmoPayloadAllocator* result = output_vmo_payload_allocator_locked();
  FX_DCHECK(result);

  return *result;
}

fuchsia::sysmem::BufferCollectionTokenPtr PayloadManager::TakeOutputSysmemToken() {
  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(output_.config_.mode_ == PayloadMode::kUsesSysmemVmos);
  FX_DCHECK(output_.sysmem_token_for_node_);
  return std::move(output_.sysmem_token_for_node_);
}

bool PayloadManager::MaybeAllocatePayloadBufferForCopy(
    uint64_t size, fbl::RefPtr<PayloadBuffer>* payload_buffer_out) const {
  FX_DCHECK(payload_buffer_out || (size == 0));

  std::lock_guard<std::mutex> locker(mutex_);
  FX_DCHECK(ready_locked());

  if (!copy_) {
    // Don't need to copy.
    return false;
  }

  FX_DCHECK(input_.payload_allocator());

  if (size == 0) {
    // Need to copy, but the size is zero, so we don't need a destination buffer.
    if (payload_buffer_out) {
      *payload_buffer_out = nullptr;
    }
    return true;
  }

  if (allocate_callback_) {
    // The input side has provided a callback to do the actual allocation. We'll use that.
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
  return ready_deferrals_ == 0 && output_.config_.mode_ != PayloadMode::kNotConfigured &&
         input_.config_.mode_ != PayloadMode::kNotConfigured;
}

void PayloadManager::EnsureBufferCollectionTokens(Connector* connector) {
  FX_DCHECK(connector);

  if (connector->sysmem_token_for_mate_or_provisioning_) {
    // Already has tokens.
    return;
  }

  EnsureSysmemAllocator();
  sysmem_allocator_->AllocateSharedCollection(connector->sysmem_token_for_node_.NewRequest());
  connector->sysmem_token_for_node_->Duplicate(
      ZX_DEFAULT_VMO_RIGHTS, connector->sysmem_token_for_mate_or_provisioning_.NewRequest());

  connector->sysmem_token_generation_++;
}

void PayloadManager::ShareBufferCollection(
    Connector* from, Connector* to,
    fidl::InterfaceRequest<fuchsia::sysmem::BufferCollectionToken> dup) {
  FX_DCHECK(from);
  FX_DCHECK(to);
  FX_DCHECK(dup);
  FX_DCHECK(from->sysmem_token_for_mate_or_provisioning_);
  FX_DCHECK(!to->sysmem_token_for_mate_or_provisioning_);
  FX_DCHECK(!to->sysmem_token_for_node_);
  to->sysmem_token_for_node_ = std::move(from->sysmem_token_for_mate_or_provisioning_);
  to->sysmem_token_for_node_->Duplicate(ZX_DEFAULT_VMO_RIGHTS, std::move(dup));

  to->sysmem_token_generation_++;
}

void PayloadManager::DecrementReadyDeferrals() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);

  {
    std::lock_guard<std::mutex> locker(mutex_);
    FX_DCHECK(ready_deferrals_ != 0);
    --ready_deferrals_;

    if (!ready_locked()) {
      return;
    }
  }

  if (ready_callback_for_output_) {
    ready_callback_for_output_();
  }

  if (ready_callback_for_input_) {
    ready_callback_for_input_();
  }
}

void PayloadManager::EnsureSysmemAllocator() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FX_CHECK(service_provider_);
  if (!sysmem_allocator_) {
    sysmem_allocator_ = service_provider_->ConnectToService<fuchsia::sysmem::Allocator>();
  }
}

void PayloadManager::UpdateAllocators() {
  FXL_DCHECK_CREATION_THREAD_IS_CURRENT(thread_checker_);
  FX_DCHECK(output_.config_.mode_ != PayloadMode::kNotConfigured);
  FX_DCHECK(input_.config_.mode_ != PayloadMode::kNotConfigured);
  FX_DCHECK(input_.config_.mode_ != PayloadMode::kProvidesLocalMemory);

  // This method is called by |ApplyOutputConfiguration| and |ApplyInputConfiguration|, which may
  // be called in either order. The first time one of the Apply methods is called, we don't have
  // a payload config for both connectors, so this method is not called.

  // When |ApplyXxxConfiguration| is called, and the connector is being configured to use sysmem,
  // the caller expects the buffer collection token to be available immediately after the call.
  // For this reason, when the |ApplyXxxConfiguration| methods *do not* call |UpdateAllocators|,
  // they instead call |EnsureBufferCollectionTokens|. In all other cases, |UpdateAllocators| is
  // responsible for calling |EnsureBufferCollectionTokens| to make sure connectors have their
  // tokens. This allows |UpdateAllocators| to refrain from creating a second pair of tokens when
  // the two connectors need to share a buffer collection.

  // We may set this to true again later.
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
          output_.EnsureProvisionedVmoAllocator(CombinedConfig());
          input_.EnsureNoAllocator();
          break;
        case PayloadMode::kProvidesVmos:
          // The output will provide VMOs to its own VMO allocator.
          // The input will read from the mapped VMOs.
          // If the output doesn't provide enough VMO memory, we may need to
          // give the input its own local memory allocator and perform copies.
          output_.EnsureExternalVmoAllocator(CombinedVmoAllocation());
          input_.EnsureNoAllocator();
          break;
        case PayloadMode::kUsesSysmemVmos:
          // The output will use sysmem VMOs.
          // The input will read from the mapped sysmem VMOs.
          // We don't need to specify vmo allocation, because the output does all the allocation
          // itself.
          ++ready_deferrals_;
          EnsureBufferCollectionTokens(&output_);
          output_.EnsureProvisionedSysmemVmoAllocator(input_.config_, sysmem_allocator_.get(),
                                                      CombinedVmoAllocation());
          input_.EnsureNoAllocator();
          break;
        default:
          FX_DCHECK(false);
          break;
      }
      break;
    case PayloadMode::kUsesVmos:
      switch (output_.config_.mode_) {
        case PayloadMode::kUsesLocalMemory:
          // The output will allocate from the input's allocator.
          // The input will have a VMO allocator with VMOs provided here.
          output_.EnsureNoAllocator();
          input_.EnsureProvisionedVmoAllocator(CombinedConfig());
          break;
        case PayloadMode::kProvidesLocalMemory:
          // The output itself will allocate local memory.
          // The input will have a VMO allocator with VMOs provided here.
          // Payloads will be copied.
          output_.EnsureNoAllocator();
          input_.EnsureProvisionedVmoAllocator(AugmentedInputConfig());
          copy_ = true;
          break;
        case PayloadMode::kUsesVmos:
          // The input and the output share an allocator, which we associate
          // with the input by default. The input will have a VMO allocator
          // with VMOs provided here. The output will have access to those VMOs.
          output_.EnsureNoAllocator();
          input_.EnsureProvisionedVmoAllocator(CombinedConfig());
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
            output_.EnsureExternalVmoAllocator(CombinedVmoAllocation());
            input_.EnsureNoAllocator();
          } else {
            // The output will provide VMOs to its own VMO allocator.
            // The input will have a VMO allocator with VMOs provided here.
            output_.EnsureExternalVmoAllocator();
            input_.EnsureProvisionedVmoAllocator(AugmentedInputConfig());
            copy_ = true;
          }
          break;
        case PayloadMode::kUsesSysmemVmos:
          if (ConfigsAreCompatible()) {
            // The output will use sysmem VMOs.
            // The input will have a VMO allocator with VMOs from sysmem.
            // We don't need to specify vmo allocation, because the output does all the allocation
            // itself.
            ++ready_deferrals_;
            EnsureBufferCollectionTokens(&output_);
            output_.EnsureProvisionedSysmemVmoAllocator(input_.config_, sysmem_allocator_.get(),
                                                        CombinedVmoAllocation());
            input_.EnsureNoAllocator();
          } else {
            // The output will use sysmem VMOs.
            // The input will allocate from its own VMOs provided here.
            // Payloads will be copied.
            ++ready_deferrals_;
            EnsureBufferCollectionTokens(&output_);
            output_.EnsureProvisionedSysmemVmoAllocator(input_.config_, sysmem_allocator_.get(),
                                                        output_.config_.vmo_allocation_);
            input_.EnsureProvisionedVmoAllocator(AugmentedInputConfig());
            copy_ = true;
          }
          break;
        default:
          FX_DCHECK(false);
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
          input_.EnsureExternalVmoAllocator(CombinedVmoAllocation());
          break;
        case PayloadMode::kProvidesLocalMemory:
          // The output will allocate its own local memory.
          // The input will provide VMOs to its own VMO allocator.
          // Payloads will be copied.
          output_.EnsureNoAllocator();
          input_.EnsureExternalVmoAllocator();
          copy_ = true;
          break;
        case PayloadMode::kUsesVmos:
          if (ConfigsAreCompatible()) {
            // The output will allocate from the input's allocator.
            // The input will provide VMOs to its own VMO allocator.
            // If the input doesn't provide enough VMO memory, we may need to
            // give the output its own VMO allocator and perform copies.
            output_.EnsureNoAllocator();
            input_.EnsureExternalVmoAllocator(CombinedVmoAllocation());
          } else {
            // The output will allocate from its own VMOs provided here.
            // The input will provide VMOs to its own VMO allocator.
            // Payloads will be copied.
            output_.EnsureProvisionedVmoAllocator(output_.config_);
            input_.EnsureExternalVmoAllocator();
            copy_ = true;
          }
          break;
        case PayloadMode::kProvidesVmos:
          // The output will provide VMOs to its own VMO allocator.
          // The input will provide VMOs to its own VMO allocator.
          // Payloads will be copied.
          output_.EnsureExternalVmoAllocator();
          input_.EnsureExternalVmoAllocator();
          copy_ = true;
          break;
        case PayloadMode::kUsesSysmemVmos:
          // The output will use sysmem VMOs.
          // The input will provide VMOs to its own VMO allocator.
          // Payloads will be copied.
          // For the output allocator, we use the output's specified VMO allocation.
          ++ready_deferrals_;
          EnsureBufferCollectionTokens(&output_);
          output_.EnsureProvisionedSysmemVmoAllocator(input_.config_, sysmem_allocator_.get(),
                                                      output_.config_.vmo_allocation_);
          input_.EnsureExternalVmoAllocator();
          copy_ = true;
          break;
        default:
          // Input never has PayloadMode::kProvidesLocalMemory.
          FX_DCHECK(false);
          break;
      }
      break;
    case PayloadMode::kUsesSysmemVmos:
      switch (output_.config_.mode_) {
        case PayloadMode::kUsesLocalMemory:
          // The output will allocate from the input's allocator.
          // The input will use sysmem VMOs.
          output_.EnsureNoAllocator();
          ++ready_deferrals_;
          EnsureBufferCollectionTokens(&input_);
          input_.EnsureProvisionedSysmemVmoAllocator(output_.config_, sysmem_allocator_.get(),
                                                     input_.config_.vmo_allocation_);
          break;
        case PayloadMode::kProvidesLocalMemory:
          // The output will allocate its own local memory.
          // The input will use sysmem VMOs.
          // Payloads will be copied.
          // We use the input's VMO allocation, because that constraint needs to be met, and the
          // copier doesn't care.
          output_.EnsureNoAllocator();
          ++ready_deferrals_;
          EnsureBufferCollectionTokens(&input_);
          input_.EnsureProvisionedSysmemVmoAllocator(CopyToOutputConfig(), sysmem_allocator_.get(),
                                                     input_.config_.vmo_allocation_);
          copy_ = true;
          break;
        case PayloadMode::kUsesVmos:
          if (ConfigsAreCompatible()) {
            // The output will allocate from the input's allocator.
            // The input will use sysmem VMOs.
            // We use the combined VMO allocation of the input and output, because we need to
            // apply the constraints of both.
            output_.EnsureNoAllocator();
            ++ready_deferrals_;
            EnsureBufferCollectionTokens(&input_);
            input_.EnsureProvisionedSysmemVmoAllocator(output_.config_, sysmem_allocator_.get(),
                                                       CombinedVmoAllocation());
          } else {
            // The output will allocate from its own VMOs provided here.
            // The input will use sysmem VMOs.
            // Payloads will be copied.
            // We use the input's VMO allocation, because that constraint needs to be met, and the
            // copier doesn't care.
            output_.EnsureProvisionedVmoAllocator(output_.config_);
            ++ready_deferrals_;
            EnsureBufferCollectionTokens(&input_);
            input_.EnsureProvisionedSysmemVmoAllocator(
                CopyToOutputConfig(), sysmem_allocator_.get(), input_.config_.vmo_allocation_);
            copy_ = true;
          }
          break;
        case PayloadMode::kProvidesVmos:
          // The output will provide VMOs to its own VMO allocator.
          // The input will use sysmem VMOs.
          // Payloads will be copied.
          // We use the input's VMO allocation, because that constraint needs to be met, and the
          // copier doesn't care.
          output_.EnsureExternalVmoAllocator();
          ++ready_deferrals_;
          EnsureBufferCollectionTokens(&input_);
          input_.EnsureProvisionedSysmemVmoAllocator(CopyToOutputConfig(), sysmem_allocator_.get(),
                                                     input_.config_.vmo_allocation_);
          copy_ = true;
          break;
        case PayloadMode::kUsesSysmemVmos:
          FX_DCHECK(output_.sysmem_token_for_mate_or_provisioning_ ||
                    input_.sysmem_token_for_mate_or_provisioning_);
          if (ConfigsAreCompatible()) {
            // The output and input will share sysmem VMOs.
            output_.EnsureNoAllocator();

            // We need a third token for the 'silent' participant.
            fuchsia::sysmem::BufferCollectionTokenPtr third_token;

            // If we're configuring this connection for the first time, one of the connectors
            // will have buffer collection tokens already, and we'll use those. If we're
            // reconfiguring, we'll need a fresh set of tokens.
            if (input_.sysmem_token_for_mate_or_provisioning_) {
              // The downstream connector (input) has its tokens, so we'll use that collection.
              ShareBufferCollection(&input_, &output_, third_token.NewRequest());
            } else if (output_.sysmem_token_for_mate_or_provisioning_) {
              // The upstream connector (output) has its tokens, so we'll use that collection.
              ShareBufferCollection(&output_, &input_, third_token.NewRequest());
            } else {
              // This connection was configured previously. None of the connector tokens are
              // populated, because they've already been used. We need to make a new set of
              // tokens.
              EnsureBufferCollectionTokens(&input_);
              ShareBufferCollection(&input_, &output_, third_token.NewRequest());
            }

            // We provision a VMO allocator, but only so we know how many buffers are in the
            // collection. This adds a third 'silent' participant in the collection, which is why
            // we created a third token.
            // TODO(fxbug.dev/38243): Remove when we don't need to know the buffer count.
            FX_CHECK(third_token);
            FX_CHECK(!input_.sysmem_token_for_mate_or_provisioning_);
            input_.sysmem_token_for_mate_or_provisioning_ = std::move(third_token);
            ++ready_deferrals_;
            input_.EnsureProvisionedSysmemVmoAllocator(
                PayloadConfig{.mode_ = PayloadMode::kUsesSysmemVmos,
                              .max_aggregate_payload_size_ = 0,
                              .max_payload_count_ = 0,
                              .max_payload_size_ = 0,
                              .vmo_allocation_ = CombinedVmoAllocation(),
                              .map_flags_ = 0},
                sysmem_allocator_.get(), CombinedVmoAllocation());
          } else {
            // The output will use sysmem VMOs.
            // The input will use sysmem VMOs.
            // Payloads will be copied.
            // We use the VMO allocation of the output for output and input for input, because their
            // respective constraints must be met and the copier doesn't care.
            ++ready_deferrals_;
            EnsureBufferCollectionTokens(&output_);
            output_.EnsureProvisionedSysmemVmoAllocator(input_.config_, sysmem_allocator_.get(),
                                                        output_.config_.vmo_allocation_);
            ++ready_deferrals_;
            EnsureBufferCollectionTokens(&input_);
            input_.EnsureProvisionedSysmemVmoAllocator(
                CopyToOutputConfig(), sysmem_allocator_.get(), input_.config_.vmo_allocation_);
            copy_ = true;
          }
          break;
        default:
          // Input never has PayloadMode::kProvidesLocalMemory.
          FX_DCHECK(false);
          break;
      }
      break;
    default:
      FX_DCHECK(false);
      break;
  }
}

bool PayloadManager::ConfigsAreCompatible() const {
  FX_DCHECK(ConfigModesAreCompatible());

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

  return true;
}

bool PayloadManager::ConfigModesAreCompatible() const {
  if (output_.config_.mode_ == PayloadMode::kProvidesLocalMemory) {
    if (input_.config_.mode_ == PayloadMode::kUsesVmos ||
        input_.config_.mode_ == PayloadMode::kProvidesVmos ||
        input_.config_.mode_ == PayloadMode::kUsesSysmemVmos) {
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
      FX_DCHECK(input_.config_.vmo_allocation_ != VmoAllocation::kNotApplicable);
      // Falls through.
    case VmoAllocation::kUnrestricted:
      if (input_.config_.vmo_allocation_ == VmoAllocation::kSingleVmo ||
          input_.config_.vmo_allocation_ == VmoAllocation::kVmoPerBuffer) {
        return input_.config_.vmo_allocation_;
      }

      return VmoAllocation::kUnrestricted;

    case VmoAllocation::kSingleVmo:
      FX_DCHECK(input_.config_.vmo_allocation_ != VmoAllocation::kVmoPerBuffer);
      return VmoAllocation::kSingleVmo;

    case VmoAllocation::kVmoPerBuffer:
      FX_DCHECK(input_.config_.vmo_allocation_ != VmoAllocation::kSingleVmo);
      return VmoAllocation::kVmoPerBuffer;
  }
}

PayloadConfig PayloadManager::CombinedConfig() const {
  PayloadConfig config;

  config.max_payload_size_ =
      std::max(output_.config_.max_payload_size_, input_.config_.max_payload_size_);
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

  config.map_flags_ = output_.config_.map_flags_ | input_.config_.map_flags_;

  return config;
}

PayloadConfig PayloadManager::AugmentedInputConfig() const {
  PayloadConfig config = input_.config_;

  if (config.max_payload_size_ < output_.config_.max_payload_size_) {
    config.max_payload_size_ = output_.config_.max_payload_size_;
  }

  return config;
}

PayloadConfig PayloadManager::CopyToOutputConfig() const {
  PayloadConfig config = output_.config_;

  config.max_payload_count_ = 0;         // Copying, so no packets for output.
  config.map_flags_ = ZX_VM_PERM_WRITE;  // Need to write for copies.

  return config;
}

fbl::RefPtr<PayloadBuffer> PayloadManager::AllocateUsingAllocateCallback(uint64_t size) const {
  FX_DCHECK(allocate_callback_);
  FX_DCHECK(input_.vmo_allocator_);

  // The input side has provided a callback to do the actual allocation.
  // In addition to the size, it needs the |PayloadVmos| interface from
  // the VMO allocator associated with the input.
  return allocate_callback_(size, *input_.vmo_allocator_);
}

///////////////////////////////////////////////////////////////////////////////
// PayloadManager::Connector implementation.

void PayloadManager::Connector::EnsureNoAllocator() {
  local_memory_allocator_ = nullptr;
  vmo_allocator_ = nullptr;
}

void PayloadManager::Connector::EnsureLocalMemoryAllocator() {
  vmo_allocator_ = nullptr;

  if (!local_memory_allocator_) {
    local_memory_allocator_ = LocalMemoryPayloadAllocator::Create();
  }
}

void PayloadManager::Connector::EnsureEmptyVmoAllocator(VmoAllocation vmo_allocation) {
  local_memory_allocator_ = nullptr;

  if (!vmo_allocator_) {
    vmo_allocator_ = VmoPayloadAllocator::Create();
  }

  FX_DCHECK(vmo_allocator_);
  vmo_allocator_->RemoveAllVmos();

  if (sysmem_collection_) {
    sysmem_collection_->Close();
  }

  sysmem_collection_ = nullptr;

  if (vmo_allocator_->vmo_allocation() != vmo_allocation) {
    vmo_allocator_->SetVmoAllocation(vmo_allocation);
  }
}

void PayloadManager::Connector::EnsureExternalVmoAllocator(VmoAllocation vmo_allocation) {
  if (vmo_allocation == VmoAllocation::kNotApplicable) {
    vmo_allocation = config_.vmo_allocation_;
    FX_DCHECK(vmo_allocation != VmoAllocation::kNotApplicable);
  }

  EnsureEmptyVmoAllocator(vmo_allocation);
}

void PayloadManager::Connector::EnsureProvisionedVmoAllocator(const PayloadConfig& config) {
  PayloadConfig concrete_config = Concretize(config);

  EnsureEmptyVmoAllocator(concrete_config.vmo_allocation_);

  if (concrete_config.vmo_allocation_ == VmoAllocation::kVmoPerBuffer) {
    FX_DCHECK(concrete_config.max_payload_size_ != 0);
    FX_DCHECK(concrete_config.max_payload_count_ != 0);

    // Allocate a VMO for each payload.
    for (uint64_t i = 0; i < concrete_config.max_payload_count_; ++i) {
      vmo_allocator_->AddVmo(PayloadVmo::Create(concrete_config.max_payload_size_,
                                                ZX_VM_PERM_READ | ZX_VM_PERM_WRITE));
    }
  } else {
    FX_DCHECK(concrete_config.vmo_allocation_ == VmoAllocation::kSingleVmo);
    FX_DCHECK(concrete_config.max_aggregate_payload_size_ != 0);

    // Create a single VMO from which to allocate all payloads.
    vmo_allocator_->AddVmo(PayloadVmo::Create(concrete_config.max_aggregate_payload_size_,
                                              ZX_VM_PERM_READ | ZX_VM_PERM_WRITE));
  }
}

void PayloadManager::Connector::EnsureProvisionedSysmemVmoAllocator(
    const PayloadConfig& local_config, fuchsia::sysmem::Allocator* sysmem_allocator,
    VmoAllocation vmo_allocation) {
  FX_DCHECK(sysmem_allocator);
  FX_DCHECK(sysmem_token_for_mate_or_provisioning_);
  FX_DCHECK(owner_);

  EnsureEmptyVmoAllocator(vmo_allocation);

  sysmem_token_for_mate_or_provisioning_->Sync([this, config = local_config, sysmem_allocator]() {
    FX_DCHECK(sysmem_token_for_mate_or_provisioning_);
    sysmem_allocator->BindSharedCollection(std::move(sysmem_token_for_mate_or_provisioning_),
                                           sysmem_collection_.NewRequest());

    uint32_t cpu_usage;

    switch (config.map_flags_) {
      case ZX_VM_PERM_READ:
        cpu_usage = fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageReadOften;
        break;
      case ZX_VM_PERM_WRITE:
        cpu_usage = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageWriteOften;
        break;
      case ZX_VM_PERM_READ | ZX_VM_PERM_WRITE:
        cpu_usage = fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageReadOften |
                    fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageWriteOften;
        break;
      default:
        // This is a bit of a hack. This 'no mapping' case only happens between the video decoder
        // and the video renderer. In this case, the scenic image pipe that receives these VMOs
        // maps them r/w, so we ask for those permissions.
        // TODO(dalesat): Need to be able to specify CPU usage without implied mapping.
        cpu_usage = fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageReadOften |
                    fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageWriteOften;
        break;
    }

    fuchsia::sysmem::BufferCollectionConstraints constraints{
        .usage = {.cpu = cpu_usage},
        .min_buffer_count_for_camping = config.max_payload_count_,
        .min_buffer_count_for_dedicated_slack = 0,
        .min_buffer_count_for_shared_slack = 0,
        .min_buffer_count = 0,
        .max_buffer_count = 0,
        .has_buffer_memory_constraints = true,
        .image_format_constraints_count = 0};
    constraints.buffer_memory_constraints.min_size_bytes = config.max_payload_size_;
    constraints.buffer_memory_constraints.heap_permitted_count = 0;
    constraints.buffer_memory_constraints.ram_domain_supported = true;

    if (config.output_video_constraints_) {
      constraints.image_format_constraints_count = 1;
      constraints.image_format_constraints[0] = *config.output_video_constraints_;
    }

    sysmem_collection_->SetConstraints(true, constraints);

    sysmem_collection_->WaitForBuffersAllocated(
        [this, map_flags = config.map_flags_](
            zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 collection_info) {
          if (status != ZX_OK) {
            FX_PLOGS(ERROR, status) << "Sysmem buffer allocation failed";
            return;
          }

          for (uint32_t i = 0; i < collection_info.buffer_count; ++i) {
            auto& vmo_buffer = collection_info.buffers[i];
            FX_DCHECK(vmo_buffer.vmo_usable_start == 0);
            FX_DCHECK(vmo_buffer.vmo);
            // When |map_flags| is |ZX_VM_PERM_WRITE|, we 'or' in |ZX_VM_PERM_READ|, otherwise the
            // map call fails with |ZX_ERR_INVALID_ARGS|.
            vmo_allocator_->AddVmo(PayloadVmo::Create(
                std::move(vmo_buffer.vmo),
                map_flags == ZX_VM_PERM_WRITE ? (ZX_VM_PERM_READ | ZX_VM_PERM_WRITE) : map_flags));
          }

          owner_->DecrementReadyDeferrals();
        });
  });
}

PayloadAllocator* PayloadManager::Connector::payload_allocator() const {
  if (local_memory_allocator_) {
    FX_DCHECK(!vmo_allocator_);
    return local_memory_allocator_.get();
  }

  return vmo_allocator_.get();
}

}  // namespace media_player
