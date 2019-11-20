// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/inspect_deprecated/inspect.h"

#include "src/lib/syslog/cpp/logger.h"

using component::ObjectDir;

namespace inspect_deprecated {

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
void internal::RemoveEntity<component::Metric>(component::Object* object, const std::string& name) {
  object->RemoveMetric(name);
}

LazyMetric::LazyMetric() {}

LazyMetric::LazyMetric(internal::EntityWrapper<component::Metric> entity)
    : entity_(std::move(entity)) {}
void LazyMetric::Set(MetricCallback callback) {
  if (entity_) {
    entity_->ParentObject()->SetMetric(entity_->name(),
                                       component::CallbackMetric(std::move(callback)));
  }
}

#define DEFINE_PROPERTY_METHODS(CLASS, TYPE)                                                    \
  CLASS::CLASS() {}                                                                             \
  CLASS::CLASS(internal::EntityWrapper<component::Property> entity) {                           \
    entity_.template emplace<kEntityWrapperVariant>(std::move(entity));                         \
  }                                                                                             \
  CLASS::CLASS(::inspect::CLASS entity) {                                                       \
    entity_.template emplace<kVmoVariant>(std::move(entity));                                   \
  }                                                                                             \
  void CLASS::Set(TYPE value) {                                                                 \
    if (entity_.index() == kEntityWrapperVariant) {                                             \
      auto& entity = entity_.template get<kEntityWrapperVariant>();                             \
      entity.ParentObject()->SetProperty(entity.name(), component::Property(std::move(value))); \
    } else if (entity_.index() == kVmoVariant) {                                                \
      entity_.template get<kVmoVariant>().Set(value);                                           \
    }                                                                                           \
  }

#define DEFINE_LAZY_PROPERTY_METHODS(CLASS, TYPE)                                  \
  CLASS::CLASS() {}                                                                \
  CLASS::CLASS(internal::EntityWrapper<component::Property> entity)                \
      : entity_(std::move(entity)) {}                                              \
  void CLASS::Set(TYPE value) {                                                    \
    if (entity_) {                                                                 \
      entity_->ParentObject()->SetProperty(entity_->name(),                        \
                                           component::Property(std::move(value))); \
    }                                                                              \
  }

DEFINE_PROPERTY_METHODS(StringProperty, std::string)
DEFINE_PROPERTY_METHODS(ByteVectorProperty, VectorValue)
DEFINE_LAZY_PROPERTY_METHODS(LazyStringProperty, StringValueCallback)
DEFINE_LAZY_PROPERTY_METHODS(LazyByteVectorProperty, VectorValueCallback)

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

Node::Node(std::string name) : Node(component::ExposedObject(std::move(name))) {}

Node::Node(ObjectDir object_dir) : Node(component::ExposedObject(std::move(object_dir))) {}

Node::Node(::inspect::Node object) {
  object_.template emplace<kVmoVariant>(std::move(object));
  node_ptr_ = &object_.template get<kVmoVariant>();
}

Node::Node(::inspect::Node* node_ptr) { node_ptr_ = node_ptr; }

Node::Node(component::ExposedObject object) {
  object_.template emplace<kComponentVariant>(std::move(object));
}

Node::Node(Node&& other) { *this = std::move(other); }

Node& Node::operator=(Node&& other) {
  object_ = std::move(other.object_);
  other.node_ptr_ = nullptr;
  if (object_.index() == kVmoVariant) {
    node_ptr_ = &object_.template get<kVmoVariant>();
  }
  return *this;
}

Node::~Node() {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    // TODO(nathaniel): Does this if have to be here? Is object being assigned
    // to an empty shared_ptr a rough edge that can be sanded down? See
    // discussion near the end of
    // https://fuchsia-review.googlesource.com/c/fuchsia/+/288093.
    if (object) {
      for (auto& detacher : object_.template get<kComponentVariant>().object()->TakeDetachers()) {
        detacher.cancel();
      }
    }
  }
};

fuchsia::inspect::deprecated::Object Node::object() const {
  if (object_.index() == kComponentVariant) {
    return object_.template get<kComponentVariant>().object()->ToFidl();
  }
  return fuchsia::inspect::deprecated::Object();
}

component::ObjectDir Node::object_dir() const {
  if (object_.index() == kComponentVariant) {
    return component::ObjectDir(object_.template get<kComponentVariant>().object());
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
  } else if (node_ptr_) {
    return Node(node_ptr_->CreateChild(std::move(name)));
  }
  return Node();
}

IntMetric Node::CreateIntMetric(std::string name, int64_t value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetMetric(name, component::IntMetric(value));
    return IntMetric(internal::EntityWrapper<component::Metric>(std::move(name), object));
  } else if (node_ptr_) {
    return IntMetric(node_ptr_->CreateInt(std::move(name), value));
  }

  return IntMetric();
}

UIntMetric Node::CreateUIntMetric(std::string name, uint64_t value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetMetric(name, component::UIntMetric(value));
    return UIntMetric(internal::EntityWrapper<component::Metric>(std::move(name), object));
  } else if (node_ptr_) {
    return UIntMetric(node_ptr_->CreateUint(std::move(name), value));
  }

  return UIntMetric();
}

DoubleMetric Node::CreateDoubleMetric(std::string name, double value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetMetric(name, component::DoubleMetric(value));
    return DoubleMetric(internal::EntityWrapper<component::Metric>(std::move(name), object));
  } else if (node_ptr_) {
    return DoubleMetric(node_ptr_->CreateDouble(std::move(name), value));
  }

  return DoubleMetric();
}

IntArray Node::CreateIntArray(std::string name, size_t slots) {
  if (node_ptr_) {
    return IntArray(node_ptr_->CreateIntArray(name, slots));
  }
  return IntArray();
}

UIntArray Node::CreateUIntArray(std::string name, size_t slots) {
  if (node_ptr_) {
    return UIntArray(node_ptr_->CreateUintArray(name, slots));
  }
  return UIntArray();
}

DoubleArray Node::CreateDoubleArray(std::string name, size_t slots) {
  if (node_ptr_) {
    return DoubleArray(node_ptr_->CreateDoubleArray(name, slots));
  }
  return DoubleArray();
}

LinearIntHistogramMetric Node::CreateLinearIntHistogramMetric(std::string name, int64_t floor,
                                                              int64_t step_size, size_t buckets) {
  if (node_ptr_) {
    return LinearIntHistogramMetric(
        node_ptr_->CreateLinearIntHistogram(name, floor, step_size, buckets));
  }
  return LinearIntHistogramMetric();
}

LinearUIntHistogramMetric Node::CreateLinearUIntHistogramMetric(std::string name, uint64_t floor,
                                                                uint64_t step_size,
                                                                size_t buckets) {
  if (node_ptr_) {
    return LinearUIntHistogramMetric(
        node_ptr_->CreateLinearUintHistogram(name, floor, step_size, buckets));
  }
  return LinearUIntHistogramMetric();
}

LinearDoubleHistogramMetric Node::CreateLinearDoubleHistogramMetric(std::string name, double floor,
                                                                    double step_size,
                                                                    size_t buckets) {
  if (node_ptr_) {
    return LinearDoubleHistogramMetric(
        node_ptr_->CreateLinearDoubleHistogram(name, floor, step_size, buckets));
  }
  return LinearDoubleHistogramMetric();
}

ExponentialIntHistogramMetric Node::CreateExponentialIntHistogramMetric(std::string name,
                                                                        int64_t floor,
                                                                        int64_t initial_step,
                                                                        int64_t step_multiplier,
                                                                        size_t buckets) {
  if (node_ptr_) {
    return ExponentialIntHistogramMetric(node_ptr_->CreateExponentialIntHistogram(
        name, floor, initial_step, step_multiplier, buckets));
  }
  return ExponentialIntHistogramMetric();
}

ExponentialUIntHistogramMetric Node::CreateExponentialUIntHistogramMetric(std::string name,
                                                                          uint64_t floor,
                                                                          uint64_t initial_step,
                                                                          uint64_t step_multiplier,
                                                                          size_t buckets) {
  if (node_ptr_) {
    return ExponentialUIntHistogramMetric(node_ptr_->CreateExponentialUintHistogram(
        name, floor, initial_step, step_multiplier, buckets));
  }
  return ExponentialUIntHistogramMetric();
}

ExponentialDoubleHistogramMetric Node::CreateExponentialDoubleHistogramMetric(
    std::string name, double floor, double initial_step, double step_multiplier, size_t buckets) {
  if (node_ptr_) {
    return ExponentialDoubleHistogramMetric(node_ptr_->CreateExponentialDoubleHistogram(
        name, floor, initial_step, step_multiplier, buckets));
  }
  return ExponentialDoubleHistogramMetric();
}

LazyMetric Node::CreateLazyMetric(std::string name, component::Metric::ValueCallback callback) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetMetric(name, component::CallbackMetric(std::move(callback)));
    return LazyMetric(internal::EntityWrapper<component::Metric>(std::move(name), object));
  }
  return LazyMetric();
}

StringProperty Node::CreateStringProperty(std::string name, std::string value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetProperty(name, component::Property(std::move(value)));
    return StringProperty(internal::EntityWrapper<component::Property>(std::move(name), object));
  } else if (node_ptr_) {
    return StringProperty(node_ptr_->CreateString(std::move(name), value));
  }

  return StringProperty();
}

ByteVectorProperty Node::CreateByteVectorProperty(std::string name, VectorValue value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetProperty(name, component::Property(std::move(value)));
    return ByteVectorProperty(
        internal::EntityWrapper<component::Property>(std::move(name), object));
  } else if (node_ptr_) {
    return ByteVectorProperty(node_ptr_->CreateByteVector(std::move(name), value));
  }

  return ByteVectorProperty();
}

LazyStringProperty Node::CreateLazyStringProperty(std::string name, StringValueCallback value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetProperty(name, component::Property(std::move(value)));
    return LazyStringProperty(
        internal::EntityWrapper<component::Property>(std::move(name), object));
  }
  return LazyStringProperty();
}

LazyByteVectorProperty Node::CreateLazyByteVectorProperty(std::string name,
                                                          VectorValueCallback value) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetProperty(name, component::Property(std::move(value)));
    return LazyByteVectorProperty(
        internal::EntityWrapper<component::Property>(std::move(name), object));
  }
  return LazyByteVectorProperty();
}

ChildrenCallback Node::CreateChildrenCallback(ChildrenCallbackFunction callback) {
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetChildrenCallback(std::move(callback));
    return ChildrenCallback(object);
  }
  return ChildrenCallback();
}

fit::deferred_callback Node::SetChildrenManager(ChildrenManager* children_manager) {
  FX_CHECK(children_manager) << "children_manager must be non-null!";
  if (object_.index() == kComponentVariant) {
    auto object = object_.template get<kComponentVariant>().object();
    object->SetChildrenManager(children_manager);
    return fit::defer_callback([object] { object->SetChildrenManager(nullptr); });
  }
  return fit::defer_callback([] {});
}

const TreeSettings kDefaultTreeSettings = {.initial_size = 4096, .maximum_size = 256 * 1024};

Tree::Tree()
    : inspector_(::inspect::Inspector()), root_(std::make_unique<Node>(&inspector_.GetRoot())) {}

Tree::Tree(::inspect::Inspector inspector)
    : inspector_(std::move(inspector)), root_(std::make_unique<Node>(&inspector_.GetRoot())) {}

zx::vmo Tree::DuplicateVmo() const { return inspector_.DuplicateVmo(); }

Node& Tree::GetRoot() const { return *root_; }

Tree Inspector::CreateTree() { return CreateTree(kDefaultTreeSettings); }

Tree Inspector::CreateTree(TreeSettings settings) {
  auto inspector =
      ::inspect::Inspector(::inspect::InspectSettings{.maximum_size = settings.maximum_size});

  return Tree(std::move(inspector));
}

std::string UniqueName(const std::string& prefix) {
  return component::ExposedObject::UniqueName(prefix);
}

}  // namespace inspect_deprecated
