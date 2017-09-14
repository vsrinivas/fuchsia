// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_OBJECT_IMPL_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_OBJECT_IMPL_H_

#include "apps/ledger/src/storage/public/object.h"
#include "apps/ledger/src/storage/public/page_storage.h"

#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/public/types.h"
#include "zx/vmar.h"
#include "third_party/leveldb/include/leveldb/iterator.h"

#include <memory>

namespace storage {

// Object whose data is equal to its id.
class InlinedObject : public Object {
 public:
  explicit InlinedObject(ObjectId id);
  ~InlinedObject() override;

  // Object:
  ObjectId GetId() const override;
  Status GetData(fxl::StringView* data) const override;

 private:
  const ObjectId id_;
};

// Object whose data is backed by a string.
class StringObject : public Object {
 public:
  StringObject(ObjectId id, std::string content);
  ~StringObject() override;

  // Object:
  ObjectId GetId() const override;
  Status GetData(fxl::StringView* data) const override;

 private:
  const ObjectId id_;
  std::string content_;
};

// Object whose data is backed by a value in LevelDB.
class LevelDBObject : public Object {
 public:
  LevelDBObject(ObjectId id, std::unique_ptr<leveldb::Iterator> iterator);
  ~LevelDBObject() override;

  // Object:
  ObjectId GetId() const override;
  Status GetData(fxl::StringView* data) const override;

 private:
  const ObjectId id_;
  std::unique_ptr<leveldb::Iterator> iterator_;
};

// Object whose data is backed by a VMO.
class VmoObject : public Object {
 public:
  VmoObject(ObjectId id, zx::vmo vmo);
  ~VmoObject() override;

  // Object:
  ObjectId GetId() const override;
  Status GetData(fxl::StringView* data) const override;
  Status GetVmo(zx::vmo* vmo) const override;

 private:
  Status Initialize() const;

  mutable bool initialized_ = false;
  const ObjectId id_;
  zx::vmo vmo_;
  mutable zx::vmar vmar_;
  mutable fxl::StringView data_;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_OBJECT_IMPL_H_
