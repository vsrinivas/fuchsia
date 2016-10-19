// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/store/object_store.h"

#include "apps/ledger/storage/impl/store/tree_node.h"
#include "apps/ledger/storage/public/commit_contents.h"

namespace storage {

ObjectStore::ObjectStore(PageStorage* page_storage)
    : page_storage_(page_storage) {}

ObjectStore::~ObjectStore() {}

Status ObjectStore::AddObject(convert::ExtendedStringView data,
                              std::unique_ptr<const Object>* object) {
  return page_storage_->AddObjectSynchronous(data, object);
}

Status ObjectStore::GetObject(ObjectIdView id,
                              std::unique_ptr<const Object>* object) {
  return page_storage_->GetObjectSynchronous(id, object);
}

}  // namespace storage
