// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/internal/client_details.h>

namespace fidl::internal {

fidl::Status IncomingEventDispatcherBase::DispatchEvent(
    fidl::IncomingMessage& msg, internal::MessageStorageViewBase* storage_view) {
  return ::fidl::Status::UnknownOrdinal();
}

}  // namespace fidl::internal
