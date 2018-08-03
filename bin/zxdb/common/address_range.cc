// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/common/address_range.h"

#include "lib/fxl/logging.h"

namespace zxdb {

AddressRange::AddressRange(uint64_t begin, uint64_t end)
    : begin_(begin), end_(end) {
  FXL_DCHECK(end_ >= begin_);
}

}  // namespace zxdb
