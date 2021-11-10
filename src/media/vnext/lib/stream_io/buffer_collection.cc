// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/stream_io/buffer_collection.h"

#include <lib/async/default.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fostr/fidl/fuchsia/media2/formatting.h"

namespace fmlib {
namespace {

fuchsia::media2::ConnectionError ToConnectionError(fuchsia::media2::BufferProviderError error) {
  static_assert(static_cast<uint32_t>(fuchsia::media2::BufferProviderError::OVERCONSTRAINED) ==
                static_cast<uint32_t>(fuchsia::media2::ConnectionError::OVERCONSTRAINED));
  static_assert(static_cast<uint32_t>(fuchsia::media2::BufferProviderError::UNDERCONSTRAINED) ==
                static_cast<uint32_t>(fuchsia::media2::ConnectionError::UNDERCONSTRAINED));
  static_assert(static_cast<uint32_t>(fuchsia::media2::BufferProviderError::INSUFFICIENT_MEMORY) ==
                static_cast<uint32_t>(fuchsia::media2::ConnectionError::INSUFFICIENT_MEMORY));
  static_assert(static_cast<uint32_t>(fuchsia::media2::BufferProviderError::NO_PARTICIPANTS) ==
                static_cast<uint32_t>(fuchsia::media2::ConnectionError::NOT_USED));
  static_assert(
      static_cast<uint32_t>(fuchsia::media2::BufferProviderError::TIMED_OUT_WAITING_FOR_CREATION) ==
      static_cast<uint32_t>(fuchsia::media2::ConnectionError::TIMED_OUT_WAITING_FOR_CREATION));
  static_assert(
      static_cast<uint32_t>(
          fuchsia::media2::BufferProviderError::TIMED_OUT_WAITING_FOR_PARTICPANT) ==
      static_cast<uint32_t>(fuchsia::media2::ConnectionError::TIMED_OUT_WAITING_FOR_PARTICPANT));
  static_assert(static_cast<uint32_t>(fuchsia::media2::BufferProviderError::ACCESS_DENIED) ==
                static_cast<uint32_t>(fuchsia::media2::ConnectionError::ACCESS_DENIED));
  static_assert(static_cast<uint32_t>(fuchsia::media2::BufferProviderError::MALFORMED_REQUEST) ==
                static_cast<uint32_t>(fuchsia::media2::ConnectionError::MALFORMED_REQUEST));
  static_assert(static_cast<uint32_t>(fuchsia::media2::BufferProviderError::NOT_SUPPORTED) ==
                static_cast<uint32_t>(fuchsia::media2::ConnectionError::NOT_SUPPORTED));
  return static_cast<fuchsia::media2::ConnectionError>(error);
}

}  // namespace

std::vector<zx::vmo> BufferCollection::DuplicateVmos(zx_rights_t rights) const {
  std::vector<zx::vmo> result;

  std::lock_guard<std::mutex> locker(mutex_);
  std::transform(buffer_vmos().begin(), buffer_vmos().end(), std::back_inserter(result),
                 [rights](const BufferVmo& buffer_vmo) {
                   zx::vmo vmo;
                   zx_status_t status = buffer_vmo.vmo().duplicate(rights, &vmo);
                   FX_CHECK(status == ZX_OK) << "Failed to duplicate vmo.";
                   return vmo;
                 });

  return result;
}

// static
fpromise::promise<std::vector<zx::vmo>, fuchsia::media2::BufferProviderError>
BufferCollection::GetBuffers(fuchsia::media2::BufferProvider& provider, zx::eventpair token,
                             const fuchsia::media2::BufferConstraints& constraints,
                             // TODO(dalesat): change parameters so the NOLINT isn't needed.
                             // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                             const std::string& name, uint64_t id, zx_vm_option_t map_flags) {
  fuchsia::media2::BufferRights rights{};
  if ((map_flags & ZX_VM_PERM_READ) != 0) {
    rights |= fuchsia::media2::BufferRights::READ;
  }
  if ((map_flags & ZX_VM_PERM_WRITE) != 0) {
    rights |= fuchsia::media2::BufferRights::WRITE;
  }

  fpromise::bridge<fuchsia::media2::BufferProvider_GetBuffers_Result> bridge;

  provider.GetBuffers(std::move(token), fidl::Clone(constraints), rights, name, id,
                      bridge.completer.bind());

  return bridge.consumer.promise().then(
      [](fpromise::result<fuchsia::media2::BufferProvider_GetBuffers_Result, void>&
             result) mutable {
        return static_cast<
            fpromise::result<std::vector<zx::vmo>, fuchsia::media2::BufferProviderError>>(
            result.take_value());
      });
}

// static
std::vector<BufferCollection::BufferVmo> BufferCollection::CreateBufferVmos(
    std::vector<zx::vmo> vmos, zx_vm_option_t map_flags) {
  FX_CHECK(!vmos.empty());

  std::vector<BufferCollection::BufferVmo> buffer_vmos;
  zx_status_t status = ZX_OK;
  std::transform(vmos.begin(), vmos.end(), std::back_inserter(buffer_vmos),
                 [map_flags, &status](zx::vmo& vmo) {
                   auto result = BufferVmo(std::move(vmo), map_flags);
                   if (!result.is_valid()) {
                     status = result.status();
                   }
                   return result;
                 });

  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "CreateBufferVmos: failed to map one or more buffers";
    buffer_vmos.clear();
  }

  return buffer_vmos;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// OutputBufferCollection definitions.

// static
fpromise::promise<std::unique_ptr<OutputBufferCollection>, fuchsia::media2::ConnectionError>
OutputBufferCollection::Create(async::Executor& executor, fuchsia::media2::BufferProvider& provider,
                               zx::eventpair token,
                               const fuchsia::media2::BufferConstraints& constraints,
                               const std::string& name, uint64_t id, zx_vm_option_t map_flags) {
  return GetBuffers(provider, std::move(token), constraints, name, id, map_flags)
      .then(
          [&executor, map_flags](
              fpromise::result<std::vector<zx::vmo>, fuchsia::media2::BufferProviderError>& result)
              -> fpromise::result<std::unique_ptr<OutputBufferCollection>,
                                  fuchsia::media2::ConnectionError> {
            if (result.is_error()) {
              FX_LOGS(ERROR) << "BufferCollection: GetBuffers failed " << result.error();
              return fpromise::error(ToConnectionError(result.error()));
            }

            std::vector<BufferVmo> buffer_vmos = CreateBufferVmos(result.take_value(), map_flags);
            if (buffer_vmos.empty()) {
              return fpromise::error(fuchsia::media2::ConnectionError::FAILED_TO_MAP_BUFFER);
            }

            return fpromise::ok(std::unique_ptr<OutputBufferCollection>(
                new OutputBufferCollection(executor, std::move(buffer_vmos))));
          });
}

OutputBufferCollection::~OutputBufferCollection() {
  FailPendingAllocation();

  // |ClosureContext::FailPendingAllocation| takes the context's mutex and then calls
  // |OutputBufferCollection::FailPendingAllocation|, which takes the mutex on this collection.
  // |ClosureContext::Reset| also takes the context's mutex, so we need to avoid making that call
  // while holding the mutex on this collection.
  std::shared_ptr<ClosureContext> closure_context;
  {
    std::lock_guard<std::mutex> locker(mutex_);
    closure_context = std::move(closure_context_);
  }

  if (closure_context) {
    // Must not be called while holding |mutex_|.
    closure_context->Reset();
  }
}

PayloadBuffer OutputBufferCollection::AllocatePayloadBuffer(size_t size) {
  std::lock_guard<std::mutex> locker(mutex_);
  return AllocatePayloadBufferUnsafe(size);
}

PayloadBuffer OutputBufferCollection::AllocatePayloadBufferBlocking(size_t size) {
  FX_CHECK(executor_.dispatcher() != async_get_default_dispatcher())
      << "AllocatePayloadBufferBlocking must not be called on the output's FIDL thread.";

  auto result = AllocatePayloadBuffer(size);

  if (result) {
    return result;
  }

  // TODO(dalest): debug only...remove outer |sync_completion_wait|.
  if (sync_completion_wait(&completion_, zx::sec(5).get()) == ZX_ERR_TIMED_OUT) {
    FX_LOGS(INFO) << "AllocatePayloadBufferBlocking: blocked for >5 seconds";
    sync_completion_wait(&completion_, ZX_TIME_INFINITE);
  }

  return AllocatePayloadBuffer(size);
}

fpromise::promise<PayloadBuffer> OutputBufferCollection::AllocatePayloadBufferWhenAvailable(
    size_t size) {
  std::lock_guard<std::mutex> locker(mutex_);

  auto result = AllocatePayloadBufferUnsafe(size);

  if (result) {
    return fpromise::make_ok_promise(std::move(result));
  }

  FX_CHECK(!when_available_completer_);
  fpromise::bridge<> bridge;
  when_available_completer_ = std::move(bridge.completer);
  when_available_size_ = size;
  return bridge.consumer.promise()
      .and_then([this, size]() { return fpromise::ok(AllocatePayloadBuffer(size)); })
      .wrap_with(scope_);
}

void OutputBufferCollection::FailPendingAllocation() {
  std::lock_guard<std::mutex> locker(mutex_);

  // Unblocks a pending |AllocatePayloadBufferBlocking|.
  sync_completion_signal(&completion_);

  // Unblocks a pending |AllocatePayloadBufferWhenAvailable|.
  if (when_available_completer_) {
    when_available_completer_.complete_ok();
  }
}

fit::closure OutputBufferCollection::GetFailPendingAllocationClosure() {
  std::lock_guard<std::mutex> locker(mutex_);
  if (!closure_context_) {
    closure_context_ = std::make_shared<ClosureContext>(this);
  }

  return [context = closure_context_]() mutable {
    FX_CHECK(context);
    context->FailPendingAllocation();
  };
}

PayloadBuffer OutputBufferCollection::AllocatePayloadBufferUnsafe(size_t size) {
  FX_CHECK(free_vmo_guess_ < buffer_vmos().size());
  FX_CHECK(size > 0);
  FX_CHECK(size <= buffer_vmos().front().size());

  sync_completion_reset(&completion_);

  size_t vmo_index = free_vmo_guess_;

  while (true) {
    if (!buffer_vmos()[vmo_index].is_allocated()) {
      free_vmo_guess_ = (vmo_index + 1) % buffer_vmos().size();

      buffer_vmos()[vmo_index].Allocate();

      PayloadBuffer result(
          fuchsia::media2::PayloadRange{
              .buffer_id = static_cast<uint32_t>(vmo_index), .offset = 0, .size = size},
          buffer_vmos()[vmo_index].data());

      executor_.schedule_task(result.WhenDestroyed()
                                  .and_then([this, vmo_index]() {
                                    std::lock_guard<std::mutex> locker(mutex_);

                                    buffer_vmos()[vmo_index].Free();

                                    sync_completion_signal(&completion_);

                                    if (when_available_completer_) {
                                      // TODO(dalesat): Will need to check size in some cases.
                                      when_available_completer_.complete_ok();
                                    }
                                  })
                                  .wrap_with(scope_));

      return result;
    }

    vmo_index = (vmo_index + 1) % buffer_vmos().size();
    if (vmo_index == free_vmo_guess_) {
      // Buffer pool exhausted.
      break;
    }
  }

  return PayloadBuffer();
}

bool OutputBufferCollection::BufferAvailableUnsafe(size_t size) {
  FX_CHECK(free_vmo_guess_ < buffer_vmos().size());
  FX_CHECK(size > 0);
  FX_CHECK(size <= buffer_vmos().front().size());

  size_t vmo_index = free_vmo_guess_;

  while (true) {
    if (!buffer_vmos()[vmo_index].is_allocated()) {
      free_vmo_guess_ = vmo_index;
      return true;
    }

    vmo_index = (vmo_index + 1) % buffer_vmos().size();
    if (vmo_index == free_vmo_guess_) {
      break;
    }
  }

  return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// InputBufferCollection::BufferVmo definitions.

// static
fpromise::promise<std::unique_ptr<InputBufferCollection>, fuchsia::media2::ConnectionError>
InputBufferCollection::Create(fuchsia::media2::BufferProvider& provider, zx::eventpair token,
                              const fuchsia::media2::BufferConstraints& constraints,
                              const std::string& name, uint64_t id, zx_vm_option_t map_flags) {
  return GetBuffers(provider, std::move(token), constraints, name, id, map_flags)
      .then(
          [map_flags](
              fpromise::result<std::vector<zx::vmo>, fuchsia::media2::BufferProviderError>& result)
              -> fpromise::result<std::unique_ptr<InputBufferCollection>,
                                  fuchsia::media2::ConnectionError> {
            if (result.is_error()) {
              FX_LOGS(ERROR) << "BufferCollection: GetBuffers failed " << result.error();
              return fpromise::error(ToConnectionError(result.error()));
            }

            std::vector<BufferVmo> buffer_vmos = CreateBufferVmos(result.take_value(), map_flags);
            if (buffer_vmos.empty()) {
              return fpromise::error(fuchsia::media2::ConnectionError::FAILED_TO_MAP_BUFFER);
            }

            return fpromise::ok(std::unique_ptr<InputBufferCollection>(
                new InputBufferCollection(std::move(buffer_vmos))));
          });
}

PayloadBuffer InputBufferCollection::GetPayloadBuffer(
    const fuchsia::media2::PayloadRange& payload_range) {
  if (payload_range.size == 0) {
    return PayloadBuffer();
  }

  std::lock_guard<std::mutex> locker(mutex_);

  if (payload_range.buffer_id >= buffer_vmos().size()) {
    return PayloadBuffer();
  }

  auto& buffer_vmo = buffer_vmos()[payload_range.buffer_id];
  if (payload_range.offset + payload_range.size > buffer_vmo.size()) {
    return PayloadBuffer();
  }

  return PayloadBuffer(payload_range, buffer_vmo.at_offset(payload_range.offset));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// BufferCollection::BufferVmo definitions.

BufferCollection::BufferVmo::BufferVmo(zx::vmo vmo, zx_vm_option_t map_flags)
    : vmo_(std::move(vmo)) {
  status_ = vmo_mapper_.Map(vmo_, 0, 0, map_flags, nullptr);
}

void* BufferCollection::BufferVmo::at_offset(size_t offset) const {
  FX_CHECK(offset < size());
  FX_CHECK(data() != nullptr);
  return reinterpret_cast<uint8_t*>(data()) + offset;
}

}  // namespace fmlib
