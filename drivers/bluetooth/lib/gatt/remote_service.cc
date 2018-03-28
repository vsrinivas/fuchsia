// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_service.h"

#include <lib/async/default.h>

#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"

namespace btlib {
namespace gatt {

using att::Status;
using att::StatusCallback;
using common::HostError;

namespace {

// Executes |task|. Posts it on |dispatcher| if dispatcher is not null.
void RunOrPost(fbl::Function<void()> task, async_t* dispatcher) {
  FXL_DCHECK(task);

  if (!dispatcher) {
    task();
    return;
  }

  async::PostTask(dispatcher, std::move(task));
}

void ReportStatus(Status status,
                  StatusCallback callback,
                  async_t* dispatcher) {
  RunOrPost([status, cb = std::move(callback)] { cb(status); }, dispatcher);
}

}  // namespace

RemoteService::RemoteService(const ServiceData& service_data,
                             fxl::WeakPtr<Client> client,
                             async_t* gatt_dispatcher)
    : service_data_(service_data),
      gatt_dispatcher_(gatt_dispatcher),
      client_(client),
      characteristics_ready_(false),
      shut_down_(false) {
  FXL_DCHECK(client_);
  FXL_DCHECK(gatt_dispatcher_);
}

void RemoteService::ShutDown() {
  std::vector<PendingClosure> rm_handlers;

  {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!alive()) {
      return;
    }

    shut_down_ = true;
    rm_handlers = std::move(rm_handlers_);
  }

  for (auto& handler : rm_handlers) {
    RunOrPost(std::move(handler.callback), handler.dispatcher);
  }
}

bool RemoteService::AddRemovedHandler(fxl::Closure handler,
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
    if (characteristics_ready_) {
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
        self->characteristics_.emplace_back(id, chrc);
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

      self->characteristics_ready_ = status.is_success();

      FXL_DCHECK(!self->pending_discov_reqs_.empty());
      auto pending = std::move(self->pending_discov_reqs_);

      // Skip descriptor discovery and end the procedure as no characteristics
      // were found (or the operation failed).
      for (auto& req : pending) {
        self->ReportCharacteristics(status, std::move(req.callback),
                                    req.dispatcher);
      }
    };

    client_->DiscoverCharacteristics(service_data_.range_start,
                                     service_data_.range_end,
                                     std::move(chrc_cb), std::move(res_cb));
  });
}

bool RemoteService::IsDiscovered() const {
  FXL_DCHECK(IsOnGattThread());
  return characteristics_ready_;
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

    // TODO(armansito): Use the "long write" procedure when supported.
    if (!(chrc->info().properties & Property::kWrite)) {
      FXL_VLOG(1) << "gatt: Characteristic does not support \"write\"";
      ReportStatus(Status(HostError::kNotSupported), std::move(cb), dispatcher);
      return;
    }

    FXL_DCHECK(chrc);

    auto res_cb = [cb = std::move(cb), dispatcher](Status status) mutable {
      ReportStatus(status, std::move(cb), dispatcher);
    };

    client_->WriteRequest(chrc->info().value_handle,
                          common::BufferView(value.data(), value.size()),
                          std::move(res_cb));
  });
}

bool RemoteService::IsOnGattThread() const {
  return async_get_default() == gatt_dispatcher_;
}

HostError RemoteService::GetCharacteristic(IdType id, RemoteCharacteristic** out_char) {
  FXL_DCHECK(IsOnGattThread());
  FXL_DCHECK(out_char);

  if (shut_down_)
    return HostError::kFailed;

  if (!characteristics_ready_)
    return HostError::kNotReady;

  if (id >= characteristics_.size())
    return HostError::kNotFound;

  *out_char = &characteristics_[id];
  return HostError::kNoError;
}

void RemoteService::RunGattTask(fbl::Closure task) {
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

}  // namespace gatt
}  // namespace btlib
