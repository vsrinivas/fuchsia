// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_characteristic.h"

#include "client.h"

namespace btlib {
namespace gatt {

using common::HostError;

RemoteCharacteristic::Descriptor::Descriptor(IdType id,
                                             const DescriptorData& info)
    : id_(id), info_(info) {}

RemoteCharacteristic::RemoteCharacteristic(IdType id,
                                           const CharacteristicData& info)
    : id_(id),
      info_(info),
      discovery_error_(false),
      shut_down_(false),
      ccc_handle_(att::kInvalidHandle),
      weak_ptr_factory_(this) {
  // See comments about "ID scheme" in remote_characteristics.h
  FXL_DCHECK(id_ <= std::numeric_limits<uint16_t>::max());
}

RemoteCharacteristic::RemoteCharacteristic(RemoteCharacteristic&& other)
    : id_(other.id_),
      info_(other.info_),
      discovery_error_(other.discovery_error_),
      shut_down_(other.shut_down_.load()),
      ccc_handle_(other.ccc_handle_),
      weak_ptr_factory_(this) {
  other.weak_ptr_factory_.InvalidateWeakPtrs();
}

void RemoteCharacteristic::ShutDown() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  // Make sure that all weak pointers are invalidated on the GATT thread.
  weak_ptr_factory_.InvalidateWeakPtrs();
  shut_down_ = true;
}

void RemoteCharacteristic::DiscoverDescriptors(Client* client,
                                               att::Handle range_end,
                                               StatusCallback callback) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(client);
  FXL_DCHECK(callback);
  FXL_DCHECK(!shut_down_);
  FXL_DCHECK(range_end >= info().value_handle);

  discovery_error_ = false;
  descriptors_.clear();

  if (info().value_handle == range_end) {
    callback(att::Status());
    return;
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto desc_cb = [self](const DescriptorData& desc) {
    if (!self)
      return;

    FXL_DCHECK(self->thread_checker_.IsCreationThreadCurrent());
    if (self->discovery_error_)
      return;

    if (desc.type == types::kClientCharacteristicConfig) {
      if (self->ccc_handle_ != att::kInvalidHandle) {
        FXL_VLOG(1) << "gatt: characteristic has more than one CCC descriptor!";
        self->discovery_error_ = true;
        return;
      }
      self->ccc_handle_ = desc.handle;
    }

    // See comments about "ID scheme" in remote_characteristics.h
    FXL_DCHECK(self->descriptors_.size() <=
               std::numeric_limits<uint16_t>::max());
    IdType id = (self->id_ << 16) | self->descriptors_.size();
    self->descriptors_.push_back(Descriptor(id, desc));
  };

  auto status_cb = [self, cb = std::move(callback)](att::Status status) {
    if (!self) {
      cb(att::Status(HostError::kFailed));
      return;
    }

    FXL_DCHECK(self->thread_checker_.IsCreationThreadCurrent());

    if (self->discovery_error_) {
      status = att::Status(HostError::kFailed);
    }

    if (!status) {
      self->descriptors_.clear();
    }
    cb(status);
  };

  client->DiscoverDescriptors(info().value_handle + 1, range_end,
                              std::move(desc_cb), std::move(status_cb));
}

}  // namespace gatt
}  // namespace btlib
