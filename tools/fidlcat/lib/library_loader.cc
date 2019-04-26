// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "library_loader.h"

#include <src/lib/fxl/logging.h>

#include "rapidjson/error/en.h"
#include "tools/fidlcat/lib/wire_types.h"

// See library_loader.h for details.

namespace fidlcat {

std::unique_ptr<Type> InterfaceMethodParameter::GetType() const {
  if (!value_.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    return Type::get_illegal();
  }
  const rapidjson::Value& type = value_["type"];
  return Type::GetType(enclosing_method_.enclosing_interface()
                           .enclosing_library()
                           .enclosing_loader(),
                       type);
}

std::unique_ptr<Type> Library::TypeFromIdentifier(
    bool is_nullable, std::string& identifier) const {
  auto str = structs_.find(identifier);
  if (str != structs_.end()) {
    std::unique_ptr<Type> type(new StructType(std::ref(str->second)));
    if (is_nullable) {
      return std::make_unique<PointerType>(type.release());
    }
    return type;
  }
  auto enu = enums_.find(identifier);
  if (enu != enums_.end()) {
    return std::make_unique<EnumType>(std::ref(enu->second));
  }
  auto uni = unions_.find(identifier);
  if (uni != unions_.end()) {
    std::unique_ptr<Type> type(new UnionType(std::ref(uni->second)));
    if (is_nullable) {
      return std::make_unique<PointerType>(type.release());
    }
    return type;
  }
  // And probably for tables.
  return Type::get_illegal();
}

bool Library::GetInterfaceByName(const std::string& name,
                                 const Interface** ptr) const {
  for (const auto& interface : interfaces()) {
    if (interface.name() == name) {
      *ptr = &interface;
      return true;
    }
  }
  return false;
}

const std::unique_ptr<Type> Enum::GetType() const {
  // TODO Consider caching this.
  return Type::ScalarTypeFromName(value_["type"].GetString());
}

std::string Enum::GetNameFromBytes(const uint8_t* bytes, size_t length) const {
  std::unique_ptr<Type> type = GetType();
  for (auto& member : value_["members"].GetArray()) {
    Marker end(bytes + length, nullptr);
    Marker marker(bytes, nullptr, end);
    if (type->ValueEquals(marker, length, member["value"]["literal"])) {
      return member["name"].GetString();
    }
  }
  return "(Unknown enum member)";
}

uint32_t Enum::size() const { return GetType()->InlineSize(); }

std::unique_ptr<Type> StructMember::GetType() const {
  if (!value_.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    return Type::get_illegal();
  }
  const rapidjson::Value& type = value_["type"];
  return Type::GetType(
      enclosing_struct().enclosing_library().enclosing_loader(), type);
}

std::unique_ptr<Type> UnionMember::GetType() const {
  if (!value_.HasMember("type")) {
    FXL_LOG(ERROR) << "Type missing";
    return Type::get_illegal();
  }
  const rapidjson::Value& type = value_["type"];
  return Type::GetType(enclosing_union().enclosing_library().enclosing_loader(),
                       type);
}

const UnionMember& Union::get_illegal_member() const {
  class IllegalUnionMember : public UnionMember {
   public:
    IllegalUnionMember(const Union& uni) : UnionMember(uni, value_) {}

    virtual std::unique_ptr<Type> GetType() const override {
      return Type::get_illegal();
    }

    virtual uint64_t size() const override { return enclosing_union().size(); }

    virtual std::string name() const override { return "unknown"; }

   private:
    const rapidjson::Value value_;
  };
  if (illegal_ == nullptr) {
    illegal_.reset(new IllegalUnionMember(*this));
  }
  return *illegal_;
}

const UnionMember& Union::MemberWithTag(uint32_t tag) const {
  if (tag >= members_.size() || tag < 0) {
    return get_illegal_member();
  }
  return members_[tag];
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

InterfaceMethod::InterfaceMethod(const Interface& interface,
                                 const rapidjson::Value& value)
    : enclosing_interface_(interface), value_(value) {
  if (value_["has_request"].GetBool()) {
    request_params_ =
        std::make_optional<std::vector<InterfaceMethodParameter>>();
    auto request_arr = value["maybe_request"].GetArray();
    request_params_->reserve(request_arr.Size());
    for (auto& request : request_arr) {
      request_params_->emplace_back(*this, request);
    }
  } else {
    request_params_ = {};
  }

  if (value_["has_response"].GetBool()) {
    response_params_ =
        std::make_optional<std::vector<InterfaceMethodParameter>>();
    auto response_arr = value["maybe_response"].GetArray();
    response_params_->reserve(response_arr.Size());
    for (auto& response : response_arr) {
      response_params_->emplace_back(*this, response);
    }
  } else {
    response_params_ = {};
  }
}

InterfaceMethod::InterfaceMethod(InterfaceMethod&& other)
    : enclosing_interface_(other.enclosing_interface_), value_(other.value_) {
  if (other.request_params_) {
    request_params_ =
        std::make_optional<std::vector<InterfaceMethodParameter>>();
    for (auto& request : *other.request_params_) {
      request_params_->emplace_back(*this, request.value_);
    }
  } else {
    request_params_ = {};
  }

  if (other.response_params_) {
    response_params_ =
        std::make_optional<std::vector<InterfaceMethodParameter>>();
    for (auto& response : *other.response_params_) {
      response_params_->emplace_back(*this, response.value_);
    }
  } else {
    response_params_ = {};
  }
}

std::string InterfaceMethod::fully_qualified_name() const {
  return enclosing_interface_.name() + "." + name();
}

bool Interface::GetMethodByFullName(const std::string& name,
                                    const InterfaceMethod** method_ptr) const {
  for (const auto& method : methods()) {
    if (method.fully_qualified_name() == name) {
      *method_ptr = &method;
      return true;
    }
  }
  return false;
}

}  // namespace fidlcat
