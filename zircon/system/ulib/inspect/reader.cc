// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>
#include <stack>
#include <unordered_map>

#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/cpp/vmo/block.h>
#include <lib/inspect/cpp/vmo/scanner.h>
#include <lib/inspect/cpp/vmo/snapshot.h>

namespace inspect {

namespace internal {

// A ParsedNode contains parsed information for a node.
// It is built iteratively as children and values are discovered.
//
// A ParsedNode is valid only if it has been initialized with a name and
// parent index (which happens when its OBJECT_VALUE block is read).
//
// A ParsedNode is "complete" when the number of children in the parsed
// hierarchy matches an expected count. At this point the Hierarchy may be
// removed and the ParsedNode discarded.
struct ParsedNode {
  // The node hierarchy being parsed out of the buffer.
  // Propertys and properties are parsed into here as they are read.
  Hierarchy hierarchy;

  // The number of children expected for this node.
  // The node is considered "complete" once the number of children in the
  // hierarchy matches this count.
  size_t children_count = 0;

  // The index of the parent, only valid if this node is initialized.
  BlockIndex parent;

  // Initializes the stored node with the given name and parent.
  void InitializeNode(std::string name, BlockIndex parent) {
    hierarchy.node().name() = std::move(name);
    this->parent = parent;
    initialized_ = true;
  }

  explicit operator bool() { return initialized_; }

  bool is_complete() { return hierarchy.children().size() == children_count; }

 private:
  bool initialized_ = false;
};

// The |Reader| supports reading the contents of a |Snapshot|.
// This class constructs a hierarchy of nodes contained in the snapshot
// if the snapshot is valid.
class Reader {
 public:
  Reader(Snapshot snapshot) : snapshot_(std::move(snapshot)) {}

  // Read the contents of the snapshot and return the root node.
  fit::result<Hierarchy> Read();

 private:
  // Gets a pointer to the ParsedNode for the given index. A new ParsedObject
  // is created if one did not exist previously for the index.
  ParsedNode* GetOrCreate(BlockIndex index);

  void InnerScanBlocks();

  // Initialize an Object for the given BlockIndex.
  void InnerCreateObject(BlockIndex index, const Block* block);

  // Parse a numeric property block and attach it to the given parent.
  void InnerParseNumericProperty(ParsedNode* parent, const Block* block);

  // Parse a property block and attach it to the given parent.
  void InnerParseProperty(ParsedNode* parent, const Block* block);

  // Helper to interpret the given block as a NAME block and return a
  // copy of the name contents.
  std::string GetAndValidateName(BlockIndex index);

  // Contents of the read VMO.
  Snapshot snapshot_;

  // Map of block index to the parsed node being constructed for that address.
  std::unordered_map<BlockIndex, ParsedNode> parsed_nodes_;
};

std::string Reader::GetAndValidateName(BlockIndex index) {
  const Block* block = snapshot_.GetBlock(index);
  if (!block) {
    return nullptr;
  }
  size_t size = OrderToSize(GetOrder(block));
  auto len = NameBlockFields::Length::Get<size_t>(block->header);
  if (len > size) {
    return nullptr;
  }
  return std::string(block->payload.data, len);
}

void Reader::InnerScanBlocks() {
  ScanBlocks(snapshot_.data(), snapshot_.size(), [this](BlockIndex index, const Block* block) {
    BlockType type = GetType(block);
    if (index == 0) {
      if (type != BlockType::kHeader) {
        return;
      }
    } else if (type == BlockType::kNodeValue) {
      // This block defines an Object, use the value to fill out the name of
      // the ParsedNode.
      InnerCreateObject(index, block);
    } else if (type == BlockType::kIntValue || type == BlockType::kUintValue ||
               type == BlockType::kDoubleValue || type == BlockType::kArrayValue) {
      // This block defines a numeric property for an Object, parse the
      // property into the properties field of the ParsedNode.
      auto parent_index = ValueBlockFields::ParentIndex::Get<BlockIndex>(block->header);
      InnerParseNumericProperty(GetOrCreate(parent_index), block);
    } else if (type == BlockType::kPropertyValue) {
      // This block defines a property for an Object, parse the property
      // into the properties field of the ParsedNode.
      auto parent_index = ValueBlockFields::ParentIndex::Get<BlockIndex>(block->header);
      InnerParseProperty(GetOrCreate(parent_index), block);
    }
  });
}

fit::result<Hierarchy> Reader::Read() {
  if (!snapshot_) {
    // Snapshot is invalid, return an error.
    return fit::error();
  }

  // Scan blocks into the parsed_node map. This creates ParsedNodes with
  // properties and an accurate count of the number of expected
  // children. ParsedNodes with a valid OBJECT_VALUE block are initialized
  // with a name and parent index.
  InnerScanBlocks();

  // Stack of completed nodes to process. Entries consist of the completed
  // Hierarchy and the block index of their parent.
  std::stack<std::pair<Hierarchy, BlockIndex>> complete_nodes;

  // Iterate over the map of parsed nodes and find those nodes that are
  // already "complete." These nodes are moved to the complete_nodes map for
  // bottom-up processing.
  auto it = parsed_nodes_.begin();
  while (it != parsed_nodes_.end()) {
    if (!it->second) {
      // The node is not valid, ignore.
      it = parsed_nodes_.erase(it);
      continue;
    }

    if (it->second.is_complete()) {
      // The node is valid and complete, push it onto the stack.
      complete_nodes.push(std::make_pair(std::move(it->second.hierarchy), it->second.parent));
      it = parsed_nodes_.erase(it);
      continue;
    }

    ++it;
  }

  // Construct a valid hierarchy from the bottom up by attaching completed
  // nodes to their parent node. Once a parent becomes complete, add it to
  // the stack to recursively bubble the completed children towards the root.
  while (!complete_nodes.empty()) {
    auto obj = std::move(complete_nodes.top());
    complete_nodes.pop();

    if (obj.second == 0) {
      // We stop once we find a complete node with parent 0. This is assumed
      // to be the root, so we return it.
      // TODO(crjohns): Deal with the case of multiple roots, if we decide to
      // support it.
      return fit::ok(std::move(obj.first));
    }

    // Get the parent node, which was created during block scanning.
    auto it = parsed_nodes_.find(obj.second);
    ZX_ASSERT(it != parsed_nodes_.end());
    auto* parent = &it->second;
    parent->hierarchy.children().emplace_back(std::move(obj.first));
    if (parent->is_complete()) {
      // The parent node is now complete, push it onto the stack.
      complete_nodes.push(std::make_pair(std::move(parent->hierarchy), parent->parent));
      parsed_nodes_.erase(it);
    }
  }

  // We processed all completed nodes but could not find a complete root,
  // return an error.
  return fit::error();
}

ParsedNode* Reader::GetOrCreate(BlockIndex index) {
  return &parsed_nodes_.emplace(index, ParsedNode()).first->second;
}

ArrayDisplayFormat ArrayBlockFormatToDisplay(ArrayBlockFormat format) {
  switch (format) {
    case ArrayBlockFormat::kLinearHistogram:
      return ArrayDisplayFormat::LINEAR_HISTOGRAM;
    case ArrayBlockFormat::kExponentialHistogram:
      return ArrayDisplayFormat::EXPONENTIAL_HISTOGRAM;
    default:
      return ArrayDisplayFormat::FLAT;
  }
}

void Reader::InnerParseNumericProperty(ParsedNode* parent, const Block* block) {
  auto name = GetAndValidateName(ValueBlockFields::NameIndex::Get<size_t>(block->header));
  if (name.empty()) {
    return;
  }

  auto& parent_props = parent->hierarchy.node().properties();

  BlockType type = GetType(block);
  switch (type) {
    case BlockType::kIntValue:
      parent_props.emplace_back(
          PropertyValue(std::move(name), IntPropertyValue(block->payload.i64)));
      return;
    case BlockType::kUintValue:
      parent_props.emplace_back(
          PropertyValue(std::move(name), UintPropertyValue(block->payload.u64)));
      return;
    case BlockType::kDoubleValue:
      parent_props.emplace_back(
          PropertyValue(std::move(name), DoublePropertyValue(block->payload.f64)));
      return;
    case BlockType::kArrayValue: {
      auto entry_type = ArrayBlockPayload::EntryType::Get<BlockType>(block->payload.u64);
      auto count = ArrayBlockPayload::Count::Get<uint8_t>(block->payload.u64);
      if (GetArraySlot<const int64_t>(block, count - 1) == nullptr) {
        // Block does not store the entire array.
        return;
      }

      auto array_format = ArrayBlockFormatToDisplay(
          ArrayBlockPayload::Flags::Get<ArrayBlockFormat>(block->payload.u64));

      if (entry_type == BlockType::kIntValue) {
        std::vector<int64_t> values;
        std::copy(GetArraySlot<const int64_t>(block, 0), GetArraySlot<const int64_t>(block, count),
                  std::back_inserter(values));
        parent_props.emplace_back(
            PropertyValue(std::move(name), IntArrayValue(std::move(values), array_format)));
      } else if (entry_type == BlockType::kUintValue) {
        std::vector<uint64_t> values;
        std::copy(GetArraySlot<const uint64_t>(block, 0),
                  GetArraySlot<const uint64_t>(block, count), std::back_inserter(values));
        parent_props.emplace_back(
            PropertyValue(std::move(name), UintArrayValue(std::move(values), array_format)));
      } else if (entry_type == BlockType::kDoubleValue) {
        std::vector<double> values;
        std::copy(GetArraySlot<const double>(block, 0), GetArraySlot<const double>(block, count),
                  std::back_inserter(values));
        parent_props.emplace_back(
            PropertyValue(std::move(name), DoubleArrayValue(std::move(values), array_format)));
      }
      return;
    }
    default:
      return;
  }
}

void Reader::InnerParseProperty(ParsedNode* parent, const Block* block) {
  auto name = GetAndValidateName(ValueBlockFields::NameIndex::Get<size_t>(block->header));
  if (name.empty()) {
    return;
  }

  size_t remaining_length = PropertyBlockPayload::TotalLength::Get<size_t>(block->payload.u64);
  const size_t total_length = remaining_length;

  size_t current_offset = 0;
  std::vector<uint8_t> buf(total_length);

  BlockIndex cur_extent = PropertyBlockPayload::ExtentIndex::Get<BlockIndex>(block->payload.u64);
  auto* extent = snapshot_.GetBlock(cur_extent);
  while (remaining_length > 0) {
    if (!extent || GetType(extent) != BlockType::kExtent) {
      break;
    }
    size_t len = std::min(remaining_length, PayloadCapacity(GetOrder(extent)));
    memcpy(&buf[current_offset], extent->payload.data, len);
    remaining_length -= len;
    current_offset += len;
    extent =
        snapshot_.GetBlock(ExtentBlockFields::NextExtentIndex::Get<BlockIndex>(extent->header));
  }

  auto& parent_properties = parent->hierarchy.node().properties();
  if (PropertyBlockPayload::Flags::Get<uint8_t>(block->payload.u64) &
      static_cast<uint8_t>(inspect::PropertyBlockFormat::kBinary)) {
    parent_properties.emplace_back(
        inspect::PropertyValue(std::move(name), inspect::ByteVectorPropertyValue(buf)));
  } else {
    parent_properties.emplace_back(inspect::PropertyValue(
        std::move(name), inspect::StringPropertyValue(std::string(buf.begin(), buf.end()))));
  }
}

void Reader::InnerCreateObject(BlockIndex index, const Block* block) {
  auto name = GetAndValidateName(ValueBlockFields::NameIndex::Get<BlockIndex>(block->header));
  if (name.empty()) {
    return;
  }
  auto* parsed_node = GetOrCreate(index);
  auto parent_index = ValueBlockFields::ParentIndex::Get<BlockIndex>(block->header);
  parsed_node->InitializeNode(std::move(name), parent_index);
  if (parent_index && parent_index != index) {
    // Only link to a parent if the parent can be valid (not index 0).
    auto* parent = GetOrCreate(parent_index);
    parent->children_count += 1;
  }
}
}  // namespace internal

fit::result<Hierarchy> ReadFromSnapshot(Snapshot snapshot) {
  internal::Reader reader(std::move(snapshot));
  return reader.Read();
}

fit::result<Hierarchy> ReadFromVmo(const zx::vmo& vmo) {
  inspect::Snapshot snapshot;
  if (inspect::Snapshot::Create(std::move(vmo), &snapshot) != ZX_OK) {
    return fit::error();
  }
  return ReadFromSnapshot(std::move(snapshot));
}

fit::result<Hierarchy> ReadFromBuffer(std::vector<uint8_t> buffer) {
  inspect::Snapshot snapshot;
  if (inspect::Snapshot::Create(std::move(buffer), &snapshot) != ZX_OK) {
    // TODO(CF-865): Best-effort read of invalid snapshots.
    return fit::error();
  }
  return ReadFromSnapshot(std::move(snapshot));
}

}  // namespace inspect
