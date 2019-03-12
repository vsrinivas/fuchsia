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
size_t UnknownPrint(const uint8_t* bytes, size_t length,
                    rapidjson::Value& value,
                    rapidjson::Document::AllocatorType& allocator) {
  size_t size = length * 3 + 1;
  char output[size];
  for (size_t i = 0; i < length; i++) {
    snprintf(output + (i * 3), 4, "%02x ", bytes[i]);
  }
  output[size - 2] = '\0';
  value.SetString(output, size, allocator);
  return length;
}

size_t StringPrint(const uint8_t* bytes, size_t length, rapidjson::Value& value,
                   rapidjson::Document::AllocatorType& allocator) {
  // Strings: First 8 bytes is length
  uint64_t string_length = MemoryFrom<uint64_t>(bytes);
  // next 8 bytes is 0 if the string is null, and 0xffffffff otherwise.
  if (bytes[8] == 0x0) {
    value.SetString("(null)", allocator);
  } else {
    // everything after that is the string.
    value.SetString(reinterpret_cast<const char*>(bytes + 16), string_length,
                    allocator);
  }
  return length;
}

bool DummyEq(const uint8_t* bytes, size_t length,
             const rapidjson::Value& value) {
  FXL_LOG(FATAL) << "Equality operator for type not implemented";
  return false;
}

size_t BoolPrint(const uint8_t* bytes, size_t length, rapidjson::Value& value,
                 rapidjson::Document::AllocatorType& allocator) {
  // assert that length == 1
  if (*bytes) {
    value.SetString("true", allocator);
  } else {
    value.SetString("false", allocator);
  }
  return sizeof(bool);
}

// A generic PrintFunction that can be used for any scalar type.
template <typename T>
size_t PrimitivePrint(const uint8_t* bytes, size_t length,
                      rapidjson::Value& value,
                      rapidjson::Document::AllocatorType& allocator) {
  T val = MemoryFrom<T>(bytes);
  value.SetString(std::to_string(val).c_str(), allocator);
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

size_t StructPrint(const Struct& str, const uint8_t* bytes, size_t length,
                   rapidjson::Value& value,
                   rapidjson::Document::AllocatorType& allocator) {
  value.SetObject();
  for (auto& member : str.members()) {
    Type member_type = member.GetType();
    rapidjson::Value key;
    key.SetString(member.name().c_str(), allocator);

    rapidjson::Value v;
    member_type.MakeValue(&bytes[member.offset()], member.size(), v, allocator);

    value.AddMember(key, v, allocator);
  }
  return length;
}

size_t ArrayPrint(Type type, uint32_t count, const uint8_t* bytes,
                  size_t length, rapidjson::Value& value,
                  rapidjson::Document::AllocatorType& allocator) {
  value.SetArray();
  size_t offset = 0;
  for (uint32_t i = 0; i < count; i++) {
    rapidjson::Value element;
    offset += type.MakeValue(bytes + offset, length, element, allocator);
    value.PushBack(element, allocator);
  }
  return length;
}

size_t VectorPrint(Type type, const uint8_t* bytes, size_t length,
                   rapidjson::Value& value,
                   rapidjson::Document::AllocatorType& allocator) {
  uint64_t size = MemoryFrom<uint64_t>(bytes);
  uint64_t data = MemoryFrom<uint64_t>(bytes + sizeof(size));
  if (data == UINTPTR_MAX) {
    ArrayPrint(type, size, bytes + sizeof(size) + sizeof(data), 0, value,
               allocator);
  } else if (data == 0) {
    value.SetNull();
  }
  return length;
}

size_t EnumPrint(const Enum& e, const uint8_t* bytes, size_t length,
                 rapidjson::Value& value,
                 rapidjson::Document::AllocatorType& allocator) {
  std::string name = e.GetNameFromBytes(bytes, length);
  value.SetString(name, allocator);
  return length;
}

}  // anonymous namespace

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

Type Library::TypeFromIdentifier(std::string& identifier) const {
  using std::placeholders::_1;
  using std::placeholders::_2;
  using std::placeholders::_3;
  using std::placeholders::_4;

  auto str = structs_.find(identifier);
  if (str != structs_.end()) {
    return Type(std::bind(StructPrint, std::ref(str->second), _1, _2, _3, _4),
                DummyEq);
  }
  auto enu = enums_.find(identifier);
  if (enu != enums_.end()) {
    return Type(std::bind(EnumPrint, std::ref(enu->second), _1, _2, _3, _4),
                DummyEq);
  }
  // And probably for enums and unions and tables.
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
    // TODO: Something else here
    return Type::get_illegal();
  }

  return library->TypeFromIdentifier(id);
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

Type Type::GetType(const LibraryLoader& loader, const rapidjson::Value& type) {
  using std::placeholders::_1;
  using std::placeholders::_2;
  using std::placeholders::_3;
  using std::placeholders::_4;

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
                          element_count, _1, _2, _3, _4),
                DummyEq);

  } else if (kind == "vector") {
    const rapidjson::Value& element_type = type["element_type"];
    using namespace std::placeholders;
    return Type(
        std::bind(VectorPrint, GetType(loader, element_type), _1, _2, _3, _4),
        DummyEq);
  } else if (kind == "string") {
    return Type(StringPrint, DummyEq);
  } else if (kind == "handle") {
  } else if (kind == "request") {
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
