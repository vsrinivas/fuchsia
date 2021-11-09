// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/internal/transport.h>

namespace fidl {
namespace internal {

AnyUnownedTransport MakeAnyUnownedTransport(const AnyTransport& transport) {
  return transport.borrow();
}

}  // namespace internal
}  // namespace fidl
