// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/btree/encoding.h"

#include <lib/fit/function.h>

#include <algorithm>

#include "src/ledger/bin/encryption/primitives/hash.h"
#include "src/ledger/bin/storage/impl/btree/tree_node_generated.h"
#include "src/ledger/bin/storage/impl/data_serialization.h"
#include "src/ledger/bin/storage/impl/object_identifier_encoding.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"

namespace storage {
namespace btree {
namespace {
KeyPriority ToKeyPriority(KeyPriorityStorage priority_storage) {
  switch (priority_storage) {
    case KeyPriorityStorage_EAGER:
      return KeyPriority::EAGER;
    case KeyPriorityStorage_LAZY:
      return KeyPriority::LAZY;
  }
}

bool IsKeyPriorityStorageValid(KeyPriorityStorage priority_storage) {
  return priority_storage == KeyPriorityStorage_EAGER ||
         priority_storage == KeyPriorityStorage_LAZY;
}

KeyPriorityStorage ToKeyPriorityStorage(KeyPriority priority) {
  switch (priority) {
    case KeyPriority::EAGER:
      return KeyPriorityStorage_EAGER;
    case KeyPriority::LAZY:
      return KeyPriorityStorage_LAZY;
  }
}

bool IsTreeNodeEntryValid(const EntryStorage* entry) {
  // TODO(LE-823): For compatibility, we accept entries without entry_id.
  return entry && entry->key() && IsObjectIdentifierStorageValid(entry->object_id()) &&
         IsKeyPriorityStorageValid(entry->priority());
}

Entry ToEntry(const EntryStorage* entry_storage, ObjectIdentifierFactory* factory) {
  FXL_DCHECK(IsTreeNodeEntryValid(entry_storage));
  EntryId entry_id = entry_storage->entry_id() ? convert::ToString(entry_storage->entry_id()) : "";
  Entry entry{convert::ToString(entry_storage->key()),
              ToObjectIdentifier(entry_storage->object_id(), factory),
              ToKeyPriority(entry_storage->priority()), entry_id};
  SetEntryIdIfMissing(&entry);
  return entry;
}
}  // namespace

void SetEntryIdIfMissing(Entry* entry) {
  if (!entry->entry_id.empty()) {
    // The EntryId was already read from the node.
    return;
  }
  // We just read a tree node without |entry_id|s. For compatibility, we accept such entries.
  // Compute and add the entry here.
  // TODO(LE-823): Remove compatibility.
  const ObjectIdentifier& object_id = entry->object_identifier;

  entry->entry_id = encryption::SHA256WithLengthHash(SafeConcatenation(
      {entry->key, absl::StrCat(object_id.key_index()), object_id.object_digest().Serialize(),
       entry->priority == KeyPriority::EAGER ? "E" : "L"}));
}

bool CheckValidTreeNodeSerialization(fxl::StringView data) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(data.data()), data.size());
  if (!VerifyTreeNodeStorageBuffer(verifier)) {
    return false;
  }

  const TreeNodeStorage* tree_node =
      GetTreeNodeStorage(reinterpret_cast<const unsigned char*>(data.data()));

  if (!tree_node->children() || !tree_node->entries()) {
    return false;
  }

  if (tree_node->children()->size() > tree_node->entries()->size() + 1) {
    return false;
  }

  // Check that the indexes are strictly increasing.
  size_t expected_min_next_index = 0;
  for (const auto* child : *(tree_node->children())) {
    if (child->index() < expected_min_next_index) {
      return false;
    }
    if (!IsObjectIdentifierStorageValid(child->object_id())) {
      return false;
    }
    expected_min_next_index = child->index() + 1;
  }

  // Check that all index are in [0, tree_node->entries()->size()]
  if (expected_min_next_index > tree_node->entries()->size() + 1) {
    return false;
  }

  // Check that the first entry is correct.
  if (tree_node->entries()->size() > 0) {
    const auto* entry = *tree_node->entries()->begin();
    if (!IsTreeNodeEntryValid(entry)) {
      return false;
    }
  }
  // Check that the rest of the entries are correct and that the keys are in
  // order.
  auto it = std::adjacent_find(tree_node->entries()->begin(), tree_node->entries()->end(),
                               [](const auto* e1, const auto* e2) {
                                 FXL_DCHECK(IsTreeNodeEntryValid(e1));
                                 if (!IsTreeNodeEntryValid(e2)) {
                                   return true;
                                 }
                                 return convert::ExtendedStringView(e1->key()) >=
                                        convert::ExtendedStringView(e2->key());
                               });
  return it == tree_node->entries()->end();
}

std::string EncodeNode(uint8_t level, const std::vector<Entry>& entries,
                       const std::map<size_t, ObjectIdentifier>& children) {
  flatbuffers::FlatBufferBuilder builder;

  auto entries_offsets = builder.CreateVector(
      entries.size(), static_cast<std::function<flatbuffers::Offset<EntryStorage>(size_t)>>(
                          [&builder, &entries](size_t i) {
                            const auto& entry = entries[i];
                            FXL_DCHECK(!entry.entry_id.empty());
                            return CreateEntryStorage(
                                builder, convert::ToFlatBufferVector(&builder, entry.key),
                                ToObjectIdentifierStorage(&builder, entry.object_identifier),
                                ToKeyPriorityStorage(entry.priority),
                                convert::ToFlatBufferVector(&builder, entry.entry_id));
                          }));

  size_t children_count = children.size();
  auto current_children = children.begin();
  auto children_offsets = builder.CreateVector(
      children_count, static_cast<std::function<flatbuffers::Offset<ChildStorage>(size_t)>>(
                          [&builder, &current_children](size_t i) {
                            size_t index = current_children->first;
                            auto object_identifier_storage =
                                ToObjectIdentifierStorage(&builder, current_children->second);
                            ++current_children;
                            return CreateChildStorage(builder, index, object_identifier_storage);
                          }));

  builder.Finish(CreateTreeNodeStorage(builder, entries_offsets, children_offsets, level));

  return convert::ToString(builder);
}

bool DecodeNode(fxl::StringView data, ObjectIdentifierFactory* factory, uint8_t* level,
                std::vector<Entry>* res_entries, std::map<size_t, ObjectIdentifier>* res_children) {
  if (!CheckValidTreeNodeSerialization(data)) {
    return false;
  }

  const TreeNodeStorage* tree_node =
      GetTreeNodeStorage(reinterpret_cast<const unsigned char*>(data.data()));

  *level = tree_node->level();
  res_entries->clear();
  res_entries->reserve(tree_node->entries()->size());
  for (const auto* entry_storage : *(tree_node->entries())) {
    res_entries->push_back(ToEntry(entry_storage, factory));
  }
  res_children->clear();
  for (const auto* child_storage : *(tree_node->children())) {
    (*res_children)[child_storage->index()] =
        ToObjectIdentifier(child_storage->object_id(), factory);
  }

  return true;
}
}  // namespace btree
}  // namespace storage
