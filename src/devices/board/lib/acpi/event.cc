// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/event.h"

#include <lib/ddk/debug.h>

#include "src/devices/board/lib/acpi/device.h"

namespace acpi {

void NotifyEventHandler::on_fidl_error(fidl::UnbindInfo error) { device_->RemoveNotifyHandler(); }

}  // namespace acpi
