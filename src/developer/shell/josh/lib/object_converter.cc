// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/josh/lib/object_converter.h"

#include "src/developer/shell/josh/lib/zx.h"
#include "src/lib/fidl_codec/wire_object.h"

namespace shell::fidl {

bool ObjectConverter::HandleNull(const fidl_codec::Type* type) {
  if (!JS_IsNull(value_) && !JS_IsUndefined(value_)) {
    return false;
  } else if (!type->Nullable()) {
    JS_ThrowTypeError(ctx_, "Type is not nullable.");
  } else {
    result_ = std::make_unique<fidl_codec::NullValue>();
  }

  return true;
}

void ObjectConverter::VisitType(const fidl_codec::Type* type) {
  JS_ThrowTypeError(ctx_, "Unknown FIDL type '%s'.", type->Name().c_str());
}

void ObjectConverter::VisitTableType(const fidl_codec::TableType* type) {
  if (!JS_IsObject(value_)) {
    JS_ThrowTypeError(ctx_, "Expected object.");
    return;
  }

  auto ret = std::make_unique<fidl_codec::TableValue>(type->table_definition());
  for (const auto& member : type->table_definition().members()) {
    auto value = JS_GetPropertyStr(ctx_, value_, member->name().c_str());
    if (JS_IsUndefined(value)) {
      continue;
    }

    auto child = ObjectConverter::Convert(ctx_, member->type(), value);
    if (!child) {
      return;
    }

    ret->AddMember(member.get(), std::move(child));
  }

  result_ = std::move(ret);
}

void ObjectConverter::VisitStringType(const fidl_codec::StringType* type) {
  if (HandleNull(type)) {
    return;
  }

  size_t len;
  const char* str = JS_ToCStringLen(ctx_, &len, value_);

  if (str) {
    result_ = std::make_unique<fidl_codec::StringValue>(std::string(str, len));
  }
}

void ObjectConverter::VisitBoolType(const fidl_codec::BoolType* type) {
  int got = JS_ToBool(ctx_, value_);

  // -1 indicates a problem.
  if (got >= 0) {
    result_ = std::make_unique<fidl_codec::BoolValue>(got ? 1 : 0);
  }
}

void ObjectConverter::VisitStructType(const fidl_codec::StructType* type) {
  if (HandleNull(type)) {
    return;
  }

  std::function<JSValue(const std::string&)> get_item = [this](const std::string& name) mutable {
    return JS_GetPropertyStr(ctx_, value_, name.c_str());
  };

  uint32_t idx = 0;

  if (JS_IsArray(ctx_, value_)) {
    get_item = [this, &idx](const std::string& /*name*/) mutable {
      return JS_GetPropertyUint32(ctx_, value_, idx++);
    };
  } else if (!JS_IsObject(value_)) {
    JS_ThrowTypeError(ctx_, "Expected object.");
    return;
  }

  auto ret = std::make_unique<fidl_codec::StructValue>(type->struct_definition());
  for (const auto& member : type->struct_definition().members()) {
    auto child = ObjectConverter::Convert(ctx_, member->type(), get_item(member->name()));

    if (!child) {
      return;
    }

    ret->AddField(member.get(), std::move(child));
  }

  result_ = std::move(ret);
}

void ObjectConverter::VisitUnionType(const fidl_codec::UnionType* type) {
  if (HandleNull(type)) {
    return;
  }

  if (!JS_IsObject(value_)) {
    JS_ThrowTypeError(ctx_, "Expected object.");
    return;
  }

  for (const auto& member : type->union_definition().members()) {
    auto result = JS_GetPropertyStr(ctx_, value_, member->name().c_str());

    if (JS_IsUndefined(result)) {
      continue;
    }

    auto result_converted = ObjectConverter::Convert(ctx_, member->type(), result);

    if (result_converted) {
      result_ = std::make_unique<fidl_codec::UnionValue>(*member, std::move(result_converted));
    }

    return;
  }

  JS_ThrowTypeError(ctx_, "Unknown union variant.");
}

template <typename T>
void ObjectConverter::VisitAnyList(const T* type, std::optional<size_t> count) {
  if (!count && HandleNull(type)) {
    return;
  }

  int32_t length;

  if (!JS_IsArray(ctx_, value_)) {
    JS_ThrowTypeError(ctx_, "Expected array.");
    return;
  }

  // It's an array, so assume this works...
  JS_ToInt32(ctx_, &length, JS_GetPropertyStr(ctx_, value_, "length"));

  if (count && static_cast<uint32_t>(length) != *count) {
    JS_ThrowTypeError(ctx_, "Expected array of size %lu", *count);
  }

  auto ret = std::make_unique<fidl_codec::VectorValue>();

  for (int32_t i = 0; i < length; i++) {
    JSValue val = JS_GetPropertyUint32(ctx_, value_, i);

    auto got = ObjectConverter::Convert(ctx_, type->component_type(), val);
    if (!got) {
      return;
    }

    ret->AddValue(std::move(got));
  }

  result_ = std::move(ret);
}

void ObjectConverter::VisitArrayType(const fidl_codec::ArrayType* type) {
  VisitAnyList(type, type->count());
}

void ObjectConverter::VisitVectorType(const fidl_codec::VectorType* type) {
  VisitAnyList(type, std::nullopt);
}

void ObjectConverter::VisitEnumType(const fidl_codec::EnumType* type) {
  size_t len;
  const char* str = JS_ToCStringLen(ctx_, &len, value_);

  if (str) {
    auto name = std::string(str, len);

    for (const auto& member : type->enum_definition().members()) {
      if (name == member.name()) {
        result_ =
            std::make_unique<fidl_codec::IntegerValue>(member.absolute_value(), member.negative());
      }
    }

    JS_ThrowTypeError(ctx_, "Unexpected enum value: '%s'", name.c_str());
  }
}

void ObjectConverter::VisitBitsType(const fidl_codec::BitsType* type) {
  size_t len;
  const char* str = JS_ToCStringLen(ctx_, &len, value_);

  if (!str) {
    return;
  }

  auto value_string = std::string(str, len);
  std::map<std::string, std::unique_ptr<fidl_codec::Value>> values;
  size_t pos = 0;

  while (pos < value_string.size()) {
    std::string next_key;
    size_t next = value_string.find('|', pos);
    if (next == std::string::npos) {
      next_key = value_string.substr(pos);
      pos = value_string.size();
    } else {
      next_key = value_string.substr(pos, next - pos);
      pos = next + 1;
    }

    values[next_key] = std::unique_ptr<fidl_codec::Value>();
  }

  uint64_t out = 0;

  for (const auto& member : type->bits_definition().members()) {
    if (values.find(member.name()) != values.end()) {
      out |= member.absolute_value();
    }
  }

  result_ = std::make_unique<fidl_codec::IntegerValue>(out, false);
}

void ObjectConverter::VisitHandleType(const fidl_codec::HandleType* type) {
  if (HandleNull(type)) {
    return;
  }
  JSValue handle = JS_GetPropertyStr(ctx_, value_, "_handle");

  result_ = std::make_unique<fidl_codec::HandleValue>(zx::HandleFromJsval(handle));
}

void ObjectConverter::VisitAnyInteger(bool is_signed) {
  int64_t got;
  if (JS_ToInt64(ctx_, &got, value_) != -1) {
    bool negate = is_signed && got < 0;

    if (negate) {
      got = -got;
    }

    result_ = std::make_unique<fidl_codec::IntegerValue>(static_cast<uint64_t>(got), negate);
  }
}

void ObjectConverter::VisitUint8Type(const fidl_codec::Uint8Type* type) { VisitAnyInteger(false); }

void ObjectConverter::VisitUint16Type(const fidl_codec::Uint16Type* type) {
  VisitAnyInteger(false);
}

void ObjectConverter::VisitUint32Type(const fidl_codec::Uint32Type* type) {
  VisitAnyInteger(false);
}

void ObjectConverter::VisitUint64Type(const fidl_codec::Uint64Type* type) {
  VisitAnyInteger(false);
}

void ObjectConverter::VisitInt8Type(const fidl_codec::Int8Type* type) { VisitAnyInteger(true); }

void ObjectConverter::VisitInt16Type(const fidl_codec::Int16Type* type) { VisitAnyInteger(true); }

void ObjectConverter::VisitInt32Type(const fidl_codec::Int32Type* type) { VisitAnyInteger(true); }

void ObjectConverter::VisitInt64Type(const fidl_codec::Int64Type* type) { VisitAnyInteger(true); }

void ObjectConverter::VisitAnyFloat() {
  double got;
  if (JS_ToFloat64(ctx_, &got, value_) != -1) {
    result_ = std::make_unique<fidl_codec::DoubleValue>(got);
  }
}

void ObjectConverter::VisitFloat32Type(const fidl_codec::Float32Type* type) { VisitAnyFloat(); }

void ObjectConverter::VisitFloat64Type(const fidl_codec::Float64Type* type) { VisitAnyFloat(); }

}  // namespace shell::fidl
