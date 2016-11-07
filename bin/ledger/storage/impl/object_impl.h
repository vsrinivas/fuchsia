// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_OBJECT_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_OBJECT_IMPL_H_

#include "apps/ledger/src/storage/public/object.h"

#include <vector>

namespace storage {

class ObjectImpl : public Object {
 public:
  ObjectImpl(ObjectId&& id, std::string&& file_path);
  ~ObjectImpl() override;

  // Object:
  ObjectId GetId() const override;
  Status GetData(ftl::StringView* data) const override;

 private:
  const ObjectId id_;
  const std::string file_path_;

  mutable std::string data_;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_OBJECT_IMPL_H_
