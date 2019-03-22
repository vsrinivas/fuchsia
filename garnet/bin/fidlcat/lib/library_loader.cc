// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "library_loader.h"

#include <lib/fxl/logging.h>

#include <endian.h>

#include "rapidjson/error/en.h"

// See library_loader.h for details.

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

namespace {

// This namespace contains implementations of EqualityFunctions and
// PrintFunctions for built in FIDL types.  These can be provided to
// fidlcat::Type's constructor to generate a Type object for the given type.

// These are convenience functions for reading little endian (i.e., FIDL wire
// format encoded) bits.
template <typename T>
class LeToHost {
 public:
  static T le_to_host(const T* ts);
};

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

template <typename T>
struct GetUnsigned {
  using type = typename std::conditional<std::is_same<float, T>::value,
                                         uint32_t, uint64_t>::type;
};

template <typename T>
T MemoryFrom(const uint8_t* bytes) {
  using U = typename std::conditional<std::is_integral<T>::value,
                                      std::make_unsigned<T>,
                                      GetUnsigned<T>>::type::type;
  union {
    U uval;
    T tval;
  } u;
  u.uval = LeToHost<U>::le_to_host(reinterpret_cast<const U*>(bytes));
  return u.tval;
}

// Prints out raw bytes as a C string of hex pairs ("af b0 1e...").  Useful for
// debugging / unknown data.
size_t UnknownPrint(const uint8_t* bytes, size_t length, ObjectTracker* tracker,
                    ValueGeneratingCallback& callback,
                    rapidjson::Document::AllocatorType& allocator) {
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

size_t StringPrint(const uint8_t* bytes, size_t length, ObjectTracker* tracker,
                   ValueGeneratingCallback& callback,
                   rapidjson::Document::AllocatorType& allocator) {
  // Strings: First 8 bytes is length
  uint64_t string_length = MemoryFrom<uint64_t>(bytes);
  // next 8 bytes are 0 if the string is null, and 0xffffffff otherwise.
  bool is_null = bytes[8] == 0x0;
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

bool DummyEq(const uint8_t* bytes, size_t length,
             const rapidjson::Value& value) {
  FXL_LOG(FATAL) << "Equality operator for type not implemented";
  return false;
}

size_t BoolPrint(const uint8_t* bytes, size_t length, ObjectTracker* tracker,
                 ValueGeneratingCallback& callback,
                 rapidjson::Document::AllocatorType& allocator) {
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

// A generic PrintFunction that can be used for any scalar type.
template <typename T>
size_t PrimitivePrint(const uint8_t* bytes, size_t length,
                      ObjectTracker* tracker, ValueGeneratingCallback& callback,
                      rapidjson::Document::AllocatorType& allocator) {
  T val = MemoryFrom<T>(bytes);
  callback = [val](const uint8_t* bytes, rapidjson::Value& value,
                   rapidjson::Document::AllocatorType& allocator) {
    value.SetString(std::to_string(val).c_str(), allocator);
    return 0;
  };
  return sizeof(T);
}

// A generic EqualityFunction that can be used for any scalar type.
template <typename T>
size_t PrimitiveEq(const uint8_t* bytes, size_t length,
                   const rapidjson::Value& value) {
  T lhs = MemoryFrom<T>(bytes);
  std::istringstream input(value["value"].GetString());
  // Because int8_t is really char, and we don't want to read that.
  using R =
      typename std::conditional<std::is_same<T, int8_t>::value, int, T>::type;
  R rhs;
  input >> rhs;
  return lhs == rhs;
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

size_t StructPrint(const Struct& str, bool is_nullable, const uint8_t* bytes,
                   size_t length, ObjectTracker* tracker,
                   ValueGeneratingCallback& callback,
                   rapidjson::Document::AllocatorType& allocator) {
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
      Type member_type = member.GetType();
      ValueGeneratingCallback value_callback;
      member_type.MakeValue(&bytes[member.offset()], member.size(), tracker,
                            value_callback, allocator);
      tracker->ObjectEnqueue(member.name(), std::move(value_callback), value,
                             allocator);
    }
    return mark.MaybeRunCallbacks(str.size());
  };
  return length;
}

size_t ArrayPrint(Type type, uint32_t count, const uint8_t* bytes,
                  size_t length, ObjectTracker* tracker,
                  ValueGeneratingCallback& callback,
                  rapidjson::Document::AllocatorType& allocator) {
  callback = [tracker, type, count, bytes, length](
                 const uint8_t* ignored, rapidjson::Value& value,
                 rapidjson::Document::AllocatorType& allocator) {
    value.SetArray();
    size_t offset = 0;
    for (uint32_t i = 0; i < count; i++) {
      ValueGeneratingCallback value_callback;
      offset += type.MakeValue(bytes + offset, length / count, tracker,
                               value_callback, allocator);
      tracker->ArrayEnqueue(std::move(value_callback), value, allocator);
    }
    return 0;
  };
  return length;
}

size_t VectorPrint(Type type, size_t element_size, const uint8_t* bytes,
                   size_t length, ObjectTracker* tracker,
                   ValueGeneratingCallback& callback,
                   rapidjson::Document::AllocatorType& allocator) {
  uint64_t count = MemoryFrom<uint64_t>(bytes);
  uint64_t data = MemoryFrom<uint64_t>(bytes + sizeof(uint64_t));
  if (data == UINTPTR_MAX) {
    callback = [tracker, type, count, element_size](
                   const uint8_t* bytes, rapidjson::Value& value,
                   rapidjson::Document::AllocatorType& allocator) {
      ValueGeneratingCallback callback;
      ArrayPrint(type, count, bytes, element_size * count, tracker, callback,
                 allocator);
      callback(bytes, value, allocator);
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

size_t EnumPrint(const Enum& e, const uint8_t* bytes, size_t length,
                 ObjectTracker* tracker, ValueGeneratingCallback& callback,
                 rapidjson::Document::AllocatorType& allocator) {
  std::string name = e.GetNameFromBytes(bytes, length);
  callback = [name](const uint8_t* bytes, rapidjson::Value& value,
                    rapidjson::Document::AllocatorType& allocator) {
    value.SetString(name, allocator);
    return 0;
  };
  return length;
}

}  // anonymous namespace

uint32_t Enum::size() const {
  return Type::InlineSizeFromType(enclosing_library_.enclosing_loader(),
                                  value_);
}

const Type& Type::get_illegal() {
  static Type illegal;
  return illegal;
}

Type Type::ScalarTypeFromName(const std::string& type_name) {
  static std::map<std::string, Type> scalar_type_map_{
      {"bool", Type(BoolPrint, DummyEq)},
      {"float32", Type(PrimitivePrint<float>, DummyEq)},
      {"float64", Type(PrimitivePrint<double>, DummyEq)},
      {"int8", Type(PrimitivePrint<int8_t>, PrimitiveEq<int8_t>)},
      {"int16", Type(PrimitivePrint<int16_t>, PrimitiveEq<int16_t>)},
      {"int32", Type(PrimitivePrint<int32_t>, PrimitiveEq<int32_t>)},
      {"int64", Type(PrimitivePrint<int64_t>, PrimitiveEq<int64_t>)},
      {"uint8", Type(PrimitivePrint<uint8_t>, PrimitiveEq<uint8_t>)},
      {"uint16", Type(PrimitivePrint<uint16_t>, PrimitiveEq<uint16_t>)},
      {"uint32", Type(PrimitivePrint<uint32_t>, PrimitiveEq<uint32_t>)},
      {"uint64", Type(PrimitivePrint<uint64_t>, PrimitiveEq<uint64_t>)},
  };
  auto it = scalar_type_map_.find(type_name);
  if (it != scalar_type_map_.end()) {
    return it->second;
  }
  return Type::get_illegal();
}

Type Type::TypeFromPrimitive(const rapidjson::Value& type) {
  if (!type.HasMember("subtype")) {
    FXL_LOG(ERROR) << "Invalid type";
    return Type(UnknownPrint, DummyEq);
  }

  std::string subtype = type["subtype"].GetString();
  return ScalarTypeFromName(subtype);
}

Type Library::TypeFromIdentifier(bool is_nullable,
                                 std::string& identifier) const {
  using std::placeholders::_1;
  using std::placeholders::_2;
  using std::placeholders::_3;
  using std::placeholders::_4;
  using std::placeholders::_5;

  auto str = structs_.find(identifier);
  if (str != structs_.end()) {
    return Type(std::bind(StructPrint, std::ref(str->second), is_nullable, _1,
                          _2, _3, _4, _5),
                DummyEq);
  }
  auto enu = enums_.find(identifier);
  if (enu != enums_.end()) {
    return Type(std::bind(EnumPrint, std::ref(enu->second), _1, _2, _3, _4, _5),
                DummyEq);
  }
  // And probably for unions and tables.
  return Type::get_illegal();
}

Type Type::TypeFromIdentifier(const LibraryLoader& loader,
                              const rapidjson::Value& type) {
  if (!type.HasMember("identifier")) {
    FXL_LOG(ERROR) << "Invalid type";
    return Type(UnknownPrint, DummyEq);
  }
  std::string id = type["identifier"].GetString();
  size_t split_index = id.find('/');
  std::string library_name = id.substr(0, split_index);
  const Library* library;
  if (!loader.GetLibraryFromName(library_name, &library)) {
    FXL_LOG(ERROR) << "Unknown type for identifier: " << library_name;
    // TODO: Something else here
    return Type::get_illegal();
  }

  bool is_nullable = false;
  if (type.HasMember("nullable")) {
    is_nullable = type["nullable"].GetBool();
  }
  return library->TypeFromIdentifier(is_nullable, id);
}

Type InterfaceMethodParameter::GetType() const {
  if (!value_.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    // TODO: something else here.
    // Probably print out raw bytes instead.
    return Type::get_illegal();
  }
  const rapidjson::Value& type = value_["type"];
  return Type::GetType(enclosing_method_.enclosing_interface()
                           .enclosing_library()
                           .enclosing_loader(),
                       type);
}

Type StructMember::GetType() const {
  if (!value_.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    // TODO: something else here.
    // Probably print out raw bytes instead.
    return Type::get_illegal();
  }
  const rapidjson::Value& type = value_["type"];
  return Type::GetType(
      enclosing_struct().enclosing_library().enclosing_loader(), type);
}

size_t Library::InlineSizeFromIdentifier(std::string& identifier) const {
  auto str = structs_.find(identifier);
  if (str != structs_.end()) {
    return str->second.size();
  }
  auto enu = enums_.find(identifier);
  if (enu != enums_.end()) {
    return enu->second.size();
  }
  // And probably for unions and tables.
  return 0;
}

size_t Type::InlineSizeFromIdentifier(const LibraryLoader& loader,
                                      const rapidjson::Value& type) {
  if (!type.HasMember("identifier")) {
    FXL_LOG(ERROR) << "Invalid type";
    return 0;
  }
  std::string id = type["identifier"].GetString();
  size_t split_index = id.find('/');
  std::string library_name = id.substr(0, split_index);
  const Library* library;
  if (!loader.GetLibraryFromName(library_name, &library)) {
    FXL_LOG(ERROR) << "Unknown type for identifier: " << library_name;
    // TODO: Something else here
    return 0;
  }

  return library->InlineSizeFromIdentifier(id);
}

size_t Type::InlineSizeFromType(const LibraryLoader& loader,
                                const rapidjson::Value& type) {
  std::string kind = type["kind"].GetString();
  if (kind == "array") {
    const rapidjson::Value& element_type = type["element_type"];
    uint32_t element_count =
        std::strtol(type["element_count"].GetString(), nullptr, 10);
    return InlineSizeFromType(loader, element_type) * element_count;
  } else if (kind == "vector") {
    // size + data
    return sizeof(uint64_t) + sizeof(uint64_t);
  } else if (kind == "string") {
    return sizeof(uint64_t) + sizeof(uint64_t);
  } else if (kind == "handle") {
    return sizeof(uint32_t);
  } else if (kind == "request") {
    return sizeof(uint32_t);
  } else if (kind == "primitive") {
    std::string subtype = type["subtype"].GetString();
    static std::map<std::string, size_t> scalar_size_map_{
        {"bool", sizeof(uint8_t)},     {"float32", sizeof(uint32_t)},
        {"float64", sizeof(uint64_t)}, {"int8", sizeof(int8_t)},
        {"int16", sizeof(int16_t)},    {"int32", sizeof(int32_t)},
        {"int64", sizeof(int64_t)},    {"uint8", sizeof(uint8_t)},
        {"uint16", sizeof(uint16_t)},  {"uint32", sizeof(uint32_t)},
        {"uint64", sizeof(uint64_t)},
    };
    auto it = scalar_size_map_.find(subtype);
    if (it != scalar_size_map_.end()) {
      return it->second;
    }
  } else if (kind == "identifier") {
    return InlineSizeFromIdentifier(loader, type);
  }
  FXL_LOG(ERROR) << "Invalid type " << kind;
  return 0;
}

Type Type::GetType(const LibraryLoader& loader, const rapidjson::Value& type) {
  using std::placeholders::_1;
  using std::placeholders::_2;
  using std::placeholders::_3;
  using std::placeholders::_4;
  using std::placeholders::_5;

  if (!type.HasMember("kind")) {
    FXL_LOG(ERROR) << "Invalid type";
    return Type(UnknownPrint, DummyEq);
  }
  std::string kind = type["kind"].GetString();
  if (kind == "array") {
    const rapidjson::Value& element_type = type["element_type"];
    uint32_t element_count =
        std::strtol(type["element_count"].GetString(), nullptr, 10);
    return Type(std::bind(ArrayPrint, GetType(loader, element_type),
                          element_count, _1, _2, _3, _4, _5),
                DummyEq);

  } else if (kind == "vector") {
    const rapidjson::Value& element_type = type["element_type"];
    const size_t element_size = InlineSizeFromType(loader, element_type);
    using namespace std::placeholders;
    return Type(std::bind(VectorPrint, GetType(loader, element_type),
                          element_size, _1, _2, _3, _4, _5),
                DummyEq);
  } else if (kind == "string") {
    return Type(StringPrint, DummyEq);
  } else if (kind == "handle") {
    // TODO: implement something useful.
    return Type(PrimitivePrint<uint32_t>, DummyEq);
  } else if (kind == "request") {
    // TODO: implement something useful.
    return Type(PrimitivePrint<uint32_t>, DummyEq);
  } else if (kind == "primitive") {
    return Type::TypeFromPrimitive(type);
  } else if (kind == "identifier") {
    return Type::TypeFromIdentifier(loader, type);
  }
  FXL_LOG(ERROR) << "Invalid type " << kind;
  return Type(UnknownPrint, DummyEq);
}

LibraryLoader::LibraryLoader(
    std::vector<std::unique_ptr<std::istream>>& library_streams,
    LibraryReadError* err) {
  err->value = LibraryReadError ::kOk;
  for (size_t i = 0; i < library_streams.size(); i++) {
    std::string ir(std::istreambuf_iterator<char>(*library_streams[i]), {});
    if (library_streams[i]->fail()) {
      err->value = LibraryReadError ::kIoError;
      return;
    }
    Add(ir, err);
    if (err->value != LibraryReadError ::kOk) {
      FXL_LOG(ERROR) << "JSON parse error: "
                     << rapidjson::GetParseError_En(err->parse_result.Code())
                     << " at offset " << err->parse_result.Offset();
      return;
    }
  }
}

}  // namespace fidlcat
