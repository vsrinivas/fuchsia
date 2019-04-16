// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/inspect/reader.h"

#include <lib/inspect-vmo/block.h>
#include <lib/inspect-vmo/scanner.h>
#include <lib/inspect-vmo/snapshot.h>

#include <iterator>
#include <stack>
#include <unordered_map>

#include "fuchsia/inspect/cpp/fidl.h"
#include "lib/fit/bridge.h"
#include "lib/inspect/hierarchy.h"

namespace inspect {

namespace {

hierarchy::Node FidlObjectToNode(fuchsia::inspect::Object obj) {
  std::vector<hierarchy::Property> properties;
  std::vector<hierarchy::Metric> metrics;

  for (auto& metric : *obj.metrics) {
    if (metric.value.is_uint_value()) {
      metrics.push_back(
          hierarchy::Metric(std::move(metric.key),
                            hierarchy::UIntMetric(metric.value.uint_value())));
    } else if (metric.value.is_int_value()) {
      metrics.push_back(
          hierarchy::Metric(std::move(metric.key),
                            hierarchy::IntMetric(metric.value.int_value())));
    } else if (metric.value.is_double_value()) {
      metrics.push_back(hierarchy::Metric(
          std::move(metric.key),
          hierarchy::DoubleMetric(metric.value.double_value())));
    }
  }

  for (auto& property : *obj.properties) {
    if (property.value.is_str()) {
      properties.push_back(hierarchy::Property(
          std::move(property.key),
          hierarchy::StringProperty(std::move(property.value.str()))));
    } else if (property.value.is_bytes()) {
      properties.push_back(hierarchy::Property(
          std::move(property.key),
          hierarchy::ByteVectorProperty(std::move(property.value.bytes()))));
    }
  }

  return hierarchy::Node(std::move(obj.name), std::move(properties),
                         std::move(metrics));
}

ObjectHierarchy Read(std::shared_ptr<component::Object> object_root,
                     int depth) {
  if (depth == 0) {
    return ObjectHierarchy(FidlObjectToNode(object_root->ToFidl()), {});
  } else {
    std::vector<ObjectHierarchy> children;
    auto child_names = object_root->GetChildren();
    for (auto& child_name : *child_names) {
      auto child_obj = object_root->GetChild(child_name);
      if (child_obj) {
        children.emplace_back(Read(child_obj, depth - 1));
      }
    }
    return ObjectHierarchy(FidlObjectToNode(object_root->ToFidl()),
                           std::move(children));
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

fit::promise<ObjectHierarchy> ReadFromFidl(ObjectReader reader, int depth) {
  auto reader_promise = reader.Read();
  if (depth == 0) {
    return reader_promise.and_then([reader](fuchsia::inspect::Object& obj) {
      return fit::ok(ObjectHierarchy(FidlObjectToNode(std::move(obj)), {}));
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
          return fit::ok(ObjectHierarchy(
              FidlObjectToNode(std::get<0>(result).take_value()),
              std::get<1>(result).take_value()));
        });
  }
}

namespace vmo {
namespace internal {

// A ParsedObject contains parsed information for an object.
// It is built iteratively as children and values are discovered.
//
// A ParsedObject is valid only if it has been initialized with a name and
// parent index (which happens when its OBJECT_VALUE block is read).
//
// A ParsedObject is "complete" when the number of children in the parsed
// hierarchy matches an expected count. At this point the ObjectHierarchy may be
// removed and the ParsedObject discarded.
struct ParsedObject {
  // The object hierarchy being parsed out of the buffer.
  // Metrics and properties are parsed into here as they are read.
  ObjectHierarchy hierarchy;

  // The number of children expected for this object.
  // The object is considered "complete" once the number of children in the
  // hierarchy matches this count.
  size_t children_count = 0;

  // The index of the parent, only valid if this object is initialized.
  BlockIndex parent;

  // Initializes the stored object with the given name and parent.
  void InitializeObject(std::string name, BlockIndex parent) {
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
// This class constructs a hierarchy of objects contained in the snapshot
// if the snapshot is valid.
class Reader {
 public:
  Reader(Snapshot snapshot) : snapshot_(std::move(snapshot)) {}

  // Read the contents of the snapshot and return the root object.
  fit::result<ObjectHierarchy> Read();

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
                   type == BlockType::kDoubleValue ||
                   type == BlockType::kArrayValue) {
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

fit::result<ObjectHierarchy> Reader::Read() {
  if (!snapshot_) {
    // Snapshot is invalid, return a default object.
    return fit::error();
  }

  // Scan blocks into the parsed_object map. This creates ParsedObjects with
  // metrics, properties, and an accurate count of the number of expected
  // children. ParsedObjects with a valid OBJECT_VALUE block are initialized
  // with a name and parent index.
  InnerScanBlocks();

  // Stack of completed objects to process. Entries consist of the completed
  // ObjectHierarchy and the block index of their parent.
  std::stack<std::pair<ObjectHierarchy, BlockIndex>> complete_objects;

  // Iterate over the map of parsed objects and find those objects that are
  // already "complete." These objects are moved to the complete_objects map for
  // bottom-up processing.
  auto it = parsed_objects_.begin();
  while (it != parsed_objects_.end()) {
    if (!it->second) {
      // The object is not valid, ignore.
      it = parsed_objects_.erase(it);
      continue;
    }

    if (it->second.is_complete()) {
      // The object is valid and complete, push it onto the stack.
      complete_objects.push(
          std::make_pair(std::move(it->second.hierarchy), it->second.parent));
      it = parsed_objects_.erase(it);
      continue;
    }

    ++it;
  }

  // Construct a valid hierarchy from the bottom up by attaching completed
  // objects to their parent object. Once a parent becomes complete, add it to
  // the stack to recursively bubble the completed children towards the root.
  while (complete_objects.size() > 0) {
    auto obj = std::move(complete_objects.top());
    complete_objects.pop();

    if (obj.second == 0) {
      // We stop once we find a complete object with parent 0. This is assumed
      // to be the root, so we return it.
      // TODO(crjohns): Deal with the case of multiple roots, if we decide to
      // support it.
      return fit::ok(std::move(obj.first));
    }

    // Get the parent object, which was created during block scanning.
    auto it = parsed_objects_.find(obj.second);
    ZX_ASSERT(it != parsed_objects_.end());
    auto* parent = &it->second;
    parent->hierarchy.children().emplace_back(std::move(obj.first));
    if (parent->is_complete()) {
      // The parent object is now complete, push it onto the stack.
      complete_objects.push(
          std::make_pair(std::move(parent->hierarchy), parent->parent));
      parsed_objects_.erase(it);
    }
  }

  // We processed all completed objects but could not find a complete root,
  // return an error.
  return fit::error();
}

ParsedObject* Reader::GetOrCreate(BlockIndex index) {
  return &parsed_objects_.emplace(index, ParsedObject()).first->second;
}

hierarchy::ArrayDisplayFormat ArrayFormatToDisplay(ArrayFormat format) {
  switch (format) {
    case ArrayFormat::kLinearHistogram:
      return hierarchy::ArrayDisplayFormat::LINEAR_HISTOGRAM;
    case ArrayFormat::kExponentialHistogram:
      return hierarchy::ArrayDisplayFormat::EXPONENTIAL_HISTOGRAM;
    default:
      return hierarchy::ArrayDisplayFormat::FLAT;
  }
}

void Reader::InnerParseMetric(ParsedObject* parent, const Block* block) {
  auto name = GetAndValidateName(
      ValueBlockFields::NameIndex::Get<size_t>(block->header));
  if (name.empty()) {
    return;
  }

  auto& parent_metrics = parent->hierarchy.node().metrics();

  BlockType type = GetType(block);
  switch (type) {
    case BlockType::kIntValue:
      parent_metrics.emplace_back(hierarchy::Metric(
          std::move(name), hierarchy::IntMetric(block->payload.i64)));
      return;
    case BlockType::kUintValue:
      parent_metrics.emplace_back(hierarchy::Metric(
          std::move(name), hierarchy::UIntMetric(block->payload.u64)));
      return;
    case BlockType::kDoubleValue:
      parent_metrics.emplace_back(hierarchy::Metric(
          std::move(name), hierarchy::DoubleMetric(block->payload.f64)));
      return;
    case BlockType::kArrayValue: {
      BlockType entry_type =
          ArrayBlockPayload::EntryType::Get<BlockType>(block->payload.u64);
      uint8_t count =
          ArrayBlockPayload::Count::Get<uint8_t>(block->payload.u64);
      if (GetArraySlot<const int64_t>(block, count - 1) == nullptr) {
        // Block does not store the entire array.
        return;
      }

      auto array_format = ArrayFormatToDisplay(
          ArrayBlockPayload::Flags::Get<ArrayFormat>(block->payload.u64));

      if (entry_type == BlockType::kIntValue) {
        std::vector<int64_t> values;
        std::copy(GetArraySlot<const int64_t>(block, 0),
                  GetArraySlot<const int64_t>(block, count),
                  std::back_inserter(values));
        parent_metrics.emplace_back(hierarchy::Metric(
            std::move(name),
            hierarchy::IntArray(std::move(values), array_format)));
      } else if (entry_type == BlockType::kUintValue) {
        std::vector<uint64_t> values;
        std::copy(GetArraySlot<const uint64_t>(block, 0),
                  GetArraySlot<const uint64_t>(block, count),
                  std::back_inserter(values));
        parent_metrics.emplace_back(hierarchy::Metric(
            std::move(name),
            hierarchy::UIntArray(std::move(values), array_format)));
      } else if (entry_type == BlockType::kDoubleValue) {
        std::vector<double> values;
        std::copy(GetArraySlot<const double>(block, 0),
                  GetArraySlot<const double>(block, count),
                  std::back_inserter(values));
        parent_metrics.emplace_back(hierarchy::Metric(
            std::move(name),
            hierarchy::DoubleArray(std::move(values), array_format)));
      }
      return;
    }
    default:
      return;
  }
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

  auto& parent_properties = parent->hierarchy.node().properties();
  if (PropertyBlockPayload::Flags::Get<uint8_t>(block->payload.u64) &
      static_cast<uint8_t>(inspect::vmo::PropertyFormat::kBinary)) {
    parent_properties.emplace_back(inspect::hierarchy::Property(
        std::move(name), inspect::hierarchy::ByteVectorProperty(
                             std::vector<uint8_t>(buf, buf + total_length))));
  } else {
    parent_properties.emplace_back(inspect::hierarchy::Property(
        std::move(name),
        inspect::hierarchy::StringProperty(std::string(buf, total_length))));
  }
}

void Reader::InnerCreateObject(BlockIndex index, const Block* block) {
  auto name = GetAndValidateName(
      ValueBlockFields::NameIndex::Get<BlockIndex>(block->header));
  if (name.empty()) {
    return;
  }
  auto* parsed_object = GetOrCreate(index);
  auto parent_index =
      ValueBlockFields::ParentIndex::Get<BlockIndex>(block->header);
  parsed_object->InitializeObject(std::move(name), parent_index);
  if (parent_index && parent_index != index) {
    // Only link to a parent if the parent can be valid (not index 0).
    auto* parent = GetOrCreate(parent_index);
    parent->children_count += 1;
  }
}
}  // namespace internal
}  // namespace vmo

fit::result<ObjectHierarchy> ReadFromSnapshot(vmo::Snapshot snapshot) {
  vmo::internal::Reader reader(std::move(snapshot));
  return reader.Read();
}

fit::result<ObjectHierarchy> ReadFromVmo(const zx::vmo& vmo) {
  inspect::vmo::Snapshot snapshot;
  if (inspect::vmo::Snapshot::Create(std::move(vmo), &snapshot) != ZX_OK) {
    return fit::error();
  }
  return ReadFromSnapshot(std::move(snapshot));
}

ObjectHierarchy ReadFromFidlObject(fuchsia::inspect::Object object) {
  return ObjectHierarchy(FidlObjectToNode(std::move(object)), {});
}

}  // namespace inspect
