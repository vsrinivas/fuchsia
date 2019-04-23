// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/service.h>
#include <lib/inspect/deprecated/expose.h>
#include <lib/syslog/cpp/logger.h>

namespace component {

void Property::Set(std::string value) {
  value_.emplace<std::string>(std::move(value));
}

void Property::Set(ByteVector value) {
  value_.emplace<ByteVector>(std::move(value));
}

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

Object::Object(std::string name) : name_(name) {
  FX_CHECK(std::find(name_.begin(), name_.end(), '\0') == name_.end())
      << "Object name cannot contain null bytes";
  bindings_.set_empty_set_handler([this] {
    std::shared_ptr<Object> self_if_bindings;
    std::lock_guard lock(mutex_);
    FX_DCHECK(self_if_bindings_);
    self_if_bindings = std::move(self_if_bindings_);
    self_if_bindings_ = nullptr;
    // The reference is dropped after the mutex is released, so the Object is
    // still alive when the mutex is released.
  });
}

void Object::AddBinding(fidl::InterfaceRequest<Inspect> chan) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!self_if_bindings_) {
    FX_DCHECK(bindings_.size() == 0);
    self_if_bindings_ = self_weak_ptr_.lock();
  }
  bindings_.AddBinding(this, std::move(chan));
}

void Object::ReadData(ReadDataCallback callback) { callback(ToFidl()); }

void Object::ListChildren(ListChildrenCallback callback) {
  StringOutputVector out;
  PopulateChildVector(&out);
  callback(std::move(out));
}

void Object::OpenChild(std::string name,
                       ::fidl::InterfaceRequest<Inspect> child_channel,
                       OpenChildCallback callback) {
  auto child = GetChild(name.data());
  if (!child) {
    callback(false);
    return;
  }

  child->AddBinding(std::move(child_channel));
  callback(true);
}

std::shared_ptr<Object> Object::GetChild(std::string name) {
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
  std::lock_guard<std::mutex> lock(mutex_);
  lazy_object_callback_ = std::move(callback);
}

void Object::ClearChildrenCallback() {
  std::lock_guard<std::mutex> lock(mutex_);
  ChildrenCallback temp;
  lazy_object_callback_.swap(temp);
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

void Object::PopulateChildVector(StringOutputVector* out_vector)
    __TA_EXCLUDES(mutex_) {
  // Lock the local child vector. No need to lock children since we are only
  // reading their constant name.
  std::lock_guard lock(mutex_);
  for (const auto& it : children_) {
    out_vector->push_back(it.second->name().data());
  }
  if (lazy_object_callback_) {
    ObjectVector lazy_objects;
    lazy_object_callback_(&lazy_objects);
    for (const auto& obj : lazy_objects) {
      out_vector->push_back(obj->name().data());
    }
  }
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

Object::StringOutputVector Object::GetChildren() {
  Object::StringOutputVector ret;
  PopulateChildVector(&ret);
  return ret;
}

}  // namespace component
