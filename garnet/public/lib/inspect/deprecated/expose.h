// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// !!! DEPRECATED !!!
// New usages should reference garnet/public/lib/inspect...

#ifndef LIB_INSPECT_DEPRECATED_EXPOSE_H_
#define LIB_INSPECT_DEPRECATED_EXPOSE_H_

#include <fs/lazy-dir.h>
#include <fs/pseudo-file.h>
#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>

#include <set>
#include <unordered_map>
#include <variant>

namespace component {

// Property is a string value associated with an |Object| belonging to a
// |Component|. The string value may be updated lazily at read time through the
// use of a callback.
//
// This class is not thread safe; concurrent accesses require external
// coordination.
class Property {
 public:
  using ByteVector = std::vector<uint8_t>;
  using StringValueCallback = fit::function<std::string()>;
  using VectorValueCallback = fit::function<ByteVector()>;

  // Constructs an empty property with string value "".
  Property() { Set(""); }

  // Constructs a property from a string.
  explicit Property(std::string value) { Set(std::move(value)); }
  explicit Property(ByteVector value) { Set(std::move(value)); }

  // Constructs a property with value set on each read by the given callback.
  explicit Property(StringValueCallback callback) { Set(std::move(callback)); }
  explicit Property(VectorValueCallback callback) { Set(std::move(callback)); }

  // Sets the property from a string.
  void Set(std::string value);
  void Set(ByteVector value);

  // Sets the property with value set on each read by the given callback.
  void Set(StringValueCallback callback);
  void Set(VectorValueCallback callback);

  fuchsia::inspect::Property ToFidl(const std::string& name) const;

 private:
  // Variants of possible values for this property.
  std::variant<std::string, ByteVector, StringValueCallback,
               VectorValueCallback>
      value_;
};

// Metric is a numeric value associated with an |Object| belonging to
// a |Component|.
// A Metric has a type, which is one of:
// INT:      int64_t
// UINT:     uint64_t
// DOUBLE:   double
// CALLBACK: Set by a callback function.
//
// Calling Set*() on a metric changes its type, but Add and Sub
// simply perform += or -= respectively, not changing the type of the
// Metric. This means the result of an operation will be cast back to the
// original type.
//
// This class is not thread safe; concurrent accesses require external
// coordination.
class Metric {
 public:
  using ValueCallback = fit::function<void(Metric*)>;
  enum Type { INT, UINT, DOUBLE, CALLBACK };

  // Constructs an INT metric with value 0.
  Metric() { SetInt(0); }

  // Constructs a metric set on read by the given callback.
  explicit Metric(ValueCallback callback) { SetCallback(std::move(callback)); }

  // Sets the type of this metric to INT with the given value.
  void SetInt(int64_t value);

  // Sets the type of this metric to UINT with the given value.
  void SetUInt(uint64_t value);

  // Sets the type of this metric to DOUBLE with the given value.
  void SetDouble(double value);

  // Sets the type of this metric to CALLBACK, where the given callback
  // is responsible for the value of this metric.
  void SetCallback(ValueCallback callback);

  // Gets the value of this metric as a string.
  std::string ToString() const;

  // Converts the value of this metric into its FIDL representation,
  // using the given name for the |name| field.
  fuchsia::inspect::Metric ToFidl(const std::string& name) const;

  // Adds a numeric type to the value of this metric. The type of
  // the metric will not be affected by this operation regardless of the
  // type passed in. Adding to a CALLBACK metric does nothing.
  template <typename T>
  void Add(T amount) {
    switch (type_) {
      case INT:
        int_value_ += static_cast<int64_t>(amount);
        break;
      case UINT:
        uint_value_ += static_cast<uint64_t>(amount);
        break;
      case DOUBLE:
        double_value_ += static_cast<double>(amount);
        break;
      case CALLBACK:
        break;
    }
  }

  // Subtracts a numeric type to the value of this metric. The type of
  // the metric will not be affected by this operation regardless of the
  // type passed in. Subtracting from a CALLBACK metric does nothing.
  template <typename T>
  void Sub(T amount) {
    switch (type_) {
      case INT:
        int_value_ -= static_cast<int64_t>(amount);
        break;
      case UINT:
        uint_value_ -= static_cast<uint64_t>(amount);
        break;
      case DOUBLE:
        double_value_ -= static_cast<double>(amount);
        break;
      case CALLBACK:
        break;
    }
  }

 private:
  // The current type of this metric.
  Type type_;
  // Union of 64-bit value types for the value of this metric.
  union {
    int64_t int_value_;
    uint64_t uint_value_;
    double double_value_;
  };
  // Callback to be used if type_ == CALLBACK.
  ValueCallback callback_;
};

Metric IntMetric(int64_t value);
Metric UIntMetric(uint64_t value);
Metric DoubleMetric(double value);
Metric CallbackMetric(Metric::ValueCallback callback);

// A component |Object| is any named entity that a component wishes to expose
// for inspection. An |Object| consists of any number of string |Property| and
// numeric |Metric| values. They may also have any number of uniquely named
// children. The set of children may be set dynamically at read time.
//
// |Object| implements the |Inspect| interface to expose its values and
// children over FIDL, and it implements |LazyDir| to expose the same over a
// file system.
//
// In the directory implementation, the special file `.channel` exposes a
// |Service| file to bind to the FIDL interface. The special file `.data`
// exposes the current values of the |Object| in a TAB-separated format for
// debugging. `.data` should be used strictly for debugging, and all user-facing
// utilities must communicate over the FIDL interface.
//
// This class is thread safe.
class Object : public fuchsia::inspect::Inspect {
 public:
  using ObjectVector = std::vector<std::shared_ptr<Object>>;
  using ChildrenCallback = fit::function<void(ObjectVector*)>;
  using StringOutputVector = fidl::VectorPtr<std::string>;

  // Makes a new shared pointer to an Object.
  static std::shared_ptr<Object> Make(std::string name) {
    auto ret = std::shared_ptr<Object>(new Object(std::move(name)));
    std::lock_guard<std::mutex> lock(ret->mutex_);
    ret->self_weak_ptr_ = ret;
    return ret;
  }

  // Gets the name of this |Object|.
  std::string name() { return name_; }

  // Gets a new reference to a child by name. The return value may be empty if
  // the child does not exist.
  std::shared_ptr<Object> GetChild(std::string name);

  // Sets a child to a new reference. If the child already exists, the contained
  // reference will be dropped and replaced with the given one.
  void SetChild(std::shared_ptr<Object> child);

  // Takes a child from this |Object|. This |Object| will no longer contain a
  // reference to the returned child. The return value may be empty if the child
  // does not exist.
  std::shared_ptr<Object> TakeChild(std::string name);

  // Sets a callback to dynamically populate children. The children returned by
  // this callback are in addition to the children already contained by this
  // |Object|.
  void SetChildrenCallback(ChildrenCallback callback);

  // Clears the callback for dynamic children. After calling this function, the
  // returned children will consist only of children contained by this object.
  void ClearChildrenCallback();

  // Remove a property from the object, returning true if it was found and
  // removed.
  bool RemoveProperty(const std::string& name);

  // Remove a metric from the object, returning true if it was found and
  // removed.
  bool RemoveMetric(const std::string& name);

  // Sets a |Property| on this |Object| to the given value.
  // The name of the property cannot include null bytes.
  bool SetProperty(const std::string& name, Property value);

  // Sets a |Metric| on this |Object| to the given value.
  // The name of the metric cannot include null bytes.
  bool SetMetric(const std::string& name, Metric metric);

  // Adds to a numeric |Metric| on this |Object|.
  template <typename T>
  bool AddMetric(const std::string& name, T amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = metrics_.find(name);
    if (it == metrics_.end()) {
      return false;
    }
    it->second.Add(amount);
    return true;
  }

  // Subtracts from a numeric |Metric| on this |Object|.
  template <typename T>
  bool SubMetric(const std::string& name, T amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = metrics_.find(name);
    if (it == metrics_.end()) {
      return false;
    }
    it->second.Sub(amount);
    return true;
  }

  // |Inspect| implementation

  // Reads local properties and metrics.
  void ReadData(ReadDataCallback callback) override;

  // Lists the children of this Object, including dynamic ones if they exist.
  void ListChildren(ListChildrenCallback callback) override;

  // Opens a channel with the requested child
  void OpenChild(std::string name,
                 ::fidl::InterfaceRequest<Inspect> child_channel,
                 OpenChildCallback callback) override;

  // Turn this |Object| into its FIDL representation.
  fuchsia::inspect::Object ToFidl();

  // Returns the names of this Object's children in a vector.
  StringOutputVector GetChildren();

 private:
  // Constructs a new |Object| with the given name.
  // Every object requires a name, and names for children must be unique.
  explicit Object(std::string name);

  // Helper function to populate an output vector of children objects.
  void PopulateChildVector(StringOutputVector* out_vector)
      __TA_EXCLUDES(mutex_);

  // Adds a new binding.
  void AddBinding(fidl::InterfaceRequest<Inspect> chan);

  // Mutex protecting fields below.
  mutable std::mutex mutex_;

  // The name of this object.
  std::string name_;

  // |Property| for this object, keyed by name.
  std::unordered_map<std::string, Property> properties_ __TA_GUARDED(mutex_);

  // |Property| for this object, keyed by name.
  std::unordered_map<std::string, Metric> metrics_ __TA_GUARDED(mutex_);

  // |Children| for this object, keyed by name. Ordered structure for consistent
  // iteration.
  std::map<std::string, std::shared_ptr<Object>> children_ __TA_GUARDED(mutex_);

  // Callback for retrieving lazily generated children. May be empty.
  ChildrenCallback lazy_object_callback_ __TA_GUARDED(mutex_);

  // The bindings for channels connected to this |Inspect|. The object itself is
  // owned by |self_if_bindings_| below.
  fidl::BindingSet<fuchsia::inspect::Inspect, Object*> bindings_
      __TA_GUARDED(mutex_);

  // A self shared_ptr, held only if |bindings_| is non-empty. Allows avoiding
  // circular ownership. Recursive destruction is impossible because the object
  // will not be destructed while pointing to itself.
  // TODO(FIDL-452): change when FIDL supports storing self shared ptrs.
  std::shared_ptr<Object> self_if_bindings_ __TA_GUARDED(mutex_);

  // A self weak_ptr, used to promote to a self shared_ptr when |bindings_| is
  // non-empty.
  std::weak_ptr<Object> self_weak_ptr_ __TA_GUARDED(mutex_);
};

}  // namespace component

#endif  // LIB_INSPECT_DEPRECATED_EXPOSE_H_
