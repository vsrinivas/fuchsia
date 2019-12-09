// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/inspect_deprecated/reader.h"

#include <lib/inspect/cpp/reader.h>

#include <iterator>
#include <stack>
#include <unordered_map>

#include "fuchsia/inspect/deprecated/cpp/fidl.h"
#include "lib/fit/bridge.h"
#include "lib/fit/result.h"
#include "src/lib/inspect_deprecated/hierarchy.h"

namespace inspect_deprecated {

namespace {

hierarchy::Node FidlObjectToNode(fuchsia::inspect::deprecated::Object obj) {
  std::vector<hierarchy::Property> properties;
  std::vector<hierarchy::Metric> metrics;

  for (auto& metric : obj.metrics) {
    if (metric.value.is_uint_value()) {
      metrics.push_back(hierarchy::Metric(std::move(metric.key),
                                          hierarchy::UIntMetric(metric.value.uint_value())));
    } else if (metric.value.is_int_value()) {
      metrics.push_back(
          hierarchy::Metric(std::move(metric.key), hierarchy::IntMetric(metric.value.int_value())));
    } else if (metric.value.is_double_value()) {
      metrics.push_back(hierarchy::Metric(std::move(metric.key),
                                          hierarchy::DoubleMetric(metric.value.double_value())));
    }
  }

  for (auto& property : obj.properties) {
    if (property.value.is_str()) {
      properties.push_back(hierarchy::Property(
          std::move(property.key), hierarchy::StringProperty(std::move(property.value.str()))));
    } else if (property.value.is_bytes()) {
      properties.push_back(
          hierarchy::Property(std::move(property.key),
                              hierarchy::ByteVectorProperty(std::move(property.value.bytes()))));
    }
  }

  return hierarchy::Node(std::move(obj.name), std::move(properties), std::move(metrics));
}

// TODO(crjohns, nathaniel): Needs to be asynchronous to use a ChildrenManager.
ObjectHierarchy Read(std::shared_ptr<component::Object> object_root, int depth) {
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
    return ObjectHierarchy(FidlObjectToNode(object_root->ToFidl()), std::move(children));
  }
}

hierarchy::ArrayDisplayFormat FromNewFormat(::inspect::ArrayDisplayFormat format) {
  switch (format) {
    case ::inspect::ArrayDisplayFormat::kFlat:
      return hierarchy::ArrayDisplayFormat::FLAT;
    case ::inspect::ArrayDisplayFormat::kLinearHistogram:
      return hierarchy::ArrayDisplayFormat::LINEAR_HISTOGRAM;
    case ::inspect::ArrayDisplayFormat::kExponentialHistogram:
      return hierarchy::ArrayDisplayFormat::EXPONENTIAL_HISTOGRAM;
  }
}

#define CONVERT_METRIC(OLD_NAME, NEW_NAME)                                     \
  if (property.Contains<::inspect::NEW_NAME>()) {                              \
    const auto& val = property.Get<::inspect::NEW_NAME>();                     \
    ret.node().metrics().emplace_back(                                         \
        hierarchy::Metric(property.name(), hierarchy::OLD_NAME(val.value()))); \
  }

#define CONVERT_PROPERTY(OLD_NAME, NEW_NAME)                                     \
  if (property.Contains<::inspect::NEW_NAME>()) {                                \
    const auto& val = property.Get<::inspect::NEW_NAME>();                       \
    ret.node().properties().emplace_back(                                        \
        hierarchy::Property(property.name(), hierarchy::OLD_NAME(val.value()))); \
  }

#define CONVERT_ARRAY(OLD_NAME, NEW_NAME)                                          \
  if (property.Contains<::inspect::NEW_NAME>()) {                                  \
    const auto& val = property.Get<::inspect::NEW_NAME>();                         \
    ret.node().metrics().emplace_back(hierarchy::Metric(                           \
        property.name(),                                                           \
        hierarchy::OLD_NAME(val.value(), FromNewFormat(val.GetDisplayFormat())))); \
  }

ObjectHierarchy FromNewHierarchy(const ::inspect::Hierarchy& hierarchy) {
  ObjectHierarchy ret;
  ret.node().name() = hierarchy.node().name();
  for (const auto& property : hierarchy.node().properties()) {
    CONVERT_METRIC(IntMetric, IntPropertyValue);
    CONVERT_METRIC(UIntMetric, UintPropertyValue);
    CONVERT_METRIC(DoubleMetric, DoublePropertyValue);
    CONVERT_PROPERTY(StringProperty, StringPropertyValue);
    CONVERT_PROPERTY(ByteVectorProperty, ByteVectorPropertyValue);
    CONVERT_ARRAY(IntArray, IntArrayValue);
    CONVERT_ARRAY(UIntArray, UintArrayValue);
    CONVERT_ARRAY(DoubleArray, DoubleArrayValue);
  }

  for (const auto& child : hierarchy.children()) {
    ret.children().emplace_back(FromNewHierarchy(child));
  }

  return ret;
}

}  // namespace

ObjectHierarchy ReadFromObject(const Node& object, int depth) {
  return Read(object.object_dir().object(), depth);
}

ObjectReader::ObjectReader(
    fidl::InterfaceHandle<fuchsia::inspect::deprecated::Inspect> inspect_handle)
    : state_(std::make_shared<internal::ObjectReaderState>()) {
  state_->inspect_ptr_.Bind(std::move(inspect_handle));
}

fit::promise<fuchsia::inspect::deprecated::Object> ObjectReader::Read() const {
  fit::bridge<fuchsia::inspect::deprecated::Object> bridge;
  state_->inspect_ptr_->ReadData(bridge.completer.bind());
  return bridge.consumer.promise_or(fit::error());
}

fit::promise<ChildNameVector> ObjectReader::ListChildren() const {
  fit::bridge<ChildNameVector> bridge;
  state_->inspect_ptr_->ListChildren(bridge.completer.bind());
  return bridge.consumer.promise_or(fit::error());
}

fit::promise<ObjectReader> ObjectReader::OpenChild(std::string child_name) const {
  fuchsia::inspect::deprecated::InspectPtr child_ptr;

  fit::bridge<bool> bridge;

  state_->inspect_ptr_->OpenChild(child_name, child_ptr.NewRequest(), bridge.completer.bind());

  return bridge.consumer.promise_or(fit::error())
      .and_then(
          [chan = child_ptr.Unbind()](const bool& success) mutable -> fit::result<ObjectReader> {
            if (success) {
              return fit::ok(ObjectReader(std::move(chan)));
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
    return reader_promise.and_then([reader](fuchsia::inspect::deprecated::Object& obj) {
      return fit::ok(ObjectHierarchy(FidlObjectToNode(std::move(obj)), {}));
    });
  } else {
    auto children_promise =
        reader.OpenChildren()
            .and_then([depth](std::vector<ObjectReader>& result)
                          -> fit::promise<std::vector<fit::result<ObjectHierarchy>>> {
              std::vector<fit::promise<ObjectHierarchy>> children;
              for (auto& reader : result) {
                children.emplace_back(ReadFromFidl(std::move(reader), depth - 1));
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

    return fit::join_promises(std::move(reader_promise), std::move(children_promise))
        .and_then([reader](std::tuple<fit::result<fuchsia::inspect::deprecated::Object>,
                                      fit::result<std::vector<ObjectHierarchy>>>& result) mutable
                  -> fit::result<ObjectHierarchy> {
          if (!std::get<0>(result).is_ok() || !std::get<1>(result).is_ok()) {
            return fit::error();
          }
          return fit::ok(ObjectHierarchy(FidlObjectToNode(std::get<0>(result).take_value()),
                                         std::get<1>(result).take_value()));
        });
  }
}

fit::result<ObjectHierarchy> ReadFromSnapshot(::inspect::Snapshot snapshot) {
  auto new_hierarchy = ::inspect::ReadFromSnapshot(std::move(snapshot));
  if (!new_hierarchy.is_ok()) {
    return fit::error();
  }
  return fit::ok(FromNewHierarchy(new_hierarchy.value()));
}

fit::result<ObjectHierarchy> ReadFromVmo(const zx::vmo& vmo) {
  inspect::Snapshot snapshot;
  if (inspect::Snapshot::Create(std::move(vmo), &snapshot) != ZX_OK) {
    return fit::error();
  }
  return ::inspect_deprecated::ReadFromSnapshot(std::move(snapshot));
}

fit::result<ObjectHierarchy> ReadFromBuffer(std::vector<uint8_t> buffer) {
  inspect::Snapshot snapshot;
  if (inspect::Snapshot::Create(std::move(buffer), &snapshot) != ZX_OK) {
    // TODO(CF-865): Best-effort read of invalid snapshots.
    return fit::error();
  }
  return ::inspect_deprecated::ReadFromSnapshot(std::move(snapshot));
}

ObjectHierarchy ReadFromFidlObject(fuchsia::inspect::deprecated::Object object) {
  return ObjectHierarchy(FidlObjectToNode(std::move(object)), {});
}

}  // namespace inspect_deprecated
