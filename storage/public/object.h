// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_PUBLIC_OBJECT_H_
#define APPS_LEDGER_STORAGE_PUBLIC_OBJECT_H_

#include <vector>

#include "apps/ledger/storage/public/types.h"
#include "lib/ftl/macros.h"

namespace storage {

class Object {
 public:
  Object() {}
  virtual ~Object() {}

  // Returns the id of this storage object.
  virtual ObjectId GetId() const = 0;

  // Returns the size in bytes of this object.
  virtual Status GetSize(int64_t* size) = 0;

  // Returns the data of this object.
  virtual Status GetData(const uint8_t** data) = 0;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(Object);
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_PUBLIC_OBJECT_H_
