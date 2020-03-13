// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/fidl/json_xdr.h"

#include <lib/fidl/cpp/string.h>

#include <string>

#include "src/lib/fxl/macros.h"
#include "src/lib/syslog/cpp/logger.h"

using JsonDoc = rapidjson::Document;
using JsonValue = rapidjson::Value;

namespace modular {

namespace {
const char* JsonTypeName(const rapidjson::Type type) {
  switch (type) {
    case rapidjson::kNullType:
      return "null";
    case rapidjson::kFalseType:
      return "false";
    case rapidjson::kTrueType:
      return "true";
    case rapidjson::kObjectType:
      return "object";
    case rapidjson::kArrayType:
      return "array";
    case rapidjson::kStringType:
      return "string";
    case rapidjson::kNumberType:
      return "number";
  };
}
}  // namespace

// HACK(mesch): We should not need this, get rid of it.
thread_local JsonValue XdrContext::null_ = JsonValue();

XdrContext::XdrContext(const XdrOp op, JsonDoc* const doc, std::string* const error)
    : parent_(nullptr), name_(nullptr), error_(error), op_(op), doc_(doc), value_(doc) {
  FX_DCHECK(doc_ != nullptr);
  FX_DCHECK(error_ != nullptr);
}

XdrContext::XdrContext(XdrContext* const parent, const char* const name, const XdrOp op,
                       JsonDoc* const doc, JsonValue* const value)
    : parent_(parent), name_(name), error_(nullptr), op_(op), doc_(doc), value_(value) {
  FX_DCHECK(parent_ != nullptr);
  FX_DCHECK(doc_ != nullptr);
  FX_DCHECK(value_ != nullptr);
}

XdrContext::~XdrContext() = default;

bool XdrContext::Version(uint32_t version) {
  constexpr char kVersion[] = "@version";
  switch (op_) {
    case XdrOp::TO_JSON:
      Field(kVersion).Value(&version);
      return true;

    case XdrOp::FROM_JSON: {
      if (!value_->IsObject()) {
        AddError("Version(): must be on an Object.");
        return false;
      }
      auto i = value_->FindMember(kVersion);
      if (i == value_->MemberEnd()) {
        AddError("Version(): No @version present.");
        return false;
      }
      uint32_t actual_version{};
      Field(kVersion).Value(&actual_version);
      if (actual_version != version) {
        AddError("Version(): Found version " + std::to_string(actual_version) +
                 " but expected version " + std::to_string(version));
        return false;
      }
      return true;
    }
  }
}

void XdrContext::Value(unsigned char* const data) {
  ValueWithDefault(data, true, static_cast<unsigned char>(0));
}

void XdrContext::Value(int8_t* const data) { ValueWithDefault(data, true, static_cast<int8_t>(0)); }

void XdrContext::Value(unsigned short* const data) {
  ValueWithDefault(data, true, static_cast<unsigned short>(0));
}

void XdrContext::Value(short* const data) { ValueWithDefault(data, true, static_cast<short>(0)); }

void XdrContext::Value(fidl::StringPtr* const data) { ValueWithDefault(data, true, nullptr); }

void XdrContext::Value(std::string* const data) { ValueWithDefault(data, true, ""); }

void XdrContext::ValueWithDefault(unsigned char* const data, bool use_data,
                                  unsigned char default_value) {
  switch (op_) {
    case XdrOp::TO_JSON:
      use_data ? value_->Set(static_cast<int>(*data), allocator())
               : value_->Set(static_cast<int>(default_value), allocator());
      break;

    case XdrOp::FROM_JSON:
      if (!value_->Is<int>()) {
        if (use_data) {
          AddError("Value() of unsigned char: int expected");
          return;
        }
        *data = default_value;
        return;
      }
      *data = static_cast<unsigned char>(value_->Get<int>());
      return;
  }
}

void XdrContext::ValueWithDefault(int8_t* const data, bool use_data, int8_t default_value) {
  switch (op_) {
    case XdrOp::TO_JSON:
      use_data ? value_->Set(static_cast<int>(*data), allocator())
               : value_->Set(static_cast<int>(default_value), allocator());
      break;

    case XdrOp::FROM_JSON:
      if (!value_->Is<int>()) {
        if (use_data) {
          AddError("Value() of int8: int expected");
          return;
        }
        *data = default_value;
        return;
      }
      *data = static_cast<int8_t>(value_->Get<int>());
      return;
  }
}

void XdrContext::ValueWithDefault(unsigned short* const data, bool use_data,
                                  unsigned short default_value) {
  switch (op_) {
    case XdrOp::TO_JSON:
      use_data ? value_->Set(static_cast<int>(*data), allocator())
               : value_->Set(static_cast<int>(default_value), allocator());
      break;

    case XdrOp::FROM_JSON:
      if (!value_->Is<int>()) {
        if (use_data) {
          AddError("Value() of unsigned short: int expected");
          return;
        }
        *data = default_value;
        return;
      }
      *data = static_cast<unsigned short>(value_->Get<int>());
      return;
  }
}

void XdrContext::ValueWithDefault(short* const data, bool use_data, short default_value) {
  switch (op_) {
    case XdrOp::TO_JSON:
      use_data ? value_->Set(static_cast<int>(*data), allocator())
               : value_->Set(static_cast<int>(default_value), allocator());
      break;

    case XdrOp::FROM_JSON:
      if (!value_->Is<int>()) {
        if (use_data) {
          AddError("Value() of short: int expected");
          return;
        }
        *data = default_value;
        return;
      }
      *data = static_cast<short>(value_->Get<int>());
      return;
  }
}

void XdrContext::ValueWithDefault(fidl::StringPtr* const data, bool use_data,
                                  fidl::StringPtr default_value) {
  switch (op_) {
    case XdrOp::TO_JSON:
      if (!use_data) {
        value_->SetString(default_value.has_value() ? default_value.value() : "", allocator());
        break;
      }
      data->has_value() ? value_->SetString(data->value(), allocator()) : value_->SetNull();
      break;

    case XdrOp::FROM_JSON:
      if (value_->IsNull()) {
        data->reset();
      } else if (value_->IsString()) {
        *data = value_->GetString();
      } else {
        if (use_data) {
          AddError("Value() of fidl::StringPtr: string expected");
          return;
        }
        *data = default_value;
      }
      break;
  }
}

void XdrContext::ValueWithDefault(std::string* const data, bool use_data,
                                  std::string default_value) {
  switch (op_) {
    case XdrOp::TO_JSON:
      use_data ? value_->SetString(*data, allocator())
               : value_->SetString(default_value, allocator());
      break;

    case XdrOp::FROM_JSON:
      if (value_->IsString()) {
        *data = value_->GetString();
      } else {
        if (use_data) {
          AddError("Value() of std::string: string expected");
          return;
        }
        *data = default_value;
      }
      break;
  }
}

XdrContext XdrContext::Field(const char field[]) {
  switch (op_) {
    case XdrOp::TO_JSON:
      if (!value_->IsObject()) {
        value_->SetObject();
      }
      break;

    case XdrOp::FROM_JSON:
      if (!value_->IsObject()) {
        AddError("Object expected for field " + std::string(field));
        return {this, field, op_, doc_, &null_};
      }
  }

  auto i = value_->FindMember(field);
  if (i != value_->MemberEnd()) {
    return {this, field, op_, doc_, &i->value};
  }

  switch (op_) {
    case XdrOp::TO_JSON: {
      JsonValue name{field, allocator()};
      value_->AddMember(name, JsonValue(), allocator());
      auto i = value_->FindMember(field);
      FX_DCHECK(i != value_->MemberEnd());
      return {this, field, op_, doc_, &i->value};
    }

    case XdrOp::FROM_JSON:
      return {this, field, op_, doc_, &null_};
  }
}

XdrContext XdrContext::FieldWithDefault(const char field[]) {
  switch (op_) {
    case XdrOp::TO_JSON:
      if (!value_->IsObject()) {
        value_->SetObject();
      }
      break;

    case XdrOp::FROM_JSON:
      if (!value_->IsObject()) {
        return {this, field, op_, doc_, &null_};
      }
  }

  auto i = value_->FindMember(field);
  if (i != value_->MemberEnd()) {
    return {this, field, op_, doc_, &i->value};
  }

  switch (op_) {
    case XdrOp::TO_JSON: {
      JsonValue name{field, allocator()};
      value_->AddMember(name, JsonValue(), allocator());
      auto i = value_->FindMember(field);
      FX_DCHECK(i != value_->MemberEnd());
      return {this, field, op_, doc_, &i->value};
    }

    case XdrOp::FROM_JSON:
      return {this, field, op_, doc_, &null_};
  }
}

XdrContext XdrContext::Element(const size_t i) {
  switch (op_) {
    case XdrOp::TO_JSON:
      if (!value_->IsArray()) {
        value_->SetArray();
      }
      break;

    case XdrOp::FROM_JSON:
      if (!value_->IsArray()) {
        AddError("Array expected for element " + std::to_string(i));
        return {this, nullptr, op_, doc_, &null_};
      }
  }

  if (i < value_->Size()) {
    return {this, nullptr, op_, doc_, &value_->operator[](i)};
  }

  switch (op_) {
    case XdrOp::TO_JSON:
      while (i >= value_->Size()) {
        value_->PushBack(JsonValue(), allocator());
      }
      return {this, nullptr, op_, doc_, &value_->operator[](i)};

    case XdrOp::FROM_JSON:
      return {this, nullptr, op_, doc_, &null_};
  }
}

XdrContext XdrContext::ElementWithDefault(const size_t i) {
  switch (op_) {
    case XdrOp::TO_JSON:
      if (!value_->IsArray()) {
        value_->SetArray();
      }
      break;

    case XdrOp::FROM_JSON:
      if (!value_->IsArray()) {
        return {this, nullptr, op_, doc_, &null_};
      }
  }

  if (i < value_->Size()) {
    return {this, nullptr, op_, doc_, &value_->operator[](i)};
  }

  switch (op_) {
    case XdrOp::TO_JSON:
      while (i >= value_->Size()) {
        value_->PushBack(JsonValue(), allocator());
      }
      return {this, nullptr, op_, doc_, &value_->operator[](i)};

    case XdrOp::FROM_JSON:
      return {this, nullptr, op_, doc_, &null_};
  }
}

void XdrContext::AddError(const std::string& message) {
  auto error = AddError();
  error->append(": " + message + "\n");
}

std::string* XdrContext::AddError() {
  std::string* const ret = parent_ ? parent_->AddError() : error_;

  if (parent_) {
    ret->append("/");
  }

  ret->append(JsonTypeName(value_->GetType()));

  if (name_) {
    ret->append(" ");
    ret->append(name_);
  }

  return ret;
}

std::string* XdrContext::GetError() { return parent_ ? parent_->GetError() : error_; }

}  // namespace modular
