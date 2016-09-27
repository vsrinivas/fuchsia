// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_IMPL_OBJECT_IMPL_H_
#define APPS_LEDGER_STORAGE_IMPL_OBJECT_IMPL_H_

#include "apps/ledger/storage/public/object.h"

#include <vector>

namespace storage {

class ObjectImpl : public Object {
 public:
  ObjectImpl(const ObjectId& id, const std::string& file_path);
  ~ObjectImpl() override;

  // Object:
  ObjectId GetId() const override;
  Status GetSize(int64_t* size) override;
  Status GetData(const uint8_t** data) override;

 private:
  const ObjectId id_;
  const std::string file_path_;

  std::string data_;
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_IMPL_OBJECT_IMPL_H_
