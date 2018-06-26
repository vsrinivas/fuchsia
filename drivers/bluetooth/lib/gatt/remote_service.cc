// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_service.h"

#include <lib/async/default.h>

#include "garnet/drivers/bluetooth/lib/common/run_or_post.h"
#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"

namespace btlib {
namespace gatt {

using att::Status;
using att::StatusCallback;
using common::BufferView;
using common::HostError;
using common::RunOrPost;

namespace {

void ReportStatus(Status status,
                  StatusCallback callback,
                  async_t* dispatcher) {
  RunOrPost([status, cb = std::move(callback)] { cb(status); }, dispatcher);
}

void ReportValue(att::Status status,
                 const common::ByteBuffer& value,
                 RemoteService::ReadValueCallback callback,
                 async_t* dispatcher) {
  if (!dispatcher) {
    callback(status, value);
    return;
  }

  // TODO(armansito): Consider making att::Bearer return the ATT PDU buffer
  // directly which would remove the need for a copy.

  auto buffer = common::NewSlabBuffer(value.size());
  value.Copy(buffer.get());

  async::PostTask(dispatcher,
                  [status, callback = std::move(callback),
                   val = std::move(buffer)] { callback(status, *val); });
}

}  // namespace

// static
constexpr size_t RemoteService::kSentinel;

RemoteService::RemoteService(const ServiceData& service_data,
                             fxl::WeakPtr<Client> client,
                             async_t* gatt_dispatcher)
    : service_data_(service_data),
      gatt_dispatcher_(gatt_dispatcher),
      client_(client),
      remaining_descriptor_requests_(kSentinel),
      shut_down_(false) {
  FXL_DCHECK(client_);
  FXL_DCHECK(gatt_dispatcher_);
}

RemoteService::~RemoteService() {
  std::lock_guard<std::mutex> lock(mtx_);
  FXL_DCHECK(!alive());
}

void RemoteService::ShutDown() {
  FXL_DCHECK(IsOnGattThread());

  std::vector<PendingClosure> rm_handlers;

  {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!alive()) {
      return;
    }

    for (auto& chr : characteristics_) {
      chr.ShutDown();
    }

    shut_down_ = true;
    rm_handlers = std::move(rm_handlers_);
  }

  for (auto& handler : rm_handlers) {
    RunOrPost(std::move(handler.callback), handler.dispatcher);
  }
}

bool RemoteService::AddRemovedHandler(fit::closure handler,
                                      async_t* dispatcher) {
  std::lock_guard<std::mutex> lock(mtx_);

  if (!alive())
    return false;

  rm_handlers_.emplace_back(std::move(handler), dispatcher);
  return true;
}

void RemoteService::DiscoverCharacteristics(CharacteristicCallback callback,
                                            async_t* dispatcher) {
  RunGattTask([this, cb = std::move(callback), dispatcher]() mutable {
    if (shut_down_) {
      ReportCharacteristics(Status(HostError::kFailed), std::move(cb),
                            dispatcher);
      return;
    }

    // Characteristics already discovered. Return success.
    if (HasCharacteristics()) {
      ReportCharacteristics(Status(), std::move(cb), dispatcher);
      return;
    }

    // Queue this request.
    pending_discov_reqs_.emplace_back(std::move(cb), dispatcher);

    // Nothing to do if a write request is already pending.
    if (pending_discov_reqs_.size() > 1u)
      return;

    auto self = fbl::WrapRefPtr(this);
    auto chrc_cb = [self](const CharacteristicData& chrc) {
      if (!self->shut_down_) {
        IdType id = self->characteristics_.size();
        self->characteristics_.emplace_back(self->client_, id, chrc);
      }
    };

    auto res_cb = [self](Status status) mutable {
      if (self->shut_down_) {
        status = Status(HostError::kFailed);
      }

      if (!status) {
        FXL_VLOG(1) << "gatt: characteristic discovery failed "
                    << status.ToString();

        self->characteristics_.clear();
      }

      if (self->characteristics_.empty()) {
        if (status) {
          // This marks that characteristic discovery has completed
          // successfully.
          self->remaining_descriptor_requests_ = 0u;
        }

        // Skip descriptor discovery and end the procedure as no characteristics
        // were found (or the operation failed).
        self->CompleteCharacteristicDiscovery(status);
        return;
      }

      self->StartDescriptorDiscovery();
    };

    client_->DiscoverCharacteristics(service_data_.range_start,
                                     service_data_.range_end,
                                     std::move(chrc_cb), std::move(res_cb));
  });
}

bool RemoteService::IsDiscovered() const {
  // TODO(armansito): Return true only if included services have also been
  // discovered.
  return HasCharacteristics();
}

void RemoteService::ReadCharacteristic(IdType id,
                                       ReadValueCallback cb,
                                       async_t* dispatcher) {
  RunGattTask([this, id, cb = std::move(cb), dispatcher]() mutable {
    RemoteCharacteristic* chrc;
    att::Status status = att::Status(GetCharacteristic(id, &chrc));
    if (!status) {
      ReportValue(status, BufferView(), std::move(cb), dispatcher);
      return;
    }

    // TODO(armansito): Use the "long read" procedure when supported.
    if (!(chrc->info().properties & Property::kRead)) {
      FXL_VLOG(1) << "gatt: Characteristic does not support \"read\"";
      ReportValue(att::Status(HostError::kNotSupported), BufferView(), std::move(cb), dispatcher);
      return;
    }

    FXL_DCHECK(chrc);

    auto res_cb = [cb = std::move(cb), dispatcher](att::Status status,
                                                   const auto& value) mutable {
      ReportValue(status, value, std::move(cb), dispatcher);
    };

    client_->ReadRequest(chrc->info().value_handle, std::move(res_cb));
  });
}

void RemoteService::WriteCharacteristic(IdType id,
                                        std::vector<uint8_t> value,
                                        StatusCallback cb,
                                        async_t* dispatcher) {
  RunGattTask([this, id, value = std::move(value), cb = std::move(cb),
               dispatcher]() mutable {
    RemoteCharacteristic* chrc;
    Status status = Status(GetCharacteristic(id, &chrc));
    if (!status) {
      ReportStatus(status, std::move(cb), dispatcher);
      return;
    }

    FXL_DCHECK(chrc);

    // TODO(armansito): Use the "long write" procedure when supported.
    if (!(chrc->info().properties & Property::kWrite)) {
      FXL_VLOG(1) << "gatt: Characteristic does not support \"write\"";
      ReportStatus(Status(HostError::kNotSupported), std::move(cb), dispatcher);
      return;
    }

    auto res_cb = [cb = std::move(cb), dispatcher](Status status) mutable {
      ReportStatus(status, std::move(cb), dispatcher);
    };

    client_->WriteRequest(chrc->info().value_handle,
                          BufferView(value.data(), value.size()),
                          std::move(res_cb));
  });
}

void RemoteService::WriteCharacteristicWithoutResponse(
    IdType id, std::vector<uint8_t> value) {
  RunGattTask([this, id, value = std::move(value)]() mutable {
    RemoteCharacteristic* chrc;
    Status status = Status(GetCharacteristic(id, &chrc));
    if (!status) {
      return;
    }

    FXL_DCHECK(chrc);

    if (!(chrc->info().properties & Property::kWriteWithoutResponse)) {
      FXL_VLOG(1)
          << "gatt: Characteristic does not support \"write without response\"";
      return;
    }

    client_->WriteWithoutResponse(chrc->info().value_handle,
                                  BufferView(value.data(), value.size()));
  });
}

void RemoteService::EnableNotifications(IdType id, ValueCallback callback,
                                        NotifyStatusCallback status_callback,
                                        async_t* dispatcher) {
  RunGattTask([this, id, cb = std::move(callback),
               status_cb = std::move(status_callback), dispatcher]() mutable {
    RemoteCharacteristic* chrc;
    att::Status status = att::Status(GetCharacteristic(id, &chrc));
    if (!status) {
      RunOrPost([status, cb = std::move(status_cb)] { cb(status, kInvalidId); },
                dispatcher);
      return;
    }

    FXL_DCHECK(chrc);

    chrc->EnableNotifications(std::move(cb), std::move(status_cb), dispatcher);
  });
}

void RemoteService::DisableNotifications(IdType id, IdType handler_id,
                                         StatusCallback status_callback,
                                         async_t* dispatcher) {
  RunGattTask([this, id, handler_id, cb = std::move(status_callback),
               dispatcher]() mutable {
    RemoteCharacteristic* chrc;
    att::Status status = att::Status(GetCharacteristic(id, &chrc));
    if (status && !chrc->DisableNotifications(handler_id)) {
      status = att::Status(HostError::kNotFound);
    }
    ReportStatus(status, std::move(cb), dispatcher);
  });
}

void RemoteService::StartDescriptorDiscovery() {
  FXL_DCHECK(IsOnGattThread());
  FXL_DCHECK(!pending_discov_reqs_.empty());

  FXL_DCHECK(characteristics_.size());
  remaining_descriptor_requests_ = characteristics_.size();

  auto self = fbl::WrapRefPtr(this);

  // Callback called for each characteristic. This may be called in any
  // order since we request the descriptors of all characteristics all at
  // once.
  auto desc_done_callback = [self](att::Status status) {
    // Do nothing if discovery was concluded earlier (which would have cleared
    // the pending discovery requests).
    if (self->pending_discov_reqs_.empty())
      return;

    // Report an error if the service was removed.
    if (self->shut_down_) {
      status = att::Status(HostError::kFailed);
    }

    if (status) {
      self->remaining_descriptor_requests_ -= 1;

      // Defer handling
      if (self->remaining_descriptor_requests_ > 0)
        return;

      // HasCharacteristics() should return true now.
      FXL_DCHECK(self->HasCharacteristics());

      // Fall through and notify clients below.
    } else {
      FXL_DCHECK(!self->HasCharacteristics());
      FXL_VLOG(1) << "gatt: descriptor discovery failed " << status.ToString();
      self->characteristics_.clear();

      // Fall through and notify the clients below.
    }

    self->CompleteCharacteristicDiscovery(status);
  };

  for (size_t i = 0; i < characteristics_.size(); ++i) {
    // We determine the range end handle based on the start handle of the next
    // characteristic. The characteristic ends with the service range if this is
    // the last characteristic.
    att::Handle end_handle;

    if (i == characteristics_.size() - 1) {
      end_handle = service_data_.range_end;
    } else {
      end_handle = characteristics_[i + 1].info().handle - 1;
    }

    FXL_DCHECK(client_);
    characteristics_[i].DiscoverDescriptors(end_handle, desc_done_callback);
  }
}

bool RemoteService::IsOnGattThread() const {
  return async_get_default() == gatt_dispatcher_;
}

HostError RemoteService::GetCharacteristic(IdType id, RemoteCharacteristic** out_char) {
  FXL_DCHECK(IsOnGattThread());
  FXL_DCHECK(out_char);

  if (shut_down_)
    return HostError::kFailed;

  if (!HasCharacteristics())
    return HostError::kNotReady;

  if (id >= characteristics_.size())
    return HostError::kNotFound;

  *out_char = &characteristics_[id];
  return HostError::kNoError;
}

void RemoteService::RunGattTask(fit::closure task) {
  // Capture a reference to this object to guarantee its lifetime.
  RunOrPost(
      [objref = fbl::WrapRefPtr(this), task = std::move(task)] { task(); },
      gatt_dispatcher_);
}

void RemoteService::ReportCharacteristics(Status status,
                                          CharacteristicCallback callback,
                                          async_t* dispatcher) {
  FXL_DCHECK(IsOnGattThread());
  RunOrPost(
      [self = fbl::WrapRefPtr(this), status, cb = std::move(callback)] {
        // We return a const reference to our |characteristics_| field to avoid
        // copying its contents into this lambda.
        //
        // |characteristics_| is not annotated with __TA_GUARDED() since locking
        // |mtx_| can cause a deadlock if |dispatcher| == nullptr. We
        // guarantee the validity of this data by keeping the public
        // interface of Characteristic small and by never modifying
        // |characteristics_| following discovery.
        cb(status, self->characteristics_);
      },
      dispatcher);
}

void RemoteService::CompleteCharacteristicDiscovery(att::Status status) {
  FXL_DCHECK(!pending_discov_reqs_.empty());
  FXL_DCHECK(!status || remaining_descriptor_requests_ == 0u);

  auto pending = std::move(pending_discov_reqs_);
  for (auto& req : pending) {
    ReportCharacteristics(status, std::move(req.callback), req.dispatcher);
  }
}

void RemoteService::HandleNotification(att::Handle value_handle,
                                       const common::ByteBuffer& value) {
  FXL_DCHECK(IsOnGattThread());

  if (shut_down_)
    return;

  // Find the characteristic with the given value handle.
  auto iter = std::lower_bound(characteristics_.begin(), characteristics_.end(),
                               value_handle,
                               [](const auto& chr, att::Handle value_handle) {
                                 return chr.info().value_handle < value_handle;
                               });
  if (iter != characteristics_.end() &&
      iter->info().value_handle == value_handle) {
    iter->HandleNotification(value);
  }
}

}  // namespace gatt
}  // namespace btlib
