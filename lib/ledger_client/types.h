// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_LEDGER_CLIENT_TYPES_H_
#define PERIDOT_LIB_LEDGER_CLIENT_TYPES_H_

#include "lib/fidl/cpp/bindings/array.h"

namespace modular {

// TODO(mesch): Not sure how useful that is. These types can lie.
typedef fidl::Array<uint8_t> LedgerPageId;
typedef fidl::Array<uint8_t> LedgerPageKey;

}  // namespace modular

#endif  // PERIDOT_LIB_LEDGER_CLIENT_TYPES_H_
