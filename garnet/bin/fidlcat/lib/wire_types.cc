// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wire_types.h"

#include <endian.h>
#include <src/lib/fxl/logging.h>

#include "garnet/bin/fidlcat/lib/library_loader.h"
#include "rapidjson/error/en.h"

// See wire_types.h for details.

namespace fidlcat {

namespace {

// Objects are 8-byte aligned.  This adds 7 to the current offset and masks out
// the last three bits.
inline size_t AlignToNextWordBoundary(size_t offset) {
  return (offset + 7) & (~7);
}

}  // namespace

size_t ObjectTracker::RunCallbacksFrom(size_t offset) {
  offset = AlignToNextWordBoundary(offset);
  // We can't just iterate over the callbacks, because the callbacks may add
  // more callbacks.
  while (!callbacks_.empty()) {
    auto callback = std::move(callbacks_.front());
    offset += callback(bytes_ + offset);
    callbacks_.erase(callbacks_.begin());
  }
  return offset;
}

void ObjectTracker::ObjectEnqueue(
    const std::string& key, ValueGeneratingCallback&& callback,
    rapidjson::Value& target_object,
    rapidjson::Document::AllocatorType& allocator) {
  callbacks_.push_back([cb = std::move(callback), &target_object,
                        key_string = key, &allocator](const uint8_t* bytes) {
    rapidjson::Value key;
    key.SetString(key_string.c_str(), allocator);

    rapidjson::Value& object =
        target_object.AddMember(key, rapidjson::Value(), allocator);
    size_t new_offset = cb(bytes, object[key_string.c_str()], allocator);

    return new_offset;
  });
}

void ObjectTracker::ArrayEnqueue(
    ValueGeneratingCallback&& callback, rapidjson::Value& target_array,
    rapidjson::Document::AllocatorType& allocator) {
  callbacks_.push_back([cb = std::move(callback), &target_array,
                        &allocator](const uint8_t* bytes) {
    rapidjson::Value element;
    size_t new_offset = cb(bytes, element, allocator);

    target_array.PushBack(element, allocator);
    return new_offset;
  });
}

namespace internal {

template <>
uint8_t LeToHost<uint8_t>::le_to_host(const uint8_t* bytes) {
  return *bytes;
}

template <>
uint16_t LeToHost<uint16_t>::le_to_host(const uint16_t* bytes) {
  return le16toh(*bytes);
}

template <>
uint32_t LeToHost<uint32_t>::le_to_host(const uint32_t* bytes) {
  return le32toh(*bytes);
}

template <>
uint64_t LeToHost<uint64_t>::le_to_host(const uint64_t* bytes) {
  return le64toh(*bytes);
}

}  // namespace internal

// Prints out raw bytes as a C string of hex pairs ("af b0 1e...").  Useful for
// debugging / unknown data.
size_t UnknownType::GetValueCallback(const uint8_t* bytes, size_t length,
                                     ObjectTracker* tracker,
                                     ValueGeneratingCallback& callback) const {
  callback = [length, bytes](const uint8_t* ignored, rapidjson::Value& value,
                             rapidjson::Document::AllocatorType& allocator) {
    size_t size = length * 3 + 1;
    char output[size];
    for (size_t i = 0; i < length; i++) {
      snprintf(output + (i * 3), 4, "%02x ", bytes[i]);
    }
    output[size - 2] = '\0';
    value.SetString(output, size, allocator);
    return 0;
  };
  return length;
}

bool Type::ValueEquals(const uint8_t* bytes, size_t length,
                       const rapidjson::Value& value) const {
  FXL_LOG(FATAL) << "Equality operator for type not implemented";
  return false;
}

size_t Type::InlineSize() const {
  FXL_LOG(FATAL) << "Size for type not implemented";
  return 0;
}

size_t StringType::GetValueCallback(const uint8_t* bytes, size_t length,
                                    ObjectTracker* tracker,
                                    ValueGeneratingCallback& callback) const {
  // Strings: First 8 bytes is length
  uint64_t string_length = internal::MemoryFrom<uint64_t>(bytes);
  // next 8 bytes are 0 if the string is null, and 0xffffffff otherwise.
  bool is_null = bytes[sizeof(uint64_t)] == 0x0;
  callback = [is_null, string_length](
                 const uint8_t* bytes, rapidjson::Value& value,
                 rapidjson::Document::AllocatorType& allocator) {
    if (is_null) {
      value.SetString("(null)", allocator);
      return 0UL;
    }
    // everything after that is the string.
    value.SetString(reinterpret_cast<const char*>(bytes), string_length,
                    allocator);
    return AlignToNextWordBoundary(string_length);
  };
  return length;
}

size_t BoolType::GetValueCallback(const uint8_t* bytes, size_t length,
                                  ObjectTracker* tracker,
                                  ValueGeneratingCallback& callback) const {
  callback = [val = *bytes](const uint8_t* bytes, rapidjson::Value& value,
                            rapidjson::Document::AllocatorType& allocator) {
    // assert that length == 1
    if (val) {
      value.SetString("true", allocator);
    } else {
      value.SetString("false", allocator);
    }
    return 0;
  };
  return sizeof(bool);
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
  TrackerMark(const uint8_t* bytes, std::optional<ObjectTracker*> tracker)
      : inner_tracker_(bytes) {
    if (tracker) {
      tracker_ = *tracker;
    } else {
      tracker_ = &inner_tracker_;
    }
  }

  ObjectTracker* GetTracker() const { return tracker_; }

  // Run callbacks if this is an out-of-line object.
  size_t MaybeRunCallbacks(size_t size) const {
    if (tracker_ == &inner_tracker_) {
      return AlignToNextWordBoundary(tracker_->RunCallbacksFrom(size));
    }
    // The out-of-line size for an inline object is 0.
    return 0;
  }

 private:
  ObjectTracker* tracker_;
  ObjectTracker inner_tracker_;
};

StructType::StructType(const Struct& str, bool is_nullable)
    : struct_(str), is_nullable_(is_nullable) {}

size_t StructType::GetValueCallback(const uint8_t* bytes, size_t length,
                                    ObjectTracker* tracker,
                                    ValueGeneratingCallback& callback) const {
  bool is_nullable = is_nullable_;
  const Struct& str = struct_;
  callback = [inline_bytes = bytes, str, is_nullable, tracker](
                 const uint8_t* outline_bytes, rapidjson::Value& value,
                 rapidjson::Document::AllocatorType& allocator) -> size_t {
    const uint8_t* bytes;
    std::optional<ObjectTracker*> tracker_for_mark;
    if (is_nullable) {
      bytes = outline_bytes;
      tracker_for_mark = std::nullopt;
    } else {
      bytes = inline_bytes;
      tracker_for_mark = std::optional<ObjectTracker*>(tracker);
    }

    TrackerMark mark(bytes, tracker_for_mark);
    value.SetObject();
    ObjectTracker* tracker = mark.GetTracker();
    for (auto& member : str.members()) {
      std::unique_ptr<Type> member_type = member.GetType();
      ValueGeneratingCallback value_callback;
      member_type->GetValueCallback(&bytes[member.offset()], member.size(),
                                    tracker, value_callback);
      tracker->ObjectEnqueue(member.name(), std::move(value_callback), value,
                             allocator);
    }
    return mark.MaybeRunCallbacks(str.size());
  };
  return length;
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
    ObjectTracker* tracker, size_t count, const uint8_t* bytes,
    size_t length) const {
  std::shared_ptr<Type> component_type = component_type_;
  return [tracker, component_type, count, bytes, length](
             const uint8_t* ignored, rapidjson::Value& value,
             rapidjson::Document::AllocatorType& allocator) {
    value.SetArray();
    size_t offset = 0;
    for (uint32_t i = 0; i < count; i++) {
      ValueGeneratingCallback value_callback;
      offset += component_type->GetValueCallback(bytes + offset, length / count,
                                                 tracker, value_callback);
      tracker->ArrayEnqueue(std::move(value_callback), value, allocator);
    }
    return 0;
  };
}

ArrayType::ArrayType(std::unique_ptr<Type>&& component_type, uint32_t count)
    : ElementSequenceType(std::move(component_type)), count_(count) {}

size_t ArrayType::GetValueCallback(const uint8_t* bytes, size_t length,
                                   ObjectTracker* tracker,
                                   ValueGeneratingCallback& callback) const {
  callback = GetIteratingCallback(tracker, count_, bytes, length);
  return length;
}

VectorType::VectorType(std::unique_ptr<Type>&& component_type)
    : ElementSequenceType(std::move(component_type)) {}

VectorType::VectorType(std::shared_ptr<Type> component_type,
                       size_t element_size)
    : ElementSequenceType(component_type) {}

size_t VectorType::GetValueCallback(const uint8_t* bytes, size_t length,
                                    ObjectTracker* tracker,
                                    ValueGeneratingCallback& callback) const {
  uint64_t count = internal::MemoryFrom<uint64_t>(bytes);
  uint64_t data = internal::MemoryFrom<uint64_t>(bytes + sizeof(uint64_t));
  size_t element_size = component_type_->InlineSize();
  if (data == UINTPTR_MAX) {
    VectorType vt(component_type_, element_size);
    callback = [vt, tracker, element_size, count](
                   const uint8_t* bytes, rapidjson::Value& value,
                   rapidjson::Document::AllocatorType& allocator) {
      ValueGeneratingCallback value_cb =
          vt.GetIteratingCallback(tracker, count, bytes, element_size * count);
      value_cb(bytes, value, allocator);
      return element_size * count;
    };
  } else if (data == 0) {
    callback = [](const uint8_t* bytes, rapidjson::Value& value,
                  rapidjson::Document::AllocatorType& allocator) {
      value.SetNull();
      return 0;
    };
  }
  return length;
}

EnumType::EnumType(const Enum& e) : enum_(e) {}

size_t EnumType::GetValueCallback(const uint8_t* bytes, size_t length,
                                  ObjectTracker* tracker,
                                  ValueGeneratingCallback& callback) const {
  std::string name = enum_.GetNameFromBytes(bytes, length);
  callback = [name](const uint8_t* bytes, rapidjson::Value& value,
                    rapidjson::Document::AllocatorType& allocator) {
    value.SetString(name, allocator);
    return 0;
  };
  return length;
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
    // TODO: implement something useful.
    return std::make_unique<NumericType<uint32_t>>();
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
