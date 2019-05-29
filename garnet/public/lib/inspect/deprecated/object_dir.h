// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// !!! DEPRECATED !!!
// New usages should reference garnet/public/lib/inspect...

#ifndef LIB_INSPECT_DEPRECATED_OBJECT_DIR_H_
#define LIB_INSPECT_DEPRECATED_OBJECT_DIR_H_

#include <lib/inspect/deprecated/expose.h>

namespace component {

// An |ObjectPath| describes a specific path between children objects, which may
// be defined statically within a file to impose type-safety for find()
// operations.
//
// Example:
// const ObjectPath CONTENTS = {"container", "child", "contents"};
// obj->find(CONTENTS);
using ObjectPath = std::initializer_list<const char*>;

// |ObjectDir| is a wrapper class around the raw |Object| class.
// While |Object| deals with the individual properties, metrics, and children
// related to a single |Object|, |ObjectDir| provides a lightweight wrapper
// around the |Object| interface to support higher-level operations, including:
// * Tree traversal
// * Property setters
// * Metric setters
// * Children setters/getters
//
// This class is thread-safe; it simply wraps a single |Object|, which is itself
// thread-safe.
class ObjectDir {
 public:
  // Constructs an empty |ObjectDir|, satisfying !!(*this) == false.
  ObjectDir();

  // Constructs an |ObjectDir| wrapping the given |Object|.
  explicit ObjectDir(std::shared_ptr<Object> object);

  // Construct a new |ObjectDir| wrapping a new Object with the given name.
  static ObjectDir Make(std::string name) {
    return ObjectDir(Object::Make(std::move(name)));
  }

  // The boolean value of an |ObjectDir| is true if and only if the wrapped
  // object reference is not empty.
  operator bool() const { return object_.get(); }

  // Obtains a reference to the wrapped |Object|.
  std::shared_ptr<Object> object() const { return object_; }

  std::string name() const { return (*this) ? object_->name() : ""; }

  // Finds a child |Object| by path, and wraps it in an |ObjectDir|.
  // If |initialize| is true, this method initialized all objects along the path
  // that do not exist. Otherwise, it returns an empty |ObjectDir| if any
  // |Object| along the path does not exist.
  ObjectDir find(ObjectPath path, bool initialize = true) const;

  // Sets a property on this object to the given value.
  template <typename T>
  bool set_prop(std::string name, T value) const {
    return inner_set_prop({}, std::move(name), Property(std::move(value)));
  }

  // Sets a property on the child specified by path to the given value.
  // All objects along the path that do not exist will be initialized.
  template <typename T>
  typename std::enable_if_t<std::is_lvalue_reference<T>::value, bool>::type
  set_prop(ObjectPath path, std::string name, T value) const {
    return inner_set_prop(std::move(path), std::move(name),
                          Property(std::move(value)));
  }

  // Sets a metric on this object to the given value.
  bool set_metric(std::string name, Metric metric) const {
    return set_metric({}, std::move(name), std::move(metric));
  }

  // Sets a metric on the child specified by path to use the given value.
  // All objects along the path that do not exist will be initialized.
  bool set_metric(ObjectPath path, std::string name, Metric metric) const;

  // Adds to a metric on this object.
  template <typename T>
  bool add_metric(std::string name, T amount) const {
    return add_metric({}, std::move(name), amount);
  }

  // Subtracts from a metric on this object.
  template <typename T>
  bool sub_metric(std::string name, T amount) const {
    return sub_metric({}, std::move(name), amount);
  }

  // Adds to a metric on a child specified by path.
  // All objects along the path that do not exist will be initialized.
  template <typename T>
  bool add_metric(ObjectPath path, std::string name, T amount) const {
    return object_ ? find(path).object()->AddMetric(name, amount) : false;
  }

  // Subtracts from a metric on a child specified by path.
  // All objects along the path that do not exist will be initialized.
  template <typename T>
  bool sub_metric(ObjectPath path, std::string name, T amount) const {
    return object_ ? find(path).object()->SubMetric(name, amount) : false;
  }

  // Sets a child on this object to the given object.
  void set_child(std::shared_ptr<Object> obj) const {
    set_child({}, std::move(obj));
  }

  // Sets a child on the child specified by path to the given object.
  // All objects along the path that do not exist will be initialized.
  void set_child(ObjectPath path, std::shared_ptr<Object> obj) const;

  // Sets the dynamic child callback on this object.
  void set_children_callback(Object::ChildrenCallback callback) const {
    set_children_callback({}, std::move(callback));
  }

  // Sets the dynamic child callback on the child specified by path.
  // All objects along the path that do not exist will be initialized.
  void set_children_callback(ObjectPath path,
                             Object::ChildrenCallback callback) const;

 private:
  // Inner implementation of setting properties by path.
  bool inner_set_prop(ObjectPath path, std::string name,
                      Property property) const;

  // The wrapper object.
  std::shared_ptr<Object> object_;
};

}  // namespace component

#endif  // LIB_INSPECT_DEPRECATED_OBJECT_DIR_H_
