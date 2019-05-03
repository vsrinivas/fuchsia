// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/inspect/inspect.h"

#include "lib/inspect-vmo/block.h"

using component::ObjectDir;

namespace inspect {

template <>
component::Metric internal::MakeMetric<int64_t>(int64_t value) {
  return component::IntMetric(value);
}

template <>
component::Metric internal::MakeMetric<uint64_t>(uint64_t value) {
  return component::UIntMetric(value);
}

template <>
component::Metric internal::MakeMetric<double>(double value) {
  return component::DoubleMetric(value);
}

template <>
void internal::RemoveEntity<component::Property>(component::Object* object,
                                                 const std::string& name) {
  object->RemoveProperty(name);
}

template <>
void internal::RemoveEntity<component::Metric>(component::Object* object,
                                               const std::string& name) {
  object->RemoveMetric(name);
}

LazyMetric::LazyMetric() {}

LazyMetric::LazyMetric(internal::EntityWrapper<component::Metric> entity)
    : entity_(std::move(entity)) {}
void LazyMetric::Set(MetricCallback callback) {
  if (entity_) {
    entity_->ParentObject()->SetMetric(
        entity_->name(), component::CallbackMetric(std::move(callback)));
  }
}

#define DEFINE_PROPERTY_METHODS(CLASS, TYPE)                            \
  CLASS::CLASS() {}                                                     \
  CLASS::CLASS(internal::EntityWrapper<component::Property> entity) {   \
    entity_.template emplace<kEntityWrapperVariant>(std::move(entity)); \
  }                                                                     \
  CLASS::CLASS(vmo::Property entity) {                                  \
    entity_.template emplace<kVmoVariant>(std::move(entity));           \
  }                                                                     \
  void CLASS::Set(TYPE value) {                                         \
    if (entity_.index() == kEntityWrapperVariant) {                     \
      auto& entity = entity_.template get<kEntityWrapperVariant>();     \
      entity.ParentObject()->SetProperty(                               \
          entity.name(), component::Property(std::move(value)));        \
    } else if (entity_.index() == kVmoVariant) {                        \
      entity_.template get<kVmoVariant>().Set(                          \
          {(const char*)value.data(), value.size()});                   \
    }                                                                   \
  }

#define DEFINE_LAZY_PROPERTY_METHODS(CLASS, TYPE)                   \
  CLASS::CLASS() {}                                                 \
  CLASS::CLASS(internal::EntityWrapper<component::Property> entity) \
      : entity_(std::move(entity)) {}                               \
  void CLASS::Set(TYPE value) {                                     \
    if (entity_) {                                                  \
      entity_->ParentObject()->SetProperty(                         \
          entity_->name(), component::Property(std::move(value)));  \
    }                                                               \
  }

DEFINE_PROPERTY_METHODS(StringProperty, std::string)
DEFINE_PROPERTY_METHODS(ByteVectorProperty, VectorValue)
DEFINE_LAZY_PROPERTY_METHODS(LazyStringProperty, StringValueCallback)
DEFINE_LAZY_PROPERTY_METHODS(LazyByteVectorProperty, VectorValueCallback)

ChildrenCallback::ChildrenCallback() {}

ChildrenCallback::ChildrenCallback(std::shared_ptr<component::Object> object)
    : parent_obj_(std::move(object)) {}

ChildrenCallback::~ChildrenCallback() {
  // Remove the entity from its parent if it has a parent.
  if (parent_obj_) {
    parent_obj_->ClearChildrenCallback();
  }
}

void ChildrenCallback::Set(ChildrenCallbackFunction callback) {
  if (parent_obj_) {
    parent_obj_->SetChildrenCallback(std::move(callback));
  }
}

ChildrenCallback& ChildrenCallback::operator=(ChildrenCallback&& other) {
  // Remove the entity from its parent before moving values over.
  if (parent_obj_ && parent_obj_.get() != other.parent_obj_.get()) {
    parent_obj_->ClearChildrenCallback();
  }
  parent_obj_ = std::move(other.parent_obj_);
  return *this;
}

Node::Node(std::string name)
    : Node(component::ExposedObject(std::move(name))) {}

Node::Node(ObjectDir object_dir)
    : Node(component::ExposedObject(std::move(object_dir))) {}

Node::Node(vmo::Object object) {
  object_.template emplace<kVmoVariant>(std::move(object));
}

Node::Node(component::ExposedObject object) {
  object_.template emplace<kComponentVariant>(std::move(object));
}

fuchsia::inspect::Object Node::object() const {
  if (object_.index() == kComponentVariant) {
    return object_.template get<kComponentVariant>().object()->ToFidl();
  }
  return fuchsia::inspect::Object();
}

component::ObjectDir Node::object_dir() const {
  if (object_.index() == kComponentVariant) {
    return component::ObjectDir(
        object_.template get<kComponentVariant>().object());
  }
  return component::ObjectDir();
}

component::Object::StringOutputVector Node::children() const {
  if (object_.index() == kComponentVariant) {
    return object_.template get<kComponentVariant>().object()->GetChildren();
  }
  return component::Object::StringOutputVector();
}

Node Node::CreateChild(std::string name) {
  if (object_.index() == kComponentVariant) {
    component::ExposedObject child(std::move(name));
    object_.template get<kComponentVariant>().add_child(&child);
    return Node(std::move(child));
  } else if (object_.index() == kVmoVariant) {
    return Node(
        object_.template get<kVmoVariant>().CreateChild(std::move(name)));
  }
  return Node();
}

IntMetric Node::CreateIntMetric(std::string name, int64_t value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetMetric(name, component::IntMetric(value));
    return IntMetric(
        internal::EntityWrapper<component::Metric>(std::move(name), object));
  } else if (object_.index() == kVmoVariant) {
    return IntMetric(object_.template get<kVmoVariant>().CreateIntMetric(
        std::move(name), value));
  }

  return IntMetric();
}

UIntMetric Node::CreateUIntMetric(std::string name, uint64_t value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetMetric(name, component::UIntMetric(value));
    return UIntMetric(
        internal::EntityWrapper<component::Metric>(std::move(name), object));
  } else if (object_.index() == kVmoVariant) {
    return UIntMetric(object_.template get<kVmoVariant>().CreateUintMetric(
        std::move(name), value));
  }

  return UIntMetric();
}

DoubleMetric Node::CreateDoubleMetric(std::string name, double value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetMetric(name, component::DoubleMetric(value));
    return DoubleMetric(
        internal::EntityWrapper<component::Metric>(std::move(name), object));
  } else if (object_.index() == kVmoVariant) {
    return DoubleMetric(object_.template get<kVmoVariant>().CreateDoubleMetric(
        std::move(name), value));
  }

  return DoubleMetric();
}

IntArray Node::CreateIntArray(std::string name, size_t slots) {
  return CreateIntArray(std::move(name), slots, vmo::ArrayFormat::kDefault);
}

IntArray Node::CreateIntArray(std::string name, size_t slots,
                              vmo::ArrayFormat format) {
  if (object_.index() == kVmoVariant) {
    return IntArray(object_.template get<kVmoVariant>().CreateIntArray(
        name, slots, format));
  }
  return IntArray();
}

UIntArray Node::CreateUIntArray(std::string name, size_t slots) {
  return CreateUIntArray(std::move(name), slots, vmo::ArrayFormat::kDefault);
}

UIntArray Node::CreateUIntArray(std::string name, size_t slots,
                                vmo::ArrayFormat format) {
  if (object_.index() == kVmoVariant) {
    return UIntArray(object_.template get<kVmoVariant>().CreateUintArray(
        name, slots, format));
  }
  return UIntArray();
}

DoubleArray Node::CreateDoubleArray(std::string name, size_t slots) {
  return CreateDoubleArray(std::move(name), slots, vmo::ArrayFormat::kDefault);
}

DoubleArray Node::CreateDoubleArray(std::string name, size_t slots,
                                    vmo::ArrayFormat format) {
  if (object_.index() == kVmoVariant) {
    return DoubleArray(object_.template get<kVmoVariant>().CreateDoubleArray(
        name, slots, format));
  }
  return DoubleArray();
}

LinearIntHistogramMetric Node::CreateLinearIntHistogramMetric(std::string name,
                                                              int64_t floor,
                                                              int64_t step_size,
                                                              size_t buckets) {
  if (object_.index() == kVmoVariant) {
    return LinearIntHistogramMetric(
        object_.template get<kVmoVariant>().CreateLinearIntHistogram(
            name, floor, step_size, buckets));
  }
  return LinearIntHistogramMetric();
}

LinearUIntHistogramMetric Node::CreateLinearUIntHistogramMetric(
    std::string name, uint64_t floor, uint64_t step_size, size_t buckets) {
  if (object_.index() == kVmoVariant) {
    return LinearUIntHistogramMetric(
        object_.template get<kVmoVariant>().CreateLinearUintHistogram(
            name, floor, step_size, buckets));
  }
  return LinearUIntHistogramMetric();
}

LinearDoubleHistogramMetric Node::CreateLinearDoubleHistogramMetric(
    std::string name, double floor, double step_size, size_t buckets) {
  if (object_.index() == kVmoVariant) {
    return LinearDoubleHistogramMetric(
        object_.template get<kVmoVariant>().CreateLinearDoubleHistogram(
            name, floor, step_size, buckets));
  }
  return LinearDoubleHistogramMetric();
}

ExponentialIntHistogramMetric Node::CreateExponentialIntHistogramMetric(
    std::string name, int64_t floor, int64_t initial_step,
    int64_t step_multiplier, size_t buckets) {
  if (object_.index() == kVmoVariant) {
    return ExponentialIntHistogramMetric(
        object_.template get<kVmoVariant>().CreateExponentialIntHistogram(
            name, floor, initial_step, step_multiplier, buckets));
  }
  return ExponentialIntHistogramMetric();
}

ExponentialUIntHistogramMetric Node::CreateExponentialUIntHistogramMetric(
    std::string name, uint64_t floor, uint64_t initial_step,
    uint64_t step_multiplier, size_t buckets) {
  if (object_.index() == kVmoVariant) {
    return ExponentialUIntHistogramMetric(
        object_.template get<kVmoVariant>().CreateExponentialUintHistogram(
            name, floor, initial_step, step_multiplier, buckets));
  }
  return ExponentialUIntHistogramMetric();
}

ExponentialDoubleHistogramMetric Node::CreateExponentialDoubleHistogramMetric(
    std::string name, double floor, double initial_step, double step_multiplier,
    size_t buckets) {
  if (object_.index() == kVmoVariant) {
    return ExponentialDoubleHistogramMetric(
        object_.template get<kVmoVariant>().CreateExponentialDoubleHistogram(
            name, floor, initial_step, step_multiplier, buckets));
  }
  return ExponentialDoubleHistogramMetric();
}

LazyMetric Node::CreateLazyMetric(std::string name,
                                  component::Metric::ValueCallback callback) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetMetric(name, component::CallbackMetric(std::move(callback)));
    return LazyMetric(
        internal::EntityWrapper<component::Metric>(std::move(name), object));
  }
  return LazyMetric();
}

StringProperty Node::CreateStringProperty(std::string name, std::string value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetProperty(name, component::Property(std::move(value)));
    return StringProperty(
        internal::EntityWrapper<component::Property>(std::move(name), object));
  } else if (object_.index() == kVmoVariant) {
    return StringProperty(object_.template get<kVmoVariant>().CreateProperty(
        std::move(name), {value.data(), value.size()},
        inspect::vmo::PropertyFormat::kUtf8));
  }

  return StringProperty();
}

ByteVectorProperty Node::CreateByteVectorProperty(std::string name,
                                                  VectorValue value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetProperty(name, component::Property(std::move(value)));
    return ByteVectorProperty(
        internal::EntityWrapper<component::Property>(std::move(name), object));
  } else if (object_.index() == kVmoVariant) {
    return ByteVectorProperty(
        object_.template get<kVmoVariant>().CreateProperty(
            std::move(name), {(const char*)value.data(), value.size()},
            inspect::vmo::PropertyFormat::kBinary));
  }

  return ByteVectorProperty();
}

LazyStringProperty Node::CreateLazyStringProperty(std::string name,
                                                  StringValueCallback value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetProperty(name, component::Property(std::move(value)));
    return LazyStringProperty(
        internal::EntityWrapper<component::Property>(std::move(name), object));
  }
  return LazyStringProperty();
}

LazyByteVectorProperty Node::CreateLazyByteVectorProperty(
    std::string name, VectorValueCallback value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetProperty(name, component::Property(std::move(value)));
    return LazyByteVectorProperty(
        internal::EntityWrapper<component::Property>(std::move(name), object));
  }
  return LazyByteVectorProperty();
}

ChildrenCallback Node::CreateChildrenCallback(
    ChildrenCallbackFunction callback) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetChildrenCallback(std::move(callback));
    return ChildrenCallback(object);
  }
  return ChildrenCallback();
}

namespace internal {
struct TreeState {
  // The root of the tree.
  Node root;

  // The VMO inspector object for this tree.
  vmo::Inspector inspector;
};
};  // namespace internal

const TreeSettings kDefaultTreeSettings = {.initial_size = 4096,
                                           .maximum_size = 256 * 1024};

Tree::Tree(std::unique_ptr<internal::TreeState> state)
    : state_(std::move(state)) {}

Tree::~Tree() {}

const zx::vmo& Tree::GetVmo() const { return state_->inspector.GetVmo(); }

Node& Tree::GetRoot() const { return state_->root; }

Tree Inspector::CreateTree(std::string name) {
  return CreateTree(std::move(name), kDefaultTreeSettings);
}

Tree Inspector::CreateTree(std::string name, TreeSettings settings) {
  auto state = std::make_unique<internal::TreeState>();
  state->inspector =
      vmo::Inspector(settings.initial_size, settings.maximum_size);
  state->root = Node(state->inspector.CreateObject(name.c_str()));

  return Tree(std::move(state));
}

std::string UniqueName(const std::string& prefix) {
  return component::ExposedObject::UniqueName(prefix);
}

}  // namespace inspect
