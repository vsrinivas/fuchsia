// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/types.h"

#include <lib/syslog/cpp/macros.h>

#include <limits>
#include <memory>
#include <ostream>
#include <string>

#include "src/developer/shell/interpreter/src/expressions.h"
#include "src/developer/shell/interpreter/src/instructions.h"
#include "src/developer/shell/interpreter/src/interpreter.h"
#include "src/developer/shell/interpreter/src/schema.h"
#include "src/developer/shell/interpreter/src/scope.h"
#include "src/developer/shell/interpreter/src/value.h"

namespace shell {
namespace interpreter {

// - type undefined --------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUndefined::Duplicate() const { return std::make_unique<TypeUndefined>(); }

void TypeUndefined::Dump(std::ostream& os) const { os << "undefined"; }

// - type builtin ----------------------------------------------------------------------------------

Variable* TypeBuiltin::CreateVariable(ExecutionContext* context, Scope* scope, NodeId id,
                                      const std::string& name, bool is_mutable) const {
  return scope->CreateVariable(id, name, Duplicate(), is_mutable);
}

// - type raw -------------------------------------------------------------------------------------

void TypeRaw::GenerateDefaultValue(ExecutionContext* context, code::Code* code) const {
  code->Literal64(0);
}

bool TypeRaw::GenerateVariable(ExecutionContext* context, code::Code* code, const NodeId& id,
                               const Variable* variable) const {
  if (variable->type()->Kind() != Kind()) {
    std::stringstream ss;
    ss << "Can't use variable of type " << *variable->type() << " for type " << *this << ".";
    context->EmitError(id, ss.str());
    return false;
  }
  code->LoadRaw(variable->index(), variable->type()->Size());
  return true;
}

void TypeRaw::GenerateAssignVariable(ExecutionContext* context, code::Code* code, const NodeId& id,
                                     const Variable* variable) const {
  code->StoreRaw(variable->index(), variable->type()->Size());
}

// - type bool -------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeBool::Duplicate() const { return std::make_unique<TypeBool>(); }

void TypeBool::Dump(std::ostream& os) const { os << "bool"; }

// - type char -------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeChar::Duplicate() const { return std::make_unique<TypeChar>(); }

void TypeChar::Dump(std::ostream& os) const { os << "char"; }

// - type string -----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeString::Duplicate() const { return std::make_unique<TypeString>(); }

void TypeString::Dump(std::ostream& os) const { os << "string"; }

void TypeString::GenerateDefaultValue(ExecutionContext* context, code::Code* code) const {
  StringContainer string(context->interpreter(), "");
  code->StringLiteral(string.data());
}

bool TypeString::GenerateStringLiteral(ExecutionContext* context, code::Code* code,
                                       const StringLiteral* literal) const {
  code->StringLiteral(literal->string());
  return true;
}

bool TypeString::GenerateVariable(ExecutionContext* context, code::Code* code, const NodeId& id,
                                  const Variable* variable) const {
  if (!variable->type()->IsString()) {
    std::stringstream ss;
    ss << "Can't use variable of type " << *variable->type() << " for type " << *this << ".";
    context->EmitError(id, ss.str());
    return false;
  }
  code->LoadReferenceCounted(variable->index());
  return true;
}

void TypeString::GenerateAssignVariable(ExecutionContext* context, code::Code* code,
                                        const NodeId& id, const Variable* variable) const {
  code->StoreReferenceCounted(variable->index());
}

bool TypeString::GenerateAddition(ExecutionContext* context, code::Code* code,
                                  const Addition* addition) const {
  size_t count = addition->left()->GenerateStringTerms(context, code, this) +
                 addition->right()->GenerateStringTerms(context, code, this);
  code->StringConcatenation(count);
  return true;
}

void TypeString::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  String* const* data = scope->Data<String*>(index);
  value->SetString(*data);
}

void TypeString::ClearVariable(ExecutionScope* scope, size_t index) const {
  String** data = scope->Data<String*>(index);
  if (*data != nullptr) {
    (*data)->Release();
    *data = nullptr;
  }
}

void TypeString::SetData(uint8_t* data, uint64_t value, bool free_old_value) const {
  // We don't need to take a link on the new value because the value comes from the stack and has
  // already a link.
  String* new_value = reinterpret_cast<String*>(value);
  String** string = reinterpret_cast<String**>(data);
  if (free_old_value && (*string != nullptr)) {
    (*string)->Release();
  }
  *string = new_value;
}

void TypeString::EmitResult(ExecutionContext* context, uint64_t value) const {
  Value result;
  String* string = reinterpret_cast<String*>(value);
  result.SetString(string);
  context->interpreter()->Result(context, result);
  string->Release();
}

// - type int --------------------------------------------------------------------------------------

bool TypeInt::GenerateIntegerLiteral(ExecutionContext* context, code::Code* code,
                                     const IntegerLiteral* literal) const {
  std::pair<uint64_t, uint64_t> limits = Limits();
  uint64_t max_absolute_value = literal->negative() ? limits.first : limits.second;
  if (literal->absolute_value() > max_absolute_value) {
    std::stringstream ss;
    ss << "Can't create an integer literal of type " << *this << " with " << *literal << '.';
    context->EmitError(literal->id(), ss.str());
    return false;
  }
  uint64_t value = literal->negative() ? -literal->absolute_value() : literal->absolute_value();
  size_t size = Size();
  if (size < 8) {
    // Zeros the bits which are not part of the value (useful for negative values).
    value = value & (std::numeric_limits<uint64_t>::max() >> (64 - size * 8));
  }
  code->Literal64(value);
  return true;
}

bool TypeInt::GenerateAddition(ExecutionContext* context, code::Code* code,
                               const Addition* addition) const {
  if (!addition->left()->Compile(context, code, this) ||
      !addition->right()->Compile(context, code, this)) {
    return false;
  }
  code->IntegerAddition(addition->with_exceptions(), Size(), Signed());
  return true;
}

// - type int8 -------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInt8::Duplicate() const { return std::make_unique<TypeInt8>(); }

void TypeInt8::Dump(std::ostream& os) const { os << "int8"; }

void TypeInt8::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const int8_t* data = scope->Data<int8_t>(index);
  value->SetInt8(*data);
}

void TypeInt8::EmitResult(ExecutionContext* context, uint64_t value) const {
  Value result;
  int8_t int8 = static_cast<int8_t>(static_cast<uint8_t>(value));
  result.SetInt8(int8);
  context->interpreter()->Result(context, result);
}

// - type uint8 ------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUint8::Duplicate() const { return std::make_unique<TypeUint8>(); }

void TypeUint8::Dump(std::ostream& os) const { os << "uint8"; }

void TypeUint8::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const uint8_t* data = scope->Data<uint8_t>(index);
  value->SetUint8(*data);
}

void TypeUint8::EmitResult(ExecutionContext* context, uint64_t value) const {
  Value result;
  uint8_t uint8 = static_cast<uint8_t>(value);
  result.SetUint8(uint8);
  context->interpreter()->Result(context, result);
}

// - type int16 ------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInt16::Duplicate() const { return std::make_unique<TypeInt16>(); }

void TypeInt16::Dump(std::ostream& os) const { os << "int16"; }

void TypeInt16::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const int16_t* data = scope->Data<int16_t>(index);
  value->SetInt16(*data);
}

void TypeInt16::EmitResult(ExecutionContext* context, uint64_t value) const {
  Value result;
  int16_t int16 = static_cast<int16_t>(static_cast<uint16_t>(value));
  result.SetInt16(int16);
  context->interpreter()->Result(context, result);
}

// - type uint16 -----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUint16::Duplicate() const { return std::make_unique<TypeUint16>(); }

void TypeUint16::Dump(std::ostream& os) const { os << "uint16"; }

void TypeUint16::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const uint16_t* data = scope->Data<uint16_t>(index);
  value->SetUint16(*data);
}

void TypeUint16::EmitResult(ExecutionContext* context, uint64_t value) const {
  Value result;
  uint16_t uint16 = static_cast<uint16_t>(value);
  result.SetUint16(uint16);
  context->interpreter()->Result(context, result);
}

// - type int32 ------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInt32::Duplicate() const { return std::make_unique<TypeInt32>(); }

void TypeInt32::Dump(std::ostream& os) const { os << "int32"; }

void TypeInt32::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const int32_t* data = scope->Data<int32_t>(index);
  value->SetInt32(*data);
}

void TypeInt32::EmitResult(ExecutionContext* context, uint64_t value) const {
  Value result;
  int32_t int32 = static_cast<int32_t>(static_cast<uint32_t>(value));
  result.SetInt32(int32);
  context->interpreter()->Result(context, result);
}

// - type uint32 -----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUint32::Duplicate() const { return std::make_unique<TypeUint32>(); }

void TypeUint32::Dump(std::ostream& os) const { os << "uint32"; }

void TypeUint32::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const uint32_t* data = scope->Data<uint32_t>(index);
  value->SetUint32(*data);
}

void TypeUint32::EmitResult(ExecutionContext* context, uint64_t value) const {
  Value result;
  uint32_t uint32 = static_cast<uint32_t>(value);
  result.SetUint32(uint32);
  context->interpreter()->Result(context, result);
}

// - type int64 ------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInt64::Duplicate() const { return std::make_unique<TypeInt64>(); }

void TypeInt64::Dump(std::ostream& os) const { os << "int64"; }

void TypeInt64::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const int64_t* data = scope->Data<int64_t>(index);
  value->SetInt64(*data);
}

void TypeInt64::EmitResult(ExecutionContext* context, uint64_t value) const {
  Value result;
  int64_t int64 = static_cast<int64_t>(value);
  result.SetInt64(int64);
  context->interpreter()->Result(context, result);
}

// - type uint64 -----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUint64::Duplicate() const { return std::make_unique<TypeUint64>(); }

void TypeUint64::Dump(std::ostream& os) const { os << "uint64"; }

void TypeUint64::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const uint64_t* data = scope->Data<uint64_t>(index);
  value->SetUint64(*data);
}

void TypeUint64::EmitResult(ExecutionContext* context, uint64_t value) const {
  Value result;
  result.SetUint64(value);
  context->interpreter()->Result(context, result);
}

// - type integer ----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInteger::Duplicate() const { return std::make_unique<TypeInteger>(); }

bool TypeInteger::GenerateIntegerLiteral(ExecutionContext* context, code::Code* code,
                                         const IntegerLiteral* literal) const {
  return impl_->GenerateIntegerLiteral(context, code, literal);
}

void TypeInteger::GenerateDefaultValue(ExecutionContext* context, code::Code* code) const {
  impl_->GenerateDefaultValue(context, code);
}

void TypeInteger::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  impl_->LoadVariable(scope, index, value);
}

void TypeInteger::Dump(std::ostream& os) const { os << "integer"; }

bool TypeInteger::GenerateVariable(ExecutionContext* context, code::Code* code, const NodeId& id,
                                   const Variable* variable) const {
  if (variable->type()->Kind() != TypeKind::kInt64 &&
      variable->type()->Kind() != TypeKind::kInteger) {
    std::stringstream ss;
    ss << "Can't use variable of type " << *variable->type() << " for type " << *this << ".";
    context->EmitError(id, ss.str());
    return false;
  }
  code->LoadRaw(variable->index(), variable->type()->Size());
  return true;
}

void TypeInteger::EmitResult(ExecutionContext* context, uint64_t value) const {
  impl_->EmitResult(context, value);
}

// - type float32 ----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeFloat32::Duplicate() const { return std::make_unique<TypeFloat32>(); }

void TypeFloat32::Dump(std::ostream& os) const { os << "float32"; }

// - type float64 ----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeFloat64::Duplicate() const { return std::make_unique<TypeFloat64>(); }

void TypeFloat64::Dump(std::ostream& os) const { os << "float64"; }

// - type object
// ----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeObject::Duplicate() const {
  return std::make_unique<TypeObject>(schema_);
}

void TypeObject::Dump(std::ostream& os) const {
  const char* separator = "";
  os << "{" << std::endl;
  for (auto& field : schema_->fields()) {
    os << separator << field->name() << ": ";
    field->type()->Dump(os);
    separator = ", ";
  }
  os << "}";
}

void TypeObject::GenerateObject(ExecutionContext* context, code::Code* code,
                                const ObjectDeclaration* literal) const {
  code->ObjectPush(schema_);
  code->ObjectInit();
}

void TypeObject::GenerateInitialization(ExecutionContext* context, code::Code* code,
                                        const ObjectDeclaration* declaration) const {
  for (auto& object_field : declaration->fields()) {
    const Type* type = object_field->schema()->type();
    object_field->Compile(context, code, type);
  }
}

Variable* TypeObject::CreateVariable(ExecutionContext* context, Scope* scope, NodeId id,
                                     const std::string& name, bool is_mutable) const {
  return scope->CreateVariable(id, name, Duplicate(), is_mutable);
}

bool TypeObject::GenerateVariable(ExecutionContext* context, code::Code* code, const NodeId& id,
                                  const Variable* variable) const {
  if (!variable->type()->IsObject()) {
    std::stringstream ss;
    ss << "Can't use variable of type " << *variable->type() << " for type " << *this << ".";
    context->EmitError(id, ss.str());
    return false;
  }
  code->LoadReferenceCounted(variable->index());
  return true;
}

void TypeObject::GenerateAssignVariable(ExecutionContext* context, code::Code* code,
                                        const NodeId& id, const Variable* variable) const {
  code->StoreReferenceCounted(variable->index());
}

void TypeObject::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  Object* const* data = scope->Data<Object*>(index);
  value->SetObject(*data);
}

void TypeObject::ClearVariable(ExecutionScope* scope, size_t index) const {
  Object** data = scope->Data<Object*>(index);
  if (*data != nullptr) {
    (*data)->Release();
    *data = nullptr;
  }
}

void TypeObject::SetData(uint8_t* data, uint64_t value, bool free_old_value) const {
  // We don't need to take a link on the new value because the value comes from the stack and has
  // already a link.
  Object* new_value = reinterpret_cast<Object*>(value);
  Object** object = reinterpret_cast<Object**>(data);
  if (free_old_value && (*object != nullptr)) {
    (*object)->Release();
  }
  *object = new_value;
}

void TypeObject::EmitResult(ExecutionContext* context, uint64_t value) const {
  Value result;
  auto object = reinterpret_cast<Object*>(value);
  result.SetObject(object);
  context->interpreter()->Result(context, result);
  object->Release();
}

}  // namespace interpreter
}  // namespace shell
