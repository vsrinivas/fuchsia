// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/public/object.h"

#include <lib/fsl/vmo/strings.h>

namespace storage {

Status Object::GetVmo(fsl::SizedVmo* vmo) const {
  fxl::StringView data;
  Status status = GetData(&data);
  if (status != Status::OK) {
    return status;
  }

  if (!fsl::VmoFromString(data, vmo)) {
    FXL_LOG(WARNING) << "Unable to produce VMO for data.";
    return Status::INTERNAL_IO_ERROR;
  }

  return Status::OK;
}

}  // namespace storage
