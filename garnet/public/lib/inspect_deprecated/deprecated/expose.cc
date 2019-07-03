// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/service.h>
#include <lib/fit/defer.h>
#include <lib/inspect_deprecated/deprecated/expose.h>
#include <lib/syslog/cpp/logger.h>

namespace component {

void Property::Set(std::string value) { value_.emplace<std::string>(std::move(value)); }

void Property::Set(ByteVector value) { value_.emplace<ByteVector>(std::move(value)); }

void Property::Set(StringValueCallback callback) {
  value_.emplace<StringValueCallback>(std::move(callback));
}

void Property::Set(VectorValueCallback callback) {
  value_.emplace<VectorValueCallback>(std::move(callback));
}

fuchsia::inspect::Property Property::ToFidl(const std::string& name) const {
  fuchsia::inspect::Property ret;
  ret.key = name;
  if (std::holds_alternative<std::string>(value_)) {
    ret.value.set_str(std::get<std::string>(value_));
  } else if (std::holds_alternative<ByteVector>(value_)) {
    fidl::VectorPtr<uint8_t> vec;
    auto& val = std::get<ByteVector>(value_);
    std::copy(val.begin(), val.end(), std::back_inserter(*vec));
    ret.value.set_bytes(std::move(vec));
  } else if (std::holds_alternative<StringValueCallback>(value_)) {
    ret.value.set_str(std::get<StringValueCallback>(value_)());
  } else if (std::holds_alternative<VectorValueCallback>(value_)) {
    fidl::VectorPtr<uint8_t> vec;
    auto val = std::get<VectorValueCallback>(value_)();
    std::copy(val.begin(), val.end(), std::back_inserter(*vec));
    ret.value.set_bytes(std::move(vec));
  }
  return ret;
}

void Metric::SetInt(int64_t value) {
  type_ = INT;
  int_value_ = value;
}

void Metric::SetUInt(uint64_t value) {
  type_ = UINT;
  uint_value_ = value;
}

void Metric::SetDouble(double value) {
  type_ = DOUBLE;
  double_value_ = value;
}

void Metric::SetCallback(ValueCallback callback) {
  type_ = CALLBACK;
  callback_ = std::move(callback);
}

std::string Metric::ToString() const {
  switch (type_) {
    case INT:
      return std::to_string(int_value_);
    case UINT:
      return std::to_string(uint_value_);
    case DOUBLE:
      return std::to_string(double_value_);
    case CALLBACK:
      Metric temp;
      callback_(&temp);
      return temp.ToString();
  }
}

fuchsia::inspect::Metric Metric::ToFidl(const std::string& name) const {
  fuchsia::inspect::Metric ret;
  switch (type_) {
    case INT:
      ret.value.set_int_value(int_value_);
      break;
    case UINT:
      ret.value.set_uint_value(uint_value_);
      break;
    case DOUBLE:
      ret.value.set_double_value(double_value_);
      break;
    case CALLBACK:
      Metric temp;
      callback_(&temp);
      return temp.ToFidl(name);
  }
  ret.key = name;
  return ret;
}

Metric IntMetric(int64_t value) {
  Metric ret;
  ret.SetInt(value);
  return ret;
}

Metric UIntMetric(uint64_t value) {
  Metric ret;
  ret.SetUInt(value);
  return ret;
}

Metric DoubleMetric(double value) {
  Metric ret;
  ret.SetDouble(value);
  return ret;
}

Metric CallbackMetric(Metric::ValueCallback callback) {
  Metric ret;
  ret.SetCallback(std::move(callback));
  return ret;
}

Object::Object(std::string name) : name_(std::move(name)), children_manager_(nullptr) {
  FX_CHECK(std::find(name_.begin(), name_.end(), '\0') == name_.end())
      << "Object name cannot contain null bytes";
  bindings_.set_empty_set_handler([this] {
    std::shared_ptr<Object> self_if_bindings;
    std::vector<fit::deferred_callback> detachers;
    {
      std::lock_guard lock(mutex_);
      FX_DCHECK(self_if_bindings_);
      detachers_.swap(detachers);
      self_if_bindings = std::move(self_if_bindings_);
      self_if_bindings_ = nullptr;
    }
  });
}

Object::~Object() {
  std::lock_guard lock(mutex_);
  FX_CHECK(detachers_.empty());
}

void Object::InnerAddBinding(fidl::InterfaceRequest<fuchsia::inspect::Inspect> chan) {
  if (!self_if_bindings_) {
    FX_DCHECK(bindings_.size() == 0);
    self_if_bindings_ = self_weak_ptr_.lock();
  }
  bindings_.AddBinding(this, std::move(chan));
}

void Object::AddBinding(fidl::InterfaceRequest<Inspect> chan) {
  std::lock_guard<std::mutex> lock(mutex_);
  InnerAddBinding(std::move(chan));
}

void Object::AddBinding(fidl::InterfaceRequest<Inspect> chan, fit::deferred_callback detacher) {
  std::lock_guard lock(mutex_);
  InnerAddBinding(std::move(chan));
  detachers_.push_back(std::move(detacher));
}

void Object::ReadData(ReadDataCallback callback) { callback(ToFidl()); }

fidl::VectorPtr<std::string> Object::ListUnmanagedChildNames() {
  StringOutputVector child_names;
  // Lock the local child vector. No need to lock children since we are only
  // reading their constant name.
  std::lock_guard lock(mutex_);
  for (const auto& [child_name_as_map_key, child] : children_) {
    child_names.push_back(child->name());
  }
  // TODO(crjohns): lazy_object_callback_ should not be carried over into the
  // new implementation.
  if (lazy_object_callback_) {
    ObjectVector lazy_objects;
    lazy_object_callback_(&lazy_objects);
    for (const auto& obj : lazy_objects) {
      child_names.push_back(obj->name());
    }
  }
  return child_names;
}

void Object::ListChildren(ListChildrenCallback callback) {
  std::lock_guard children_manager_lock(children_manager_mutex_);
  if (children_manager_) {
    children_manager_->GetNames([this, callback = std::move(callback)](
                                    std::vector<std::string> children_manager_child_names) {
      std::set<std::string> all_child_names(children_manager_child_names.begin(),
                                            children_manager_child_names.end());
      {
        std::lock_guard lock(mutex_);
        for (const auto& [unused_child_name_as_map_key, child] : children_) {
          all_child_names.insert(child->name());
        }
      }
      StringOutputVector child_names;
      for (const auto& child_name : all_child_names) {
        child_names.push_back(child_name);
      }
      callback(std::move(child_names));
    });
  } else {
    callback(ListUnmanagedChildNames());
  }
}

std::shared_ptr<Object> Object::GetUnmanagedChild(std::string name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = children_.find(name.data());
  if (it != children_.end()) {
    return it->second;
  }

  // If the child was not found yet, check all lazily initialized children.
  if (lazy_object_callback_) {
    ObjectVector lazy_objects;
    lazy_object_callback_(&lazy_objects);
    for (auto& obj : lazy_objects) {
      if (name == obj->name()) {
        return obj;
      }
    }
  }

  // Child not found.
  return std::shared_ptr<Object>();
}

void Object::OpenChild(std::string name, ::fidl::InterfaceRequest<Inspect> child_channel,
                       OpenChildCallback callback) {
  std::lock_guard children_manager_lock(children_manager_mutex_);
  if (children_manager_) {
    children_manager_->Attach(
        name, [this, name, child_channel = std::move(child_channel),
               callback = std::move(callback)](fit::closure detacher) mutable {
          // Upon calling of this callback passed to Attached, the
          // component-under-inspection has either added a child to the
          // hierarchy with name |name| or not, and the detacher passed to this
          // callback is our means of reporting to the
          // component-under-inspection that we no longer have an active
          // in the child. In the case of child-not-present, we call the
          // detacher right away - our interest in "nothing" ended the moment it
          // started. In the case of child-is-present, we make the binding to
          // the child that was the reason for calling OpenChild in the first
          // place and we pass the detacher to the child to be called when the
          // child's binding set is empty (because our interest in the child
          // ends when all bindings to it are unbound).
          auto deferred_detacher = fit::defer_callback(std::move(detacher));
          auto child = GetUnmanagedChild(name);
          if (!child) {
            callback(false);

          } else {
            child->AddBinding(std::move(child_channel), std::move(deferred_detacher));
            callback(true);
          }
        });
  } else {
    auto child = GetUnmanagedChild(name);
    if (!child) {
      callback(false);
      return;
    }
    child->AddBinding(std::move(child_channel));
    callback(true);
  }
}

std::shared_ptr<Object> Object::GetChild(std::string name) {
  std::lock_guard<std::mutex> children_manager_lock(children_manager_mutex_);
  FX_CHECK(!children_manager_) << "GetChild not yet supported with a ChildrenManager!";
  return GetUnmanagedChild(name);
}

void Object::SetChild(std::shared_ptr<Object> child) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = children_.find(child->name().data());
  if (it != children_.end()) {
    it->second.swap(child);
  } else {
    children_.insert(std::make_pair(child->name().data(), child));
  }
}

std::shared_ptr<Object> Object::TakeChild(std::string name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = children_.find(name.c_str());
  if (it == children_.end()) {
    return std::shared_ptr<Object>();
  }
  auto ret = it->second;
  children_.erase(it);
  return ret;
}

void Object::SetChildrenCallback(ChildrenCallback callback) {
  std::lock_guard<std::mutex> children_manager_lock(children_manager_mutex_);
  std::lock_guard<std::mutex> lock(mutex_);
  FX_CHECK(!children_manager_) << "Simultaneous use of children callback "
                                  "and children manager not supported!";
  lazy_object_callback_ = std::move(callback);
}

void Object::ClearChildrenCallback() {
  std::lock_guard<std::mutex> lock(mutex_);
  ChildrenCallback temp;
  lazy_object_callback_.swap(temp);
}

void Object::SetChildrenManager(ChildrenManager* children_manager) {
  std::vector<std::vector<fit::deferred_callback>> detachers;
  std::lock_guard<std::mutex> children_manager_lock(children_manager_mutex_);
  std::lock_guard<std::mutex> lock(mutex_);
  FX_CHECK(!lazy_object_callback_) << "Simultaneous use of children callback "
                                      "and children manager not supported!";
  FX_CHECK(!children_manager || !children_manager_)
      << "At least one of children_manager and children_manager_ must be null!";
  children_manager_ = children_manager;

  // Detachers provided to Inspect in response to a call on the being-replaced
  // ChildrenManager should not be retained by Inspect now that the
  // being-replaced ChildrenManager is being replaced, so gather those detachers
  // up now and (after releasing all locks) destroy them (which can have any
  // component-implemented effect up to and including destroying this object).
  detachers.reserve(children_.size());
  for (auto& [unused_child_name_as_map_key, child] : children_) {
    detachers.emplace_back(child->TakeDetachers());
  }
}

std::vector<fit::deferred_callback> Object::TakeDetachers() {
  std::vector<fit::deferred_callback> detachers;
  std::lock_guard lock(mutex_);
  detachers_.swap(detachers);
  return detachers;
}

bool Object::RemoveProperty(const std::string& name) {
  std::lock_guard lock(mutex_);
  auto it = properties_.find(name.c_str());
  if (it != properties_.end()) {
    properties_.erase(it);
    return true;
  }
  return false;
}

bool Object::RemoveMetric(const std::string& name) {
  std::lock_guard lock(mutex_);
  auto it = metrics_.find(name.c_str());
  if (it != metrics_.end()) {
    metrics_.erase(it);
    return true;
  }
  return false;
}

bool Object::SetProperty(const std::string& name, Property value) {
  if (name.find('\0') != std::string::npos) {
    FX_DCHECK(false) << "Null bytes are not allowed in property names.";
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  properties_[name.c_str()] = std::move(value);
  return true;
}

bool Object::SetMetric(const std::string& name, Metric metric) {
  if (name.find('\0') != std::string::npos) {
    FX_DCHECK(false) << "Null bytes are not allowed in metric names.";
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  metrics_[name.c_str()] = std::move(metric);
  return true;
}

fuchsia::inspect::Object Object::ToFidl() {
  std::lock_guard lock(mutex_);
  fuchsia::inspect::Object ret;
  ret.name = name_.data();
  for (const auto& it : properties_) {
    ret.properties.push_back(it.second.ToFidl(it.first));
  }
  for (const auto& it : metrics_) {
    ret.metrics.push_back(it.second.ToFidl(it.first));
  }
  return ret;
}

Object::StringOutputVector Object::GetChildren() { return ListUnmanagedChildNames(); }

}  // namespace component
