// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/impl/btree/builder.h"

#include <lib/fit/function.h>

#include "lib/callback/waiter.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/ledger/storage/impl/btree/internal_helper.h"
#include "peridot/bin/ledger/storage/impl/btree/synchronous_storage.h"
#include "peridot/bin/ledger/storage/impl/object_digest.h"
#include "third_party/murmurhash/murmurhash.h"

namespace storage {
namespace btree {
namespace {

constexpr uint32_t kMurmurHashSeed = 0xbeef;

using HashResultType = decltype(murmurhash(nullptr, 0, 0));
using HashSliceType = uint8_t;

union Hash {
  HashResultType hash;
  HashSliceType slices[sizeof(HashResultType) / sizeof(HashSliceType)];
};

static_assert(sizeof(Hash::slices) == sizeof(Hash::hash),
              "Hash size is incorrect.");
static_assert(sizeof(HashSliceType) < std::numeric_limits<uint8_t>::max(),
              "Hash size is too big.");

Hash FastHash(convert::ExtendedStringView value) {
  return {.hash = murmurhash(value.data(), value.size(), kMurmurHashSeed)};
}

uint8_t GetNodeLevel(convert::ExtendedStringView key) {
  // Compute the level of a key by computing the hash of the key.
  // A key is at level k if the first k bytes of the hash of |key| are 0s. This
  // constructs a tree with an expected node size of |255|.
  Hash hash = FastHash(key);
  for (size_t l = 0; l < sizeof(Hash); ++l) {
    if (hash.slices[l]) {
      return l;
    }
  }
  return std::numeric_limits<uint8_t>::max();
}

constexpr NodeLevelCalculator kDefaultNodeLevelCalculator = {&GetNodeLevel};

// Base class for tree nodes during construction. To apply mutations on a tree
// node, one starts by creating an instance of NodeBuilder from the id of an
// existing tree node, then applies mutation on it.  Once all mutations are
// applied, a call to Build will build a TreeNode in the storage.
class NodeBuilder {
 public:
  // Creates a NodeBuilder from the id of a tree node.
  static Status FromIdentifier(SynchronousStorage* page_storage,
                               ObjectIdentifier object_identifier,
                               NodeBuilder* node_builder);

  // Creates a null builder.
  NodeBuilder() { FXL_DCHECK(Validate()); }

  NodeBuilder(NodeBuilder&&) = default;

  NodeBuilder& operator=(NodeBuilder&&) = default;

  // Returns whether the builder is null.
  explicit operator bool() const { return type_ != BuilderType::NULL_NODE; }

  // Apply the given mutation on |node_builder|.
  Status Apply(const NodeLevelCalculator* node_level_calculator,
               SynchronousStorage* page_storage, EntryChange change,
               bool* did_mutate);

  // Build the tree node represented by the builder |node_builder| in the
  // storage.
  Status Build(SynchronousStorage* page_storage,
               ObjectIdentifier* object_identifier,
               std::set<ObjectIdentifier>* new_identifiers);

 private:
  enum class BuilderType {
    EXISTING_NODE,
    NEW_NODE,
    NULL_NODE,
  };

  static NodeBuilder CreateExistingBuilder(uint8_t level,
                                           ObjectIdentifier object_identifier) {
    return NodeBuilder(BuilderType::EXISTING_NODE, level,
                       std::move(object_identifier), {}, {});
  }

  static NodeBuilder CreateNewBuilder(uint8_t level, std::vector<Entry> entries,
                                      std::vector<NodeBuilder> children) {
    if (entries.empty() && !children[0]) {
      return NodeBuilder();
    }
    return NodeBuilder(BuilderType::NEW_NODE, level, {}, std::move(entries),
                       std::move(children));
  }

  NodeBuilder(BuilderType type, uint8_t level,
              ObjectIdentifier object_identifier, std::vector<Entry> entries,
              std::vector<NodeBuilder> children)
      : type_(type),
        level_(level),
        object_identifier_(std::move(object_identifier)),
        entries_(std::move(entries)),
        children_(std::move(children)) {
    FXL_DCHECK(Validate());
  }

  // Ensures that the entries and children of this builder are computed.
  Status ComputeContent(SynchronousStorage* page_storage);

  // Delete the value with the given |key| from the builder. |key_level| must be
  // greater or equal then the node level.
  Status Delete(SynchronousStorage* page_storage, uint8_t key_level,
                std::string key, bool* did_mutate);

  // Update the tree by adding |entry| (or modifying the value associated to
  // |entry.key| with |entry.value| if |key| is already in the tree).
  // |change_level| must be greater or equal than the node level.
  Status Update(SynchronousStorage* page_storage, uint8_t change_level,
                Entry entry, bool* did_mutate);

  // Split the current tree in 2 according to |key|. This method expects that
  // |key| is not in the tree. After the call, the left tree will be in the
  // current builder, and the right tree in |right|.
  Status Split(SynchronousStorage* page_storage, std::string key,
               NodeBuilder* right);

  // Merge this tree with |other|. This expects all elements of |other| to be
  // greather than elements in |this|.
  Status Merge(SynchronousStorage* page_storage, NodeBuilder other);

  // Extract the entries and children from a TreeNode.
  static void ExtractContent(const TreeNode& node, std::vector<Entry>* entries,
                             std::vector<NodeBuilder>* children);

  // Validate that the content of this builder follows the expected constraints.
  bool Validate() {
    if (type_ == BuilderType::NULL_NODE &&
        !object_identifier_.object_digest.empty()) {
      return false;
    }
    if (type_ == BuilderType::EXISTING_NODE &&
        object_identifier_.object_digest.empty()) {
      return false;
    }
    if (type_ == BuilderType::NEW_NODE && children_.empty()) {
      return false;
    }
    if ((!children_.empty() || !entries_.empty()) &&
        children_.size() != entries_.size() + 1) {
      return false;
    }
    if (type_ == BuilderType::NEW_NODE && entries_.empty() && !children_[0]) {
      return false;
    }
    return true;
  }

  // Add needed parent to |node| to produce a new tree of level |target_level|.
  NodeBuilder& ToLevel(uint8_t target_level) {
    if (!*this) {
      return *this;
    }
    FXL_DCHECK(target_level >= level_);
    while (level_ < target_level) {
      std::vector<NodeBuilder> children;
      children.push_back(std::move(*this));
      *this =
          NodeBuilder::CreateNewBuilder(level_ + 1, {}, std::move(children));
    }
    return *this;
  }

  // Collect the maximal set of nodes in the tree root at this builder than can
  // currently be built. A node can be built if and only if all its children are
  // already built. Add the buildable nodes to |output|. Return if at least a
  // node has been added to |output|.
  bool CollectNodesToBuild(std::vector<NodeBuilder*>* output) {
    if (type_ != BuilderType::NEW_NODE) {
      return false;
    }
    bool found_nodes_to_build = false;
    for (auto& child : children_) {
      found_nodes_to_build |= child.CollectNodesToBuild(output);
    }
    if (!found_nodes_to_build) {
      output->push_back(this);
    }
    return true;
  }

  BuilderType type_ = BuilderType::NULL_NODE;
  uint8_t level_;
  ObjectIdentifier object_identifier_;
  std::vector<Entry> entries_;
  std::vector<NodeBuilder> children_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NodeBuilder);
};

Status NodeBuilder::FromIdentifier(SynchronousStorage* page_storage,
                                   ObjectIdentifier object_identifier,
                                   NodeBuilder* node_builder) {
  std::unique_ptr<const TreeNode> node;
  RETURN_ON_ERROR(
      page_storage->TreeNodeFromIdentifier(object_identifier, &node));
  FXL_DCHECK(node);

  std::vector<Entry> entries;
  std::vector<NodeBuilder> children;
  ExtractContent(*node, &entries, &children);
  *node_builder = NodeBuilder(BuilderType::EXISTING_NODE, node->level(),
                              std::move(object_identifier), std::move(entries),
                              std::move(children));
  return Status::OK;
}

Status NodeBuilder::Apply(const NodeLevelCalculator* node_level_calculator,
                          SynchronousStorage* page_storage, EntryChange change,
                          bool* did_mutate) {
  if (!*this) {
    // If the change is a deletion, and the tree is null, the result is still
    // null.
    if (change.deleted) {
      *did_mutate = false;
      return Status::OK;
    }

    // Otherwise, create a node of the right level that contains only entry.
    std::vector<Entry> entries;
    uint8_t level = node_level_calculator->GetNodeLevel(change.entry.key);
    entries.push_back(std::move(change.entry));
    *this = NodeBuilder::CreateNewBuilder(level, std::move(entries),
                                          std::vector<NodeBuilder>(2));
    *did_mutate = true;
    return Status::OK;
  }

  uint8_t change_level = node_level_calculator->GetNodeLevel(change.entry.key);

  if (change_level < level_) {
    // The change is at a lower level than the current node. Find the children
    // to apply the change, transform it and reconstruct the new node.
    RETURN_ON_ERROR(ComputeContent(page_storage));

    size_t index = GetEntryOrChildIndex(entries_, change.entry.key);
    FXL_DCHECK(index == entries_.size() ||
               entries_[index].key != change.entry.key);

    NodeBuilder& child = children_[index];
    RETURN_ON_ERROR(child.Apply(node_level_calculator, page_storage,
                                std::move(change), did_mutate));
    // Suppress warning that |did_mutate| might not be initialized.
    if (!*did_mutate) {  // NOLINT
      return Status::OK;
    }

    type_ = BuilderType::NEW_NODE;
    if (entries_.empty() && !children_[0]) {
      *this = NodeBuilder();
    } else {
      child.ToLevel(level_ - 1);
    }
    return Status::OK;
  }

  if (change.deleted) {
    return Delete(page_storage, change_level, std::move(change.entry.key),
                  did_mutate);
  }

  return Update(page_storage, change_level, std::move(change.entry),
                did_mutate);
}

Status NodeBuilder::Build(SynchronousStorage* page_storage,
                          ObjectIdentifier* object_identifier,
                          std::set<ObjectIdentifier>* new_identifiers) {
  if (!*this) {
    RETURN_ON_ERROR(
        page_storage->TreeNodeFromEntries(0, {}, {}, &object_identifier_));

    *object_identifier = object_identifier_;
    new_identifiers->insert(object_identifier_);
    type_ = BuilderType::EXISTING_NODE;
    return Status::OK;
  }
  if (type_ == BuilderType::EXISTING_NODE) {
    *object_identifier = object_identifier_;
    return Status::OK;
  }

  std::vector<NodeBuilder*> to_build;
  while (CollectNodesToBuild(&to_build)) {
    auto waiter =
        fxl::MakeRefCounted<callback::StatusWaiter<Status>>(Status::OK);
    for (NodeBuilder* child : to_build) {
      std::map<size_t, ObjectIdentifier> children;
      for (size_t index = 0; index < child->children_.size(); ++index) {
        const auto& sub_child = child->children_[index];
        FXL_DCHECK(sub_child.type_ != BuilderType::NEW_NODE);
        if (sub_child) {
          children[index] = sub_child.object_identifier_;
        }
      }
      TreeNode::FromEntries(
          page_storage->page_storage(), child->level_, child->entries_,
          children,
          [new_identifiers, child, callback = waiter->NewCallback()](
              Status status, ObjectIdentifier object_identifier) {
            if (status == Status::OK) {
              child->type_ = BuilderType::EXISTING_NODE;
              child->object_identifier_ = object_identifier;
              new_identifiers->insert(child->object_identifier_);
            }
            callback(status);
          });
    }
    Status status;
    if (coroutine::SyncCall(page_storage->handler(),
                            [&waiter](fit::function<void(Status)> callback) {
                              waiter->Finalize(std::move(callback));
                            },
                            &status) ==
        coroutine::ContinuationStatus::INTERRUPTED) {
      return Status::INTERRUPTED;
    }
    if (status != Status::OK) {
      return status;
    }
    to_build.clear();
  }

  FXL_DCHECK(type_ == BuilderType::EXISTING_NODE);
  *object_identifier = object_identifier_;

  return Status::OK;
}

Status NodeBuilder::ComputeContent(SynchronousStorage* page_storage) {
  FXL_DCHECK(*this);

  if (!children_.empty()) {
    return Status::OK;
  }

  FXL_DCHECK(type_ == BuilderType::EXISTING_NODE);

  std::unique_ptr<const TreeNode> node;
  RETURN_ON_ERROR(
      page_storage->TreeNodeFromIdentifier(object_identifier_, &node));
  FXL_DCHECK(node);

  ExtractContent(*node, &entries_, &children_);
  return Status::OK;
}

Status NodeBuilder::Delete(SynchronousStorage* page_storage, uint8_t key_level,
                           std::string key, bool* did_mutate) {
  FXL_DCHECK(*this);
  FXL_DCHECK(key_level >= level_);

  // If the change is at a higher level than this node, then it is a no-op.
  if (key_level > level_) {
    return Status::OK;
  }

  RETURN_ON_ERROR(ComputeContent(page_storage));

  size_t index = GetEntryOrChildIndex(entries_, key);

  // The key must be in the current node if it is in the tree.
  if (index == entries_.size() || entries_[index].key != key) {
    // The key is not found. Return the current node.
    *did_mutate = false;
    return Status::OK;
  }

  // Element at |index| must be removed.
  RETURN_ON_ERROR(
      children_[index].Merge(page_storage, std::move(children_[index + 1])));

  type_ = BuilderType::NEW_NODE;
  *did_mutate = true;
  entries_.erase(entries_.begin() + index);
  children_.erase(children_.begin() + index + 1);

  // Check if this makes this node null.
  if (entries_.empty() && !children_[0]) {
    *this = NodeBuilder();
  }

  return Status::OK;
}

Status NodeBuilder::Update(SynchronousStorage* page_storage,
                           uint8_t change_level, Entry entry,
                           bool* did_mutate) {
  FXL_DCHECK(*this);
  FXL_DCHECK(change_level >= level_);

  // If the change is at a greater level than the node level, the current node
  // must be splitted in 2, and the new root is composed of the new entry and
  // the 2 children.
  if (change_level > level_) {
    NodeBuilder right;
    RETURN_ON_ERROR(Split(page_storage, entry.key, &right));

    std::vector<Entry> entries;
    entries.push_back(std::move(entry));
    std::vector<NodeBuilder> children;
    children.push_back(std::move(this->ToLevel(change_level - 1)));
    children.push_back(std::move(right.ToLevel(change_level - 1)));
    *this = NodeBuilder::CreateNewBuilder(change_level, std::move(entries),
                                          std::move(children));
    *did_mutate = true;
    return Status::OK;
  }

  RETURN_ON_ERROR(ComputeContent(page_storage));

  // The change is at the current level. The entries must be splitted
  // according to the key of the change.
  size_t split_index = GetEntryOrChildIndex(entries_, entry.key);

  if (split_index < entries_.size() && entries_[split_index].key == entry.key) {
    // The key is already present in the current entries of the node. The
    // value must be replaced.

    // Entries are identical, the change is a no-op.
    if (entries_[split_index].object_identifier == entry.object_identifier &&
        entries_[split_index].priority == entry.priority) {
      *did_mutate = false;
      return Status::OK;
    }

    type_ = BuilderType::NEW_NODE;
    *did_mutate = true;
    entries_[split_index].object_identifier =
        std::move(entry.object_identifier);
    entries_[split_index].priority = entry.priority;
    return Status::OK;
  }

  type_ = BuilderType::NEW_NODE;
  *did_mutate = true;

  // Split the child that encompass |entry.key|.
  NodeBuilder right;
  RETURN_ON_ERROR(
      children_[split_index].Split(page_storage, entry.key, &right));

  // Add |entry| to the list of entries of the result node.
  entries_.insert(entries_.begin() + split_index, std::move(entry));
  // Append the right node to the list of children.
  children_.insert(children_.begin() + split_index + 1, std::move(right));
  return Status::OK;
}

Status NodeBuilder::Split(SynchronousStorage* page_storage, std::string key,
                          NodeBuilder* right) {
  if (!*this) {
    *right = NodeBuilder();
    return Status::OK;
  }

  RETURN_ON_ERROR(ComputeContent(page_storage));

  // Find the index at which to split.
  size_t split_index = GetEntryOrChildIndex(entries_, key);

  // Ensure that |key| is not part of the entries.
  FXL_DCHECK(split_index == entries_.size() ||
             entries_[split_index].key != key);

  auto& child_to_split = children_[split_index];

  if (split_index == 0 && !child_to_split) {
    *right = std::move(*this);
    *this = NodeBuilder();
    return Status::OK;
  }

  if (split_index == entries_.size() && !child_to_split) {
    *right = NodeBuilder();
    return Status::OK;
  }

  type_ = BuilderType::NEW_NODE;

  // Recursively call |Split| on the child.
  NodeBuilder sub_right;
  RETURN_ON_ERROR(
      child_to_split.Split(page_storage, std::move(key), &sub_right));

  std::vector<Entry> right_entries;

  right_entries.reserve(entries_.size() - split_index);
  right_entries.insert(right_entries.end(),
                       std::make_move_iterator(entries_.begin() + split_index),
                       std::make_move_iterator(entries_.end()));

  std::vector<NodeBuilder> right_children;
  right_children.reserve(children_.size() - split_index);
  right_children.push_back(std::move(sub_right));
  right_children.insert(
      right_children.end(),
      std::make_move_iterator(children_.begin() + split_index + 1),
      std::make_move_iterator(children_.end()));

  *right = NodeBuilder::CreateNewBuilder(level_, std::move(right_entries),
                                         std::move(right_children));

  entries_.erase(entries_.begin() + split_index, entries_.end());
  children_.erase(children_.begin() + split_index + 1, children_.end());

  if (entries_.empty() && !children_[0]) {
    *this = NodeBuilder();
  }
  FXL_DCHECK(Validate());

  return Status::OK;
}

Status NodeBuilder::Merge(SynchronousStorage* page_storage, NodeBuilder other) {
  if (!other) {
    return Status::OK;
  }

  if (!*this) {
    *this = std::move(other);
    return Status::OK;
  }

  // |NULL_NODE|s do not have the level_ assigned. Only check the level if both
  // are non-null.
  FXL_DCHECK(level_ == other.level_);

  RETURN_ON_ERROR(ComputeContent(page_storage));
  RETURN_ON_ERROR(other.ComputeContent(page_storage));

  type_ = BuilderType::NEW_NODE;

  // Merge the right-most child from |left| with the left-most child
  // from |right|.
  RETURN_ON_ERROR(
      children_.back().Merge(page_storage, std::move(other.children_.front())));

  // Concatenate entries.
  entries_.insert(entries_.end(),
                  std::make_move_iterator(other.entries_.begin()),
                  std::make_move_iterator(other.entries_.end()));

  // Concatenate children, skipping the first child from other.
  children_.insert(children_.end(),
                   std::make_move_iterator(other.children_.begin() + 1),
                   std::make_move_iterator(other.children_.end()));
  return Status::OK;
}

void NodeBuilder::ExtractContent(const TreeNode& node,
                                 std::vector<Entry>* entries,
                                 std::vector<NodeBuilder>* children) {
  FXL_DCHECK(entries);
  FXL_DCHECK(children);
  *entries = std::vector<Entry>(node.entries().begin(), node.entries().end());
  children->clear();
  size_t next_index = 0;
  for (const auto& child : node.children_identifiers()) {
    for (; next_index < child.first; ++next_index) {
      children->push_back(NodeBuilder());
    }
    children->push_back(
        NodeBuilder::CreateExistingBuilder(node.level() - 1, child.second));
    ++next_index;
  }
  for (; next_index <= entries->size(); ++next_index) {
    children->push_back(NodeBuilder());
  }
}

// Apply |changes| on |root|. This is called recursively until |changes| is not
// valid anymore. At this point, build is called on |root|.
Status ApplyChangesOnRoot(const NodeLevelCalculator* node_level_calculator,
                          SynchronousStorage* page_storage, NodeBuilder root,
                          std::unique_ptr<Iterator<const EntryChange>> changes,
                          ObjectIdentifier* object_identifier,
                          std::set<ObjectIdentifier>* new_identifiers) {
  Status status;
  while (changes->Valid()) {
    EntryChange change = **changes;
    changes->Next();

    bool did_mutate;
    status = root.Apply(node_level_calculator, page_storage, std::move(change),
                        &did_mutate);
    if (status != Status::OK) {
      return status;
    }
  }

  if (changes->GetStatus() != Status::OK) {
    return changes->GetStatus();
  }
  return root.Build(page_storage, object_identifier, new_identifiers);
}

}  // namespace

const NodeLevelCalculator* GetDefaultNodeLevelCalculator() {
  return &kDefaultNodeLevelCalculator;
}

void ApplyChanges(
    coroutine::CoroutineService* coroutine_service, PageStorage* page_storage,
    ObjectIdentifier root_identifier,
    std::unique_ptr<Iterator<const EntryChange>> changes,
    fit::function<void(Status, ObjectIdentifier, std::set<ObjectIdentifier>)>
        callback,
    const NodeLevelCalculator* node_level_calculator) {
  FXL_DCHECK(storage::IsDigestValid(root_identifier.object_digest));
  coroutine_service->StartCoroutine(fxl::MakeCopyable(
      [page_storage, root_identifier = std::move(root_identifier),
       changes = std::move(changes), callback = std::move(callback),
       node_level_calculator](coroutine::CoroutineHandler* handler) mutable {
        SynchronousStorage storage(page_storage, handler);

        NodeBuilder root;
        Status status = NodeBuilder::FromIdentifier(
            &storage, std::move(root_identifier), &root);
        if (status != Status::OK) {
          callback(status, {}, {});
          return;
        }
        ObjectIdentifier object_identifier;
        std::set<ObjectIdentifier> new_identifiers;
        status = ApplyChangesOnRoot(node_level_calculator, &storage,
                                    std::move(root), std::move(changes),
                                    &object_identifier, &new_identifiers);
        if (status != Status::OK) {
          callback(status, {}, {});
          return;
        }

        if (!object_identifier.object_digest.empty()) {
          callback(Status::OK, std::move(object_identifier),
                   std::move(new_identifiers));
          return;
        }

        TreeNode::Empty(page_storage, [callback = std::move(callback)](
                                          Status status,
                                          ObjectIdentifier object_identifier) {
          std::set<ObjectIdentifier> new_identifiers({object_identifier});
          callback(status, std::move(object_identifier),
                   std::move(new_identifiers));
        });
      }));
}

}  // namespace btree
}  // namespace storage
