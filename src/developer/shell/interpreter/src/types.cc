// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/types.h"

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
#include "src/lib/syslog/cpp/logger.h"

namespace shell {
namespace interpreter {

// - type undefined --------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUndefined::Duplicate() const { return std::make_unique<TypeUndefined>(); }

void TypeUndefined::Dump(std::ostream& os) const { os << "undefined"; }

// - type builtin ----------------------------------------------------------------------------------

Variable* TypeBuiltin::CreateVariable(ExecutionContext* context, Scope* scope, NodeId id,
                                      const std::string& name) const {
  return scope->CreateVariable(id, name, Duplicate());
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
  StringContainer string("");
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

void TypeString::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  auto data = reinterpret_cast<String* const*>(scope->Data(index, sizeof(String*)));
  value->SetString(*data);
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

// - type int8 -------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInt8::Duplicate() const { return std::make_unique<TypeInt8>(); }

void TypeInt8::Dump(std::ostream& os) const { os << "int8"; }

void TypeInt8::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const int8_t* data = scope->Data<int8_t>(index);
  value->SetInt8(*data);
}

// - type uint8 ------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUint8::Duplicate() const { return std::make_unique<TypeUint8>(); }

void TypeUint8::Dump(std::ostream& os) const { os << "uint8"; }

void TypeUint8::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const uint8_t* data = scope->Data<uint8_t>(index);
  value->SetUint8(*data);
}

// - type int16 ------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInt16::Duplicate() const { return std::make_unique<TypeInt16>(); }

void TypeInt16::Dump(std::ostream& os) const { os << "int16"; }

void TypeInt16::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const int16_t* data = scope->Data<int16_t>(index);
  value->SetInt16(*data);
}

// - type uint16 -----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUint16::Duplicate() const { return std::make_unique<TypeUint16>(); }

void TypeUint16::Dump(std::ostream& os) const { os << "uint16"; }

void TypeUint16::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const uint16_t* data = scope->Data<uint16_t>(index);
  value->SetUint16(*data);
}

// - type int32 ------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInt32::Duplicate() const { return std::make_unique<TypeInt32>(); }

void TypeInt32::Dump(std::ostream& os) const { os << "int32"; }

void TypeInt32::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const int32_t* data = scope->Data<int32_t>(index);
  value->SetInt32(*data);
}

// - type uint32 -----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUint32::Duplicate() const { return std::make_unique<TypeUint32>(); }

void TypeUint32::Dump(std::ostream& os) const { os << "uint32"; }

void TypeUint32::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const uint32_t* data = scope->Data<uint32_t>(index);
  value->SetUint32(*data);
}

// - type int64 ------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInt64::Duplicate() const { return std::make_unique<TypeInt64>(); }

void TypeInt64::Dump(std::ostream& os) const { os << "int64"; }

void TypeInt64::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const int64_t* data = scope->Data<int64_t>(index);
  value->SetInt64(*data);
}

// - type uint64 -----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUint64::Duplicate() const { return std::make_unique<TypeUint64>(); }

void TypeUint64::Dump(std::ostream& os) const { os << "uint64"; }

void TypeUint64::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const uint64_t* data = scope->Data<uint64_t>(index);
  value->SetUint64(*data);
}

// - type integer ----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInteger::Duplicate() const { return std::make_unique<TypeInteger>(); }

void TypeInteger::Dump(std::ostream& os) const { os << "integer"; }

// - type float32 ----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeFloat32::Duplicate() const { return std::make_unique<TypeFloat32>(); }

void TypeFloat32::Dump(std::ostream& os) const { os << "float32"; }

// - type float64 ----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeFloat64::Duplicate() const { return std::make_unique<TypeFloat64>(); }

void TypeFloat64::Dump(std::ostream& os) const { os << "float64"; }

// - type object
// ----------------------------------------------------------------------------------

void TypeObject::Dump(std::ostream& os) const {
  // TODO: improve indentation.
  os << "{" << std::endl;
  for (auto& field : schema_->fields()) {
    ObjectFieldSchema* field_schema = field->AsObjectFieldSchema();

    FX_DCHECK(field_schema != nullptr) << "Bad node found as field schema";
    os << field_schema->name() << " : ";
    field_schema->type()->Dump(os);
    os << "," << std::endl;
  }
  os << "}";
}

size_t TypeObject::Size() const {
  // TODO: If we want the alignment right, we need to know the alignment of the largest contained
  // element.  We therefore need an Alignment() method.
  size_t size = 0;
  for (auto& field : schema_->fields()) {
    size += field->type()->Size();
  }
  return size;
}

std::unique_ptr<Type> TypeObject::Duplicate() const {
  return std::make_unique<TypeObject>(schema_);
}

}  // namespace interpreter
}  // namespace shell
