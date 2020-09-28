// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/fakes/device_id_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>

#include <memory>
#include <optional>

#include "src/lib/uuid/uuid.h"

namespace forensics {
namespace fakes {

using namespace fuchsia::feedback;

DeviceIdProvider::DeviceIdProvider() : device_id_(uuid::Generate()) {}

void DeviceIdProvider::GetId(GetIdCallback callback) { callback(device_id_); }

}  // namespace fakes
}  // namespace forensics
