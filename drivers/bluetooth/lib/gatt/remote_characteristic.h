// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/gatt/gatt_defs.h"

namespace btlib {
namespace gatt {

// Used by a RemoteService to represent one of its characteristics. This object
// maintains information about a characteristic (such as its descriptors, known
// permissions, etc) and is responsible for routing notifications to subscribed
// clients.
//
// This class is thread-safe and can be accessed on any thread. It doesn't have
// any external dependencies to keep synchronization simple.
class RemoteCharacteristic final {
 public:
  RemoteCharacteristic(IdType id, const CharacteristicData& info);

  RemoteCharacteristic(RemoteCharacteristic&&) = default;
  RemoteCharacteristic& operator=(RemoteCharacteristic&&) = default;

  // Returns the ID for this characteristic.
  IdType id() const { return id_; }

  // ATT declaration data for this characteristic.
  const CharacteristicData& info() const { return info_; }

  // TODO(armansito): Store descriptors here.
  // TODO(armansito): Add methods for caching notification subscribers and
  // notifying them.

 private:
  IdType id_;
  CharacteristicData info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteCharacteristic);
};

}  // namespace gatt
}  // namespace btlib
