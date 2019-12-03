// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/public/object.h"

#include <memory>

#include "src/ledger/lib/vmo/strings.h"

namespace storage {
Status Object::GetVmo(ledger::SizedVmo* vmo) const {
  fxl::StringView data;
  RETURN_ON_ERROR(GetData(&data));

  if (!ledger::VmoFromString(data, vmo)) {
    FXL_LOG(WARNING) << "Unable to produce VMO for object " << GetIdentifier();
    return Status::INTERNAL_ERROR;
  }

  return Status::OK;
}
}  // namespace storage
