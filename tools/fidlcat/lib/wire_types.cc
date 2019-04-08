// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_types.h"

#include <src/lib/fxl/logging.h>
#include <zircon/fidl.h>

#include "rapidjson/error/en.h"
#include "tools/fidlcat/lib/library_loader.h"

// See wire_types.h for details.

namespace fidlcat {

namespace {

// Objects are 8-byte aligned.  This adds 7 to the current offset and masks out
// the last three bits.
inline const uint8_t* AlignToNextWordBoundary(const uint8_t* offset) {
  const uintptr_t val = reinterpret_cast<const uintptr_t>(offset);
  return reinterpret_cast<uint8_t*>((val + 7) & (~7));
}

}  // namespace

bool ObjectTracker::RunCallbacksFrom(Marker& marker) {
  marker.AdvanceBytesTo(AlignToNextWordBoundary(marker.byte_pos()));
  if (!marker.is_valid()) {
    return false;
  }
  // We can't just iterate over the callbacks, because the callbacks may add
  // more callbacks.
  while (!callbacks_.empty()) {
    auto callback = std::move(callbacks_.front());
    callback(marker);
    if (!marker.is_valid()) {
      return false;
    }
    callbacks_.erase(callbacks_.begin());
  }
  return true;
}

void ObjectTracker::ObjectEnqueue(
    const std::string& key, ValueGeneratingCallback&& callback,
    rapidjson::Value& target_object,
    rapidjson::Document::AllocatorType& allocator) {
  callbacks_.push_back([this, cb = std::move(callback), &target_object,
                        key_string = key, &allocator](Marker& marker) {
    rapidjson::Value key;
    key.SetString(key_string.c_str(), allocator);

    rapidjson::Value& object =
        target_object.AddMember(key, rapidjson::Value(), allocator);
    cb(this, marker, object[key_string.c_str()], allocator);
  });
}

void ObjectTracker::ArrayEnqueue(
    ValueGeneratingCallback&& callback, rapidjson::Value& target_array,
    rapidjson::Document::AllocatorType& allocator) {
  callbacks_.push_back([this, cb = std::move(callback), &target_array,
                        &allocator](Marker& marker) {
    rapidjson::Value element;
    cb(this, marker, element, allocator);
    if (!marker.is_valid()) {
      return;
    }
    target_array.PushBack(element, allocator);
  });
}

void Marker::AdvanceBytesBy(size_t amount) {
  const uint8_t* pos = byte_pos_ + amount;
  AdvanceBytesTo(pos);
}

void Marker::AdvanceBytesTo(const uint8_t* pos) { byte_pos_ = pos; }

void Marker::AdvanceHandlesBy(size_t amount) {
  const zx_handle_t* pos = handle_pos_ + amount;
  AdvanceHandlesTo(pos);
}

void Marker::AdvanceHandlesTo(const zx_handle_t* pos) { handle_pos_ = pos; }

bool Marker::is_valid() const {
  return (end_byte_pos_ == nullptr || byte_pos_ <= end_byte_pos_) &&
         (end_byte_pos_ == nullptr || handle_pos_ <= end_handle_pos_);
}

// Prints out raw bytes as a C string of hex pairs ("af b0 1e...").  Useful for
// debugging / unknown data.
Marker UnknownType::GetValueCallback(Marker marker, size_t length,
                                     ObjectTracker* tracker,
                                     ValueGeneratingCallback& callback) const {
  const uint8_t* bytes = marker.byte_pos();
  marker.AdvanceBytesBy(length);
  if (marker.is_valid()) {
    callback = [length, bytes](ObjectTracker* tracker, Marker marker,
                               rapidjson::Value& value,
                               rapidjson::Document::AllocatorType& allocator) {
      size_t size = length * 3 + 1;
      char output[size];
      for (size_t i = 0; i < length; i++) {
        snprintf(output + (i * 3), 4, "%02x ", bytes[i]);
      }
      output[size - 2] = '\0';
      value.SetString(output, size, allocator);
      return;
    };
  }
  return marker;
}

bool Type::ValueEquals(Marker marker, size_t length,
                       const rapidjson::Value& value) const {
  FXL_LOG(FATAL) << "Equality operator for type not implemented";
  return false;
}

size_t Type::InlineSize() const {
  FXL_LOG(FATAL) << "Size for type not implemented";
  return 0;
}

Marker StringType::GetValueCallback(Marker marker, size_t length,
                                    ObjectTracker* tracker,
                                    ValueGeneratingCallback& callback) const {
  const uint8_t* bytes = marker.byte_pos();
  marker.AdvanceBytesBy(length);
  if (!marker.is_valid()) {
    return marker;
  }
  // Strings: First 8 bytes is length
  uint64_t string_length = internal::MemoryFrom<uint64_t>(bytes);
  // next 8 bytes are 0 if the string is null, and 0xffffffff otherwise.
  bool is_null = bytes[sizeof(uint64_t)] == 0x0;
  callback = [is_null, string_length](
                 ObjectTracker* tracker, Marker& marker,
                 rapidjson::Value& value,
                 rapidjson::Document::AllocatorType& allocator) {
    if (is_null) {
      value.SetString("(null)", allocator);
      return;
    }
    const uint8_t* bytes = marker.byte_pos();
    marker.AdvanceBytesTo(
        AlignToNextWordBoundary(marker.byte_pos() + string_length));
    if (marker.is_valid()) {
      // everything after that is the string.
      value.SetString(reinterpret_cast<const char*>(bytes), string_length,
                      allocator);
    }
    return;
  };
  return marker;
}

Marker BoolType::GetValueCallback(Marker marker, size_t length,
                                  ObjectTracker* tracker,
                                  ValueGeneratingCallback& callback) const {
  const uint8_t* bytes = marker.byte_pos();
  marker.AdvanceBytesBy(sizeof(bool));
  if (marker.is_valid()) {
    callback = [val = *bytes](ObjectTracker* tracker, Marker& marker,
                              rapidjson::Value& value,
                              rapidjson::Document::AllocatorType& allocator) {
      // assert that length == 1
      if (val) {
        value.SetString("true", allocator);
      } else {
        value.SetString("false", allocator);
      }
      return;
    };
  }
  return marker;
}

// This provides a Tracker for objects that may or may not be out-of-line.  If
// the object is in-line, it should use the Tracker provided by the outermost
// enclosing fixed length object (for example, an in-line struct embedded in the
// params should use the params's tracker).  If the object is out-of-line, then
// it needs its own tracker.  This class keeps track of the slightly different
// things you need to do in the two cases.
class TrackerMark {
 public:
  // Tracker should be present if this is an in-line object, and absent if it
  // isn't.
  TrackerMark(Marker marker, std::optional<ObjectTracker*> tracker,
              const Marker& end_)
      : marker_(marker), inner_tracker_(end_) {
    if (tracker) {
      tracker_ = *tracker;
    } else {
      tracker_ = &inner_tracker_;
    }
  }

  ObjectTracker* GetTracker() const { return tracker_; }

  // Run callbacks if this is an out-of-line object.
  Marker MaybeRunCallbacks(Marker end_marker) const {
    if (tracker_ == &inner_tracker_) {
      if (!tracker_->RunCallbacksFrom(end_marker)) {
        return end_marker;
      }
      end_marker.AdvanceBytesTo(AlignToNextWordBoundary(end_marker.byte_pos()));
      return end_marker;
    }
    return marker_;
  }

 private:
  Marker marker_;
  ObjectTracker* tracker_;
  ObjectTracker inner_tracker_;
};

StructType::StructType(const Struct& str, bool is_nullable)
    : struct_(str), is_nullable_(is_nullable) {}

Marker StructType::GetValueCallback(Marker marker, size_t length,
                                    ObjectTracker* tracker,
                                    ValueGeneratingCallback& callback) const {
  bool is_nullable = is_nullable_;
  const Struct& str = struct_;
  Marker inline_marker = marker;
  const uint8_t* bytes = marker.byte_pos();
  marker.AdvanceBytesBy(length);
  if (!marker.is_valid()) {
    return marker;
  }
  uintptr_t val = internal::MemoryFrom<uintptr_t>(bytes);
  if (is_nullable_) {
    if (val == 0) {
      callback = [](ObjectTracker* tracker, Marker& marker,
                    rapidjson::Value& value,
                    rapidjson::Document::AllocatorType& allocator) {
        value.SetNull();
        return;
      };

      return marker;
    }

    if (val != UINTPTR_MAX) {
      FXL_LOG(INFO) << "Illegally encoded struct.";
    }
  }
  callback = [inline_marker, str, is_nullable, tracker](
                 ObjectTracker* ignored, Marker& outline_marker,
                 rapidjson::Value& value,
                 rapidjson::Document::AllocatorType& allocator) {
    const Marker* mp;
    std::optional<ObjectTracker*> tracker_for_mark;
    if (is_nullable) {
      mp = &outline_marker;
      tracker_for_mark = std::nullopt;
    } else {
      mp = &inline_marker;
      tracker_for_mark = std::optional<ObjectTracker*>(tracker);
    }
    Marker marker = *mp;

    TrackerMark mark(outline_marker, tracker_for_mark, tracker->end());
    value.SetObject();
    ObjectTracker* tracker = mark.GetTracker();
    Marker prev_marker = marker;
    for (auto& member : str.members()) {
      std::unique_ptr<Type> member_type = member.GetType();
      ValueGeneratingCallback value_callback;
      Marker value_marker(marker.byte_pos() + member.offset(),
                          marker.handle_pos(), tracker->end());
      if (!value_marker.is_valid()) {
        outline_marker = value_marker;
        return;
      }

      value_marker.AdvanceHandlesTo(prev_marker.handle_pos());
      if (!value_marker.is_valid()) {
        outline_marker = value_marker;
        return;
      }

      prev_marker = member_type->GetValueCallback(value_marker, member.size(),
                                                  tracker, value_callback);
      tracker->ObjectEnqueue(member.name(), std::move(value_callback), value,
                             allocator);
    }
    marker.AdvanceBytesBy(str.size());
    if (!marker.is_valid()) {
      outline_marker = marker;
      return;
    }
    outline_marker = mark.MaybeRunCallbacks(marker);
  };
  return marker;
}

ElementSequenceType::ElementSequenceType(std::unique_ptr<Type>&& component_type)
    : component_type_(std::move(component_type)) {
  FXL_DCHECK(component_type_.get() != nullptr);
}

ElementSequenceType::ElementSequenceType(std::shared_ptr<Type> component_type)
    : component_type_(component_type) {
  FXL_DCHECK(component_type_.get() != nullptr);
}

ValueGeneratingCallback ElementSequenceType::GetIteratingCallback(
    ObjectTracker* tracker, size_t count, Marker marker, size_t length) const {
  std::shared_ptr<Type> component_type = component_type_;
  return [tracker, component_type, count, captured_marker = marker, length](
             ObjectTracker* ignored, Marker& inline_marker,
             rapidjson::Value& value,
             rapidjson::Document::AllocatorType& allocator) {
    value.SetArray();
    Marker marker = captured_marker;
    for (uint32_t i = 0; i < count; i++) {
      ValueGeneratingCallback value_callback;
      marker = component_type->GetValueCallback(marker, length / count, tracker,
                                                value_callback);
      if (!marker.is_valid()) {
        inline_marker = marker;
        return;
      }
      tracker->ArrayEnqueue(std::move(value_callback), value, allocator);
    }
  };
}

ArrayType::ArrayType(std::unique_ptr<Type>&& component_type, uint32_t count)
    : ElementSequenceType(std::move(component_type)), count_(count) {}

Marker ArrayType::GetValueCallback(Marker marker, size_t length,
                                   ObjectTracker* tracker,
                                   ValueGeneratingCallback& callback) const {
  callback = GetIteratingCallback(tracker, count_, marker, length);
  marker.AdvanceBytesBy(length);
  return marker;
}

VectorType::VectorType(std::unique_ptr<Type>&& component_type)
    : ElementSequenceType(std::move(component_type)) {}

VectorType::VectorType(std::shared_ptr<Type> component_type,
                       size_t element_size)
    : ElementSequenceType(component_type) {}

Marker VectorType::GetValueCallback(Marker marker, size_t length,
                                    ObjectTracker* tracker,
                                    ValueGeneratingCallback& callback) const {
  const uint8_t* bytes = marker.byte_pos();
  marker.AdvanceBytesBy(length);
  if (!marker.is_valid()) {
    return marker;
  }

  uint64_t count = internal::MemoryFrom<uint64_t>(bytes);
  uint64_t data = internal::MemoryFrom<uint64_t>(bytes + sizeof(uint64_t));
  size_t element_size = component_type_->InlineSize();
  if (data == UINTPTR_MAX) {
    VectorType vt(component_type_, element_size);
    callback = [vt, tracker, element_size, count](
                   ObjectTracker* ignored, Marker& marker,
                   rapidjson::Value& value,
                   rapidjson::Document::AllocatorType& allocator) {
      ValueGeneratingCallback value_cb =
          vt.GetIteratingCallback(tracker, count, marker, element_size * count);
      value_cb(tracker, marker, value, allocator);
      marker.AdvanceBytesBy(element_size * count);
    };
  } else if (data == 0) {
    callback =
        [](ObjectTracker* tracker, Marker& marker, rapidjson::Value& value,
           rapidjson::Document::AllocatorType& allocator) { value.SetNull(); };
  }
  return marker;
}

EnumType::EnumType(const Enum& e) : enum_(e) {}

Marker EnumType::GetValueCallback(Marker marker, size_t length,
                                  ObjectTracker* tracker,
                                  ValueGeneratingCallback& callback) const {
  std::string name = enum_.GetNameFromBytes(marker.byte_pos(), length);
  marker.AdvanceBytesBy(length);
  if (!marker.is_valid()) {
    return marker;
  }
  callback = [name](ObjectTracker* tracker, Marker& marker,
                    rapidjson::Value& value,
                    rapidjson::Document::AllocatorType& allocator) {
    value.SetString(name, allocator);
  };
  return marker;
}

Marker HandleType::GetValueCallback(Marker marker, size_t length,
                                    ObjectTracker* tracker,
                                    ValueGeneratingCallback& callback) const {
  const uint8_t* bytes = marker.byte_pos();
  marker.AdvanceBytesBy(sizeof(zx_handle_t));
  if (!marker.is_valid()) {
    return marker;
  }
  zx_handle_t val = internal::MemoryFrom<zx_handle_t>(bytes);
  if (val == FIDL_HANDLE_PRESENT) {
    // Handle is out-of-line
    callback = [](ObjectTracker* tracker, Marker& marker,
                  rapidjson::Value& value,
                  rapidjson::Document::AllocatorType& allocator) {
      const zx_handle_t* handles = marker.handle_pos();
      marker.AdvanceHandlesBy(1);
      if (!marker.is_valid()) {
        return;
      }
      zx_handle_t val = internal::MemoryFrom<zx_handle_t>(handles);
      value.SetString(std::to_string(val).c_str(), allocator);
      return;
    };
  } else if (val == FIDL_HANDLE_ABSENT) {
    callback = [val](ObjectTracker* tracker, Marker& marker,
                     rapidjson::Value& value,
                     rapidjson::Document::AllocatorType& allocator) {
      value.SetString(std::to_string(val).c_str(), allocator);
    };
  } else {
    FXL_LOG(INFO) << "Illegally encoded handle";
  }
  return marker;
}

std::unique_ptr<Type> Type::get_illegal() {
  return std::unique_ptr<Type>(new UnknownType());
}

std::unique_ptr<Type> Type::ScalarTypeFromName(const std::string& type_name) {
  static std::map<std::string, std::function<std::unique_ptr<Type>()>>
      scalar_type_map_{
          {"bool", []() { return std::make_unique<BoolType>(); }},
          {"float32", []() { return std::make_unique<Float32Type>(); }},
          {"float64", []() { return std::make_unique<Float64Type>(); }},
          {"int8", []() { return std::make_unique<Int8Type>(); }},
          {"int16", []() { return std::make_unique<Int16Type>(); }},
          {"int32", []() { return std::make_unique<Int32Type>(); }},
          {"int64", []() { return std::make_unique<Int64Type>(); }},
          {"uint8", []() { return std::make_unique<Uint8Type>(); }},
          {"uint16", []() { return std::make_unique<Uint16Type>(); }},
          {"uint32", []() { return std::make_unique<Uint32Type>(); }},
          {"uint64", []() { return std::make_unique<Uint64Type>(); }},
      };
  auto it = scalar_type_map_.find(type_name);
  if (it != scalar_type_map_.end()) {
    return it->second();
  }
  return Type::get_illegal();
}

std::unique_ptr<Type> Type::TypeFromPrimitive(const rapidjson::Value& type) {
  if (!type.HasMember("subtype")) {
    FXL_LOG(ERROR) << "Invalid type";
    return Type::get_illegal();
  }

  std::string subtype = type["subtype"].GetString();
  return ScalarTypeFromName(subtype);
}

std::unique_ptr<Type> Type::TypeFromIdentifier(const LibraryLoader& loader,
                                               const rapidjson::Value& type) {
  if (!type.HasMember("identifier")) {
    FXL_LOG(ERROR) << "Invalid type";
    return std::unique_ptr<Type>();
  }
  std::string id = type["identifier"].GetString();
  size_t split_index = id.find('/');
  std::string library_name = id.substr(0, split_index);
  const Library* library;
  if (!loader.GetLibraryFromName(library_name, &library)) {
    FXL_LOG(ERROR) << "Unknown type for identifier: " << library_name;
    // TODO: Something else here
    return std::unique_ptr<Type>();
  }

  bool is_nullable = false;
  if (type.HasMember("nullable")) {
    is_nullable = type["nullable"].GetBool();
  }
  return library->TypeFromIdentifier(is_nullable, id);
}

std::unique_ptr<Type> Type::GetType(const LibraryLoader& loader,
                                    const rapidjson::Value& type) {
  // TODO: This is creating a new type every time we need one.  That's pretty
  // inefficient.  Find a way of caching them if it becomes a problem.
  if (!type.HasMember("kind")) {
    FXL_LOG(ERROR) << "Invalid type";
    return Type::get_illegal();
  }
  std::string kind = type["kind"].GetString();
  if (kind == "array") {
    const rapidjson::Value& element_type = type["element_type"];
    uint32_t element_count =
        std::strtol(type["element_count"].GetString(), nullptr, 10);
    return std::make_unique<ArrayType>(GetType(loader, element_type),
                                       element_count);
  } else if (kind == "vector") {
    const rapidjson::Value& element_type = type["element_type"];
    return std::make_unique<VectorType>(GetType(loader, element_type));
  } else if (kind == "string") {
    return std::make_unique<StringType>();
  } else if (kind == "handle") {
    return std::make_unique<HandleType>();
  } else if (kind == "request") {
    // TODO: implement something useful.
    return std::make_unique<NumericType<uint32_t>>();
  } else if (kind == "primitive") {
    return Type::TypeFromPrimitive(type);
  } else if (kind == "identifier") {
    return Type::TypeFromIdentifier(loader, type);
  }
  FXL_LOG(ERROR) << "Invalid type " << kind;
  return get_illegal();
}

}  // namespace fidlcat
