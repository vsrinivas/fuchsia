// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_OBJECT_STORE_H_
#define APPS_LEDGER_STORAGE_OBJECT_STORE_H_

#include <memory>
#include <map>

#include "apps/ledger/storage/public/object.h"
#include "apps/ledger/storage/public/blob.h"

namespace storage {

class TreeNode;

// |ObjectStore| manages all Ledger related storage objects. This includes
// |Blob|s and |TreeNode|s.
class ObjectStore {
 public:
  ObjectStore();
  ~ObjectStore();

  Status AddObject(std::unique_ptr<Object> object);

  Status GetBlob(const ObjectId& id, std::unique_ptr<Blob>* object);
  Status GetTreeNode(const ObjectId& id, std::unique_ptr<TreeNode>* tree_node);

 private:
  // TODO(nellyv): use file system instead and remove this map.
  std::map<std::string, std::unique_ptr<Object>> map_;
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_OBJECT_STORE_H_
