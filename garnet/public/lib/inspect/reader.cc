// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/inspect/reader.h"

#include <lib/inspect-vmo/block.h>
#include <lib/inspect-vmo/scanner.h>
#include <lib/inspect-vmo/snapshot.h>
#include "lib/fit/bridge.h"

#include <stack>
#include <unordered_map>

namespace inspect {

namespace {
ObjectHierarchy Read(std::shared_ptr<component::Object> object_root,
                     int depth) {
  auto obj = object_root->ToFidl();
  if (depth == 0) {
    return ObjectHierarchy(std::move(obj), {});
  } else {
    std::vector<ObjectHierarchy> children;
    auto child_names = object_root->GetChildren();
    for (auto& child_name : *child_names) {
      auto child_obj = object_root->GetChild(child_name);
      if (child_obj) {
        children.emplace_back(Read(child_obj, depth - 1));
      }
    }
    return ObjectHierarchy(std::move(obj), std::move(children));
  }
}

}  // namespace

ObjectHierarchy ReadFromObject(const inspect::Object& object, int depth) {
  return Read(object.object_dir().object(), depth);
}

ObjectReader::ObjectReader(
    fidl::InterfaceHandle<fuchsia::inspect::Inspect> inspect_handle)
    : state_(std::make_shared<internal::ObjectReaderState>()) {
  state_->inspect_ptr_.Bind(std::move(inspect_handle));
}

fit::promise<fuchsia::inspect::Object> ObjectReader::Read() const {
  fit::bridge<fuchsia::inspect::Object> bridge;
  state_->inspect_ptr_->ReadData(bridge.completer.bind());
  return bridge.consumer.promise_or(fit::error());
}

fit::promise<ChildNameVector> ObjectReader::ListChildren() const {
  fit::bridge<ChildNameVector> bridge;
  state_->inspect_ptr_->ListChildren(bridge.completer.bind());
  return bridge.consumer.promise_or(fit::error());
}

fit::promise<ObjectReader> ObjectReader::OpenChild(
    std::string child_name) const {
  fuchsia::inspect::InspectPtr child_ptr;

  fit::bridge<bool> bridge;

  state_->inspect_ptr_->OpenChild(child_name, child_ptr.NewRequest(),
                                  bridge.completer.bind());

  ObjectReader reader(child_ptr.Unbind());
  return bridge.consumer.promise_or(fit::error())
      .and_then([ret = std::move(reader)](
                    bool success) mutable -> fit::result<ObjectReader> {
        if (success) {
          return fit::ok(ObjectReader(std::move(ret)));
        } else {
          return fit::error();
        }
      });
}

fit::promise<std::vector<ObjectReader>> ObjectReader::OpenChildren() const {
  return ListChildren()
      .and_then([reader = *this](const ChildNameVector& children) {
        std::vector<fit::promise<ObjectReader>> opens;
        for (const auto& child_name : *children) {
          opens.emplace_back(reader.OpenChild(child_name));
        }
        return fit::join_promise_vector(std::move(opens));
      })
      .and_then([](std::vector<fit::result<ObjectReader>>& objects) {
        std::vector<ObjectReader> result;
        for (auto& obj : objects) {
          if (obj.is_ok()) {
            result.emplace_back(obj.take_value());
          }
        }
        return fit::ok(std::move(result));
      });
}

ObjectHierarchy::ObjectHierarchy(fuchsia::inspect::Object object,
                                 std::vector<ObjectHierarchy> children)
    : object_(std::move(object)), children_(std::move(children)) {}

fit::promise<ObjectHierarchy> ReadFromFidl(ObjectReader reader, int depth) {
  auto reader_promise = reader.Read();
  if (depth == 0) {
    return reader_promise.and_then([reader](fuchsia::inspect::Object& obj) {
      return fit::ok(ObjectHierarchy(std::move(obj), {}));
    });
  } else {
    auto children_promise =
        reader.OpenChildren()
            .and_then(
                [depth](std::vector<ObjectReader>& result)
                    -> fit::promise<std::vector<fit::result<ObjectHierarchy>>> {
                  std::vector<fit::promise<ObjectHierarchy>> children;
                  for (auto& reader : result) {
                    children.emplace_back(
                        ReadFromFidl(std::move(reader), depth - 1));
                  }

                  return fit::join_promise_vector(std::move(children));
                })
            .and_then([](std::vector<fit::result<ObjectHierarchy>>& result) {
              std::vector<ObjectHierarchy> children;
              for (auto& res : result) {
                if (res.is_ok()) {
                  children.emplace_back(res.take_value());
                }
              }
              return fit::ok(std::move(children));
            });

    return fit::join_promises(std::move(reader_promise),
                              std::move(children_promise))
        .and_then([reader](
                      std::tuple<fit::result<fuchsia::inspect::Object>,
                                 fit::result<std::vector<ObjectHierarchy>>>&
                          result) mutable -> fit::result<ObjectHierarchy> {
          if (!std::get<0>(result).is_ok() || !std::get<0>(result).is_ok()) {
            return fit::error();
          }
          return fit::ok(ObjectHierarchy(std::get<0>(result).take_value(),
                                         std::get<1>(result).take_value()));
        });
  }
}

namespace vmo {
namespace internal {

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
  fit::result<ObjectHierarchy> GetRootObject();

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
  std::string GetAndValidateName(BlockIndex index);

  // Contents of the read VMO.
  Snapshot snapshot_;

  // Map of block index to the parsed object being constructed for that address.
  std::unordered_map<BlockIndex, ParsedObject> parsed_objects_;
};

std::string Reader::GetAndValidateName(BlockIndex index) {
  Block* block = snapshot_.GetBlock(index);
  if (!block) {
    return nullptr;
  }
  size_t size = OrderToSize(GetOrder(block));
  size_t len = NameBlockFields::Length::Get<size_t>(block->header);
  if (len > size) {
    return nullptr;
  }
  return std::string(block->payload.data, len);
}

void Reader::InnerScanBlocks() {
  ScanBlocks(
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
              ValueBlockFields::ParentIndex::Get<BlockIndex>(block->header);
          InnerParseMetric(GetOrCreate(parent_index), block);
        } else if (type == BlockType::kPropertyValue) {
          // This block defines a property for an Object, parse the property
          // into the properties field of the ParsedObject.
          auto parent_index =
              ValueBlockFields::ParentIndex::Get<BlockIndex>(block->header);
          InnerParseProperty(GetOrCreate(parent_index), block);
        }
      });
}

fit::result<ObjectHierarchy> Reader::GetRootObject() {
  if (!snapshot_) {
    // Snapshot is invalid, return a default object.
    return fit::error();
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

  if (!root_object) {
    return fit::error();
  }

  return fit::ok(std::move(*root_object));
}

ParsedObject* Reader::GetOrCreate(BlockIndex index) {
  return &parsed_objects_.emplace(index, ParsedObject()).first->second;
}

void Reader::InnerParseMetric(ParsedObject* parent, const Block* block) {
  auto name = GetAndValidateName(
      ValueBlockFields::NameIndex::Get<size_t>(block->header));
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
      ValueBlockFields::NameIndex::Get<size_t>(block->header));
  if (name.empty()) {
    return;
  }

  size_t remaining_length =
      PropertyBlockPayload::TotalLength::Get<size_t>(block->payload.u64);
  const size_t total_length = remaining_length;

  size_t current_offset = 0;
  char buf[total_length];

  BlockIndex cur_extent =
      PropertyBlockPayload::ExtentIndex::Get<BlockIndex>(block->payload.u64);
  auto* extent = snapshot_.GetBlock(cur_extent);
  while (remaining_length > 0) {
    if (!extent || GetType(extent) != BlockType::kExtent) {
      break;
    }
    size_t len = fbl::min(remaining_length, PayloadCapacity(GetOrder(extent)));
    memcpy(buf + current_offset, extent->payload.data, len);
    remaining_length -= len;
    current_offset += len;
    extent = snapshot_.GetBlock(
        ExtentBlockFields::NextExtentIndex::Get<BlockIndex>(extent->header));
  }

  fuchsia::inspect::Property property;
  property.key = std::move(name);
  if (PropertyBlockPayload::Flags::Get<uint8_t>(block->payload.u64) &
      static_cast<uint8_t>(inspect::vmo::PropertyFormat::kBinary)) {
    property.value.bytes() = std::vector<uint8_t>(buf, buf + total_length);
  } else {
    property.value.str() = std::string(buf, total_length);
  }
  parent->properties.emplace_back(std::move(property));
}

void Reader::InnerCreateObject(BlockIndex index, const Block* block) {
  auto name = GetAndValidateName(
      ValueBlockFields::NameIndex::Get<BlockIndex>(block->header));
  if (name.empty()) {
    return;
  }
  auto* parsed_object = GetOrCreate(index);
  parsed_object->InitializeObject(std::move(name));
  auto parent_index =
      ValueBlockFields::ParentIndex::Get<BlockIndex>(block->header);
  if (parent_index) {
    // Only link to a parent if the parent can be valid (not index 0).
    auto* parent = GetOrCreate(parent_index);
    parent->children.push_back(index);
  }
}
}  // namespace internal
}  // namespace vmo

fit::result<ObjectHierarchy> ReadFromSnapshot(vmo::Snapshot snapshot) {
  vmo::internal::Reader reader(std::move(snapshot));
  return reader.GetRootObject();
}

fit::result<ObjectHierarchy> ReadFromVmo(const zx::vmo& vmo) {
  inspect::vmo::Snapshot snapshot;
  if (inspect::vmo::Snapshot::Create(std::move(vmo), &snapshot) != ZX_OK) {
    return fit::error();
  }
  return ReadFromSnapshot(std::move(snapshot));
}

}  // namespace inspect
