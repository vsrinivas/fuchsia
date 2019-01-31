// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_IMPL_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_IMPL_H_

#include <memory>

#include <lib/zx/vmar.h>

#include "peridot/bin/ledger/storage/public/object.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/lib/convert/convert.h"
#include "third_party/leveldb/include/leveldb/iterator.h"

namespace storage {

// Object whose data is equal to its id.
class InlinedObject : public Object {
 public:
  explicit InlinedObject(ObjectIdentifier identifier);
  ~InlinedObject() override;

  // Object:
  ObjectIdentifier GetIdentifier() const override;
  Status GetData(fxl::StringView* data) const override;

 private:
  const ObjectIdentifier identifier_;
};

// Object whose data is backed by a string.
class StringObject : public Object {
 public:
  StringObject(ObjectIdentifier identifier, std::string content);
  ~StringObject() override;

  // Object:
  ObjectIdentifier GetIdentifier() const override;
  Status GetData(fxl::StringView* data) const override;

 private:
  const ObjectIdentifier identifier_;
  std::string content_;
};

// Object whose data is backed by a value in LevelDB.
class LevelDBObject : public Object {
 public:
  LevelDBObject(ObjectIdentifier identifier,
                std::unique_ptr<leveldb::Iterator> iterator);
  ~LevelDBObject() override;

  // Object:
  ObjectIdentifier GetIdentifier() const override;
  Status GetData(fxl::StringView* data) const override;

 private:
  const ObjectIdentifier identifier_;
  std::unique_ptr<leveldb::Iterator> iterator_;
};

// Object whose data is backed by a VMO.
class VmoObject : public Object {
 public:
  VmoObject(ObjectIdentifier identifier, fsl::SizedVmo vmo);
  ~VmoObject() override;

  // Object:
  ObjectIdentifier GetIdentifier() const override;
  Status GetData(fxl::StringView* data) const override;
  Status GetVmo(fsl::SizedVmo* vmo) const override;

 private:
  Status Initialize() const;

  mutable bool initialized_ = false;
  const ObjectIdentifier identifier_;
  fsl::SizedVmo vmo_;
  mutable zx::vmar vmar_;
  mutable fxl::StringView data_;
};

}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_OBJECT_IMPL_H_
