// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/btree/tree_node.h"

#include <lib/fit/function.h>

#include <algorithm>
#include <utility>

#include "src/ledger/bin/storage/impl/btree/encoding.h"
#include "src/ledger/bin/storage/impl/object_digest.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace storage {
namespace btree {
namespace {

// Extracts |children| and |entries| references to non-inline values into
// |references|.
void ExtractReferences(const std::vector<Entry>& entries,
                       const std::map<size_t, ObjectIdentifier>& children,
                       ObjectReferencesAndPriority* references) {
  for (const auto& [size, identifier] : children) {
    const auto& digest = identifier.object_digest();
    FXL_DCHECK(storage::IsDigestValid(digest));
    if (!GetObjectDigestInfo(digest).is_inlined()) {
      // Node-node references are always treated as eager.
      references->emplace(digest, KeyPriority::EAGER);
    }
  }
  for (const auto& entry : entries) {
    const auto& digest = entry.object_identifier.object_digest();
    if (!GetObjectDigestInfo(digest).is_inlined()) {
      references->emplace(digest, entry.priority);
    }
  }
}

}  // namespace

TreeNode::TreeNode(ObjectIdentifier identifier, uint8_t level, std::vector<Entry> entries,
                   std::map<size_t, ObjectIdentifier> children)
    : identifier_(std::move(identifier)),
      level_(level),
      entries_(std::move(entries)),
      children_(std::move(children)) {
  FXL_DCHECK(children_.empty() || children_.cbegin()->first <= entries_.size());
}

TreeNode::~TreeNode() = default;

void TreeNode::FromIdentifier(
    PageStorage* page_storage, LocatedObjectIdentifier identifier,
    fit::function<void(Status, std::unique_ptr<const TreeNode>)> callback) {
  page_storage->GetObject(std::move(identifier.identifier), std::move(identifier.location),
                          [callback = std::move(callback)](
                              Status status, std::unique_ptr<const Object> object) mutable {
                            if (status != Status::OK) {
                              callback(status, nullptr);
                              return;
                            }
                            std::unique_ptr<const TreeNode> node;
                            status = FromObject(*object, &node);
                            callback(status, std::move(node));
                          });
}

Status TreeNode::FromObject(const Object& object, std::unique_ptr<const TreeNode>* node) {
  fxl::StringView data;
  RETURN_ON_ERROR(object.GetData(&data));
  uint8_t level;
  std::vector<Entry> entries;
  std::map<size_t, ObjectIdentifier> children;
  if (!DecodeNode(data, object.GetIdentifier().factory(), &level, &entries, &children)) {
    return Status::DATA_INTEGRITY_ERROR;
  }
  node->reset(new TreeNode(object.GetIdentifier(), level, std::move(entries), std::move(children)));
  return Status::OK;
}

void TreeNode::Empty(PageStorage* page_storage,
                     fit::function<void(Status, ObjectIdentifier)> callback) {
  FromEntries(page_storage, 0u, std::vector<Entry>(), std::map<size_t, ObjectIdentifier>(),
              std::move(callback));
}

void TreeNode::FromEntries(PageStorage* page_storage, uint8_t level,
                           const std::vector<Entry>& entries,
                           const std::map<size_t, ObjectIdentifier>& children,
                           fit::function<void(Status, ObjectIdentifier)> callback) {
  FXL_DCHECK(children.empty() || children.rbegin()->first <= entries.size());
  ObjectReferencesAndPriority tree_references;
  ExtractReferences(entries, children, &tree_references);
  std::string encoding = EncodeNode(level, entries, children);
  page_storage->AddObjectFromLocal(ObjectType::TREE_NODE,
                                   storage::DataSource::Create(std::move(encoding)),
                                   std::move(tree_references), std::move(callback));
}

int TreeNode::GetKeyCount() const { return entries_.size(); }

Status TreeNode::GetEntry(int index, Entry* entry) const {
  FXL_DCHECK(index >= 0 && index < GetKeyCount());
  *entry = entries_[index];
  return Status::OK;
}

void TreeNode::AppendReferences(ObjectReferencesAndPriority* references) const {
  ExtractReferences(entries_, children_, references);
}

Status TreeNode::FindKeyOrChild(convert::ExtendedStringView key, int* index) const {
  if (key.empty()) {
    *index = 0;
    return !entries_.empty() && entries_[0].key.empty() ? Status::OK : Status::KEY_NOT_FOUND;
  }
  auto it = std::lower_bound(
      entries_.begin(), entries_.end(), key,
      [](const Entry& entry, convert::ExtendedStringView key) { return entry.key < key; });
  if (it == entries_.end()) {
    *index = entries_.size();
    return Status::KEY_NOT_FOUND;
  }
  *index = it - entries_.begin();
  if (it->key == key) {
    return Status::OK;
  }
  return Status::KEY_NOT_FOUND;
}

const ObjectIdentifier& TreeNode::GetIdentifier() const { return identifier_; }

}  // namespace btree
}  // namespace storage
