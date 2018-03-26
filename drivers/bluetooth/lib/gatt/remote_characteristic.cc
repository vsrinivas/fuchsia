// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_characteristic.h"

namespace btlib {
namespace gatt {

RemoteCharacteristic::RemoteCharacteristic(IdType id,
                                           const CharacteristicData& info)
    : id_(id), info_(info) {}

}  // namespace gatt
}  // namespace btlib
