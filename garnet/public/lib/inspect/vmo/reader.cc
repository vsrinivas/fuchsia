// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "reader.h"

#include <lib/inspect-vmo/block.h>
#include <lib/inspect-vmo/scanner.h>
#include <lib/inspect-vmo/snapshot.h>

#include <stack>
#include <unordered_map>

namespace inspect {
namespace vmo {

using internal::Block;
using internal::BlockIndex;

namespace reader {

namespace {

// A ParsedObject contains parsed information for an object, but may not
// contain a concrete value for the object itself.
// It is built iteratively as children and values are discovered.
struct ParsedObject {
  // The object itself, set when the block containing the OBJECT_VALUE is
  // parsed.
  std::optional<fuchsia::inspect::Object> object;

  // The vector of metrics, appended to as each metric is parsed.
  std::vector<fuchsia::inspect::Metric> metrics;

  // The vector of properties, appended to as each property is parsed.
  std::vector<fuchsia::inspect::Property> properties;

  // Vector of block indices for children of this object.
  std::vector<BlockIndex> children;

  // Initializes the stored object with the given name.
  void InitializeObject(std::string name) {
    object = fuchsia::inspect::Object();
    object->name = std::move(name);
  }
};

// The |Reader| supports reading the contents of a |Snapshot|.
// This class constructs a hierarchy of objects contained in the snapshot
// if the snapshot is valid.
class Reader {
 public:
  Reader(Snapshot snapshot) : snapshot_(std::move(snapshot)) {}

  // Read the contents of the snapshot and return the root object.
  std::unique_ptr<ObjectHierarchy> GetRootObject();

 private:
  // Gets a pointer to the ParsedObject for the given index. A new ParsedObject
  // is created if one did not exist previously for the index.
  ParsedObject* GetOrCreate(BlockIndex index);

  void InnerScanBlocks();

  // Initialize an Object for the given BlockIndex.
  void InnerCreateObject(BlockIndex index, const Block* block);

  // Parse a metric block and attach it to the given parent.
  void InnerParseMetric(ParsedObject* parent, const Block* block);

  // Parse a property block and attach it to the given parent.
  void InnerParseProperty(ParsedObject* parent, const Block* block);

  // Helper to interpret the given block as a NAME block and return a
  // copy of the name contents.
  std::string GetAndValidateName(internal::BlockIndex index);

  // Contents of the read VMO.
  Snapshot snapshot_;

  // Map of block index to the parsed object being constructed for that address.
  std::unordered_map<BlockIndex, ParsedObject> parsed_objects_;
};

}  // namespace

std::string Reader::GetAndValidateName(internal::BlockIndex index) {
  Block* block = snapshot_.GetBlock(index);
  if (!block) {
    return nullptr;
  }
  size_t size = OrderToSize(GetOrder(block));
  size_t len = internal::NameBlockFields::Length::Get<size_t>(block->header);
  if (len > size) {
    return nullptr;
  }
  return std::string(block->payload.data, len);
}

void Reader::InnerScanBlocks() {
  internal::ScanBlocks(
      snapshot_.data(), snapshot_.size(),
      [this](BlockIndex index, const Block* block) {
        BlockType type = GetType(block);
        if (index == 0) {
          if (type != BlockType::kHeader) {
            return;
          }
        } else if (type == BlockType::kObjectValue) {
          // This block defines an Object, use the value to fill out the name of
          // the ParsedObject.
          InnerCreateObject(index, block);
        } else if (type == BlockType::kIntValue ||
                   type == BlockType::kUintValue ||
                   type == BlockType::kDoubleValue) {
          // This block defines a metric for an Object, parse the metric into
          // the metrics field of the ParsedObject.
          auto parent_index =
              internal::ValueBlockFields::ParentIndex::Get<BlockIndex>(
                  block->header);
          InnerParseMetric(GetOrCreate(parent_index), block);
        } else if (type == BlockType::kStringValue) {
          // This block defines a property for an Object, parse the property
          // into the properties field of the ParsedObject.
          auto parent_index =
              internal::ValueBlockFields::ParentIndex::Get<BlockIndex>(
                  block->header);
          InnerParseProperty(GetOrCreate(parent_index), block);
        }
      });
}

std::unique_ptr<ObjectHierarchy> Reader::GetRootObject() {
  if (!snapshot_) {
    // Snapshot is invalid, return a default object.
    return nullptr;
  }

  // Scan blocks into the parsed_object map.
  InnerScanBlocks();

  // Stack of objects to process, consisting of a pair of parent object pointer
  // and the ParsedObject. The parent objects are guaranteed to be owned by the
  // root object or one of its descendents.
  std::stack<std::pair<ObjectHierarchy*, BlockIndex>> objects_to_populate;

  // Helper function to process an individual parsed object by index. The object
  // is removed from the parsed_objects and its children are added to the stack.
  auto process_object =
      [this, &objects_to_populate](
          BlockIndex index) -> std::unique_ptr<ObjectHierarchy> {
    auto it = parsed_objects_.find(index);
    if (it == parsed_objects_.end() || !it->second.object) {
      return nullptr;
    }
    auto current = std::move(it->second);
    parsed_objects_.erase(it);
    if (!current.object) {
      return nullptr;
    }

    auto ret = std::make_unique<ObjectHierarchy>();
    ret->object() = std::move(*current.object);
    ret->object().properties.reset(std::move(current.properties));
    ret->object().metrics.reset(std::move(current.metrics));
    for (const auto& index : current.children) {
      objects_to_populate.push(std::make_pair(ret.get(), index));
    }

    return ret;
  };

  auto root_object = process_object(1);

  while (!objects_to_populate.empty()) {
    auto current = std::move(objects_to_populate.top());
    objects_to_populate.pop();

    ObjectHierarchy* parent = current.first;
    auto obj = process_object(current.second);
    if (obj) {
      parent->children().push_back(std::move(*obj));
    }
  }

  return root_object;
}

ParsedObject* Reader::GetOrCreate(BlockIndex index) {
  return &parsed_objects_.emplace(index, ParsedObject()).first->second;
}

void Reader::InnerParseMetric(ParsedObject* parent, const Block* block) {
  auto name = GetAndValidateName(
      internal::ValueBlockFields::NameIndex::Get<size_t>(block->header));
  if (name.empty()) {
    return;
  }

  fuchsia::inspect::Metric metric;
  metric.key = std::move(name);

  BlockType type = GetType(block);
  switch (type) {
    case BlockType::kIntValue:
      metric.value.set_int_value(block->payload.i64);
      break;
    case BlockType::kUintValue:
      metric.value.set_uint_value(block->payload.u64);
      break;
    case BlockType::kDoubleValue:
      metric.value.set_double_value(block->payload.f64);
      break;
    default:
      return;
  }
  parent->metrics.emplace_back(std::move(metric));
}

void Reader::InnerParseProperty(ParsedObject* parent, const Block* block) {
  auto name = GetAndValidateName(
      internal::ValueBlockFields::NameIndex::Get<size_t>(block->header));
  if (name.empty()) {
    return;
  }

  size_t remaining_length =
      internal::PropertyBlockPayload::TotalLength::Get<size_t>(
          block->payload.u64);
  const size_t total_length = remaining_length;

  size_t current_offset = 0;
  char buf[total_length];

  BlockIndex cur_extent =
      internal::PropertyBlockPayload::ExtentIndex::Get<BlockIndex>(
          block->payload.u64);
  auto* extent = snapshot_.GetBlock(cur_extent);
  while (remaining_length > 0) {
    if (!extent || GetType(extent) != BlockType::kExtent) {
      break;
    }
    size_t len =
        fbl::min(remaining_length, internal::PayloadCapacity(GetOrder(extent)));
    memcpy(buf + current_offset, extent->payload.data, len);
    remaining_length -= len;
    current_offset += len;
    extent = snapshot_.GetBlock(
        internal::ExtentBlockFields::NextExtentIndex::Get<BlockIndex>(
            extent->header));
  }

  fuchsia::inspect::Property property;
  property.key = std::move(name);
  property.value.str() = std::string(buf, total_length);
  parent->properties.emplace_back(std::move(property));
}

void Reader::InnerCreateObject(BlockIndex index, const Block* block) {
  auto name = GetAndValidateName(
      internal::ValueBlockFields::NameIndex::Get<BlockIndex>(block->header));
  if (name.empty()) {
    return;
  }
  auto* parsed_object = GetOrCreate(index);
  parsed_object->InitializeObject(std::move(name));
  auto parent_index =
      internal::ValueBlockFields::ParentIndex::Get<BlockIndex>(block->header);
  if (parent_index) {
    // Only link to a parent if the parent can be valid (not index 0).
    auto* parent = GetOrCreate(parent_index);
    parent->children.push_back(index);
  }
}

std::unique_ptr<ObjectHierarchy> ReadSnapshot(Snapshot snapshot) {
  Reader reader(std::move(snapshot));
  return reader.GetRootObject();
}

}  // namespace reader
}  // namespace vmo
}  // namespace inspect
