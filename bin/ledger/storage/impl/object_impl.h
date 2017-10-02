// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_IMPL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_IMPL_H_

#include "peridot/bin/ledger/storage/public/object.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "third_party/leveldb/include/leveldb/iterator.h"
#include "zx/vmar.h"

#include <memory>

namespace storage {

// Object whose data is equal to its id.
class InlinedObject : public Object {
 public:
  explicit InlinedObject(ObjectDigest digest);
  ~InlinedObject() override;

  // Object:
  ObjectDigest GetDigest() const override;
  Status GetData(fxl::StringView* data) const override;

 private:
  const ObjectDigest digest_;
};

// Object whose data is backed by a string.
class StringObject : public Object {
 public:
  StringObject(ObjectDigest digest, std::string content);
  ~StringObject() override;

  // Object:
  ObjectDigest GetDigest() const override;
  Status GetData(fxl::StringView* data) const override;

 private:
  const ObjectDigest digest_;
  std::string content_;
};

// Object whose data is backed by a value in LevelDB.
class LevelDBObject : public Object {
 public:
  LevelDBObject(ObjectDigest digest,
                std::unique_ptr<leveldb::Iterator> iterator);
  ~LevelDBObject() override;

  // Object:
  ObjectDigest GetDigest() const override;
  Status GetData(fxl::StringView* data) const override;

 private:
  const ObjectDigest digest_;
  std::unique_ptr<leveldb::Iterator> iterator_;
};

// Object whose data is backed by a VMO.
class VmoObject : public Object {
 public:
  VmoObject(ObjectDigest digest, zx::vmo vmo);
  ~VmoObject() override;

  // Object:
  ObjectDigest GetDigest() const override;
  Status GetData(fxl::StringView* data) const override;
  Status GetVmo(zx::vmo* vmo) const override;

 private:
  Status Initialize() const;

  mutable bool initialized_ = false;
  const ObjectDigest digest_;
  zx::vmo vmo_;
  mutable zx::vmar vmar_;
  mutable fxl::StringView data_;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_IMPL_H_
