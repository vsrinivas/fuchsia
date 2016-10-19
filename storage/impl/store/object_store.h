// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_OBJECT_STORE_H_
#define APPS_LEDGER_STORAGE_OBJECT_STORE_H_

#include <memory>

#include "apps/ledger/convert/convert.h"
#include "apps/ledger/storage/public/object.h"
#include "apps/ledger/storage/public/page_storage.h"

namespace storage {

class TreeNode;

// |ObjectStore| manages all Ledger related storage objects. This includes
// |Object|s and |TreeNode|s.
class ObjectStore {
 public:
  ObjectStore(PageStorage* page_storage);
  ~ObjectStore();

  Status AddObject(convert::ExtendedStringView data,
                   std::unique_ptr<const Object>* object);

  Status GetObject(ObjectIdView id, std::unique_ptr<const Object>* object);

 private:
  PageStorage* page_storage_;
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_OBJECT_STORE_H_
