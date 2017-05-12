// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_INLINED_OBJECT_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_INLINED_OBJECT_IMPL_H_

#include "apps/ledger/src/storage/public/object.h"

#include <vector>

namespace storage {

// Object whose data is equal to its id.
class InlinedObjectImpl : public Object {
 public:
  InlinedObjectImpl(ObjectId id);
  ~InlinedObjectImpl() override;

  // Object:
  ObjectId GetId() const override;
  Status GetData(ftl::StringView* data) const override;

 private:
  const ObjectId id_;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_INLINED_OBJECT_IMPL_H_
