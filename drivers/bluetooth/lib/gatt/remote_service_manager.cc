// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_service_manager.h"

#include "garnet/drivers/bluetooth/lib/gatt/remote_service.h"

#include "lib/fxl/logging.h"

namespace btlib {

using common::HostError;

namespace gatt {
namespace internal {

RemoteServiceManager::ServiceListRequest::ServiceListRequest(
    ServiceListCallback callback,
    const std::vector<common::UUID>& uuids)
    : callback_(std::move(callback)), uuids_(uuids) {
  FXL_DCHECK(callback_);
}

void RemoteServiceManager::ServiceListRequest::Complete(
    att::Status status,
    const ServiceMap& services) {
  ServiceList result;

  if (!status || services.empty()) {
    callback_(status, std::move(result));
    return;
  }

  for (const auto& iter : services) {
    auto& svc = iter.second;
    auto pred = [&svc](const common::UUID& uuid) {
      return svc->uuid() == uuid;
    };
    if (uuids_.empty() ||
        std::find_if(uuids_.begin(), uuids_.end(), pred) != uuids_.end()) {
      result.push_back(iter.second);
    }
  }

  callback_(status, std::move(result));
}

RemoteServiceManager::RemoteServiceManager(std::unique_ptr<Client> client,
                                           async_dispatcher_t* gatt_dispatcher)
    : gatt_dispatcher_(gatt_dispatcher),
      client_(std::move(client)),
      initialized_(false),
      weak_ptr_factory_(this) {
  FXL_DCHECK(gatt_dispatcher_);
  FXL_DCHECK(client_);

  client_->SetNotificationHandler(
      fit::bind_member(this, &RemoteServiceManager::OnNotification));
}

RemoteServiceManager::~RemoteServiceManager() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  client_->SetNotificationHandler({});
  ClearServices();

  // Resolve all pending requests with an error.
  att::Status status(common::HostError::kFailed);

  auto pending = std::move(pending_);
  while (!pending.empty()) {
    // This copies |services|.
    pending.front().Complete(status, services_);
    pending.pop();
  }
}

void RemoteServiceManager::Initialize(att::StatusCallback cb) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  auto self = weak_ptr_factory_.GetWeakPtr();

  auto init_cb = [self, user_init_cb = std::move(cb)](att::Status status) {
    if (!self)
      return;

    self->initialized_ = true;

    user_init_cb(status);

    // Notify pending ListService() requests.
    while (!self->pending_.empty()) {
      self->pending_.front().Complete(status, self->services_);
      self->pending_.pop();
    }
  };

  // Start out with the MTU exchange.
  client_->ExchangeMTU([self, init_cb = std::move(init_cb)](
                           att::Status status, uint16_t mtu) mutable {
    if (!self) {
      init_cb(att::Status(HostError::kFailed));
      return;
    }

    if (!status) {
      FXL_VLOG(1) << "gatt: MTU exchange failed: " << status.ToString();
      init_cb(status);
      return;
    }

    auto svc_cb = [self](const ServiceData& service_data) {
      if (!self)
        return;

      auto svc = fbl::AdoptRef(new RemoteService(
          service_data, self->client_->AsWeakPtr(), self->gatt_dispatcher_));
      if (!svc) {
        FXL_VLOG(1) << "gatt: Failed to allocate RemoteService";
        return;
      }

      self->services_[svc->handle()] = svc;
    };

    auto status_cb = [self, init_cb = std::move(init_cb)](att::Status status) {
      if (!self) {
        init_cb(att::Status(HostError::kFailed));
        return;
      }

      // Service discovery support is mandatory for servers (v5.0, Vol 3,
      // Part G, 4.2).
      if (!status) {
        FXL_VLOG(1) << "gatt: Failed to discover services";

        // Clear services that were buffered so far.
        self->ClearServices();
      } else if (self->svc_watcher_) {
        // Notify all discovered services here.
        for (auto& iter : self->services_) {
          self->svc_watcher_(iter.second);
        }
      }

      init_cb(status);
    };

    self->client_->DiscoverPrimaryServices(std::move(svc_cb),
                                           std::move(status_cb));
  });
}

void RemoteServiceManager::ListServices(const std::vector<common::UUID>& uuids,
                                        ServiceListCallback callback) {
  ServiceListRequest request(std::move(callback), uuids);
  if (initialized_) {
    request.Complete(att::Status(), services_);
  } else {
    pending_.push(std::move(request));
  }
}

fbl::RefPtr<RemoteService> RemoteServiceManager::FindService(
    att::Handle handle) {
  auto iter = services_.find(handle);
  return iter == services_.end() ? nullptr : iter->second;
}

void RemoteServiceManager::ClearServices() {
  auto services = std::move(services_);
  for (auto& iter : services) {
    iter.second->ShutDown();
  }
}

void RemoteServiceManager::OnNotification(bool, att::Handle value_handle,
                                          const common::ByteBuffer& value) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  // Find the service that |value_handle| belongs to.
  auto iter = services_.upper_bound(value_handle);
  if (iter != services_.begin()) --iter;

  // If |value_handle| is within the previous service then we found it.
  auto& svc = iter->second;
  FXL_DCHECK(value_handle >= svc->handle());

  if (svc->info().range_end >= value_handle) {
    svc->HandleNotification(value_handle, value);
  }
}

}  // namespace internal
}  // namespace gatt
}  // namespace btlib
