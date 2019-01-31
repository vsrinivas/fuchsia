// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_OBJECT_H_
#define PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_OBJECT_H_

#include <vector>

#include <lib/fsl/vmo/sized_vmo.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {

class Object {
 public:
  Object() {}
  virtual ~Object() {}

  // Returns the identifier of this storage object.
  virtual ObjectIdentifier GetIdentifier() const = 0;

  // Returns the data of this object. The returned view is valid as long as this
  // object is not deleted.
  virtual Status GetData(fxl::StringView* data) const = 0;

  // Returns a vmo containing the data.
  virtual Status GetVmo(fsl::SizedVmo* vmo) const;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(Object);
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_PUBLIC_OBJECT_H_
