// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/types.h"

#include <memory>
#include <ostream>
#include <string>

#include "src/developer/shell/interpreter/src/expressions.h"
#include "src/developer/shell/interpreter/src/interpreter.h"
#include "src/developer/shell/interpreter/src/scope.h"
#include "src/developer/shell/interpreter/src/value.h"

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

void TypeString::GenerateStringLiteral(ExecutionContext* context, code::Code* code,
                                       const StringLiteral* literal) const {
  code->StringLiteral(literal->string());
}

void TypeString::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  auto data = reinterpret_cast<String* const*>(scope->Data(index, sizeof(String*)));
  value->SetString(*data);
}

// - type int8 -------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInt8::Duplicate() const { return std::make_unique<TypeInt8>(); }

void TypeInt8::Dump(std::ostream& os) const { os << "int8"; }

// - type uint8 ------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUint8::Duplicate() const { return std::make_unique<TypeUint8>(); }

void TypeUint8::Dump(std::ostream& os) const { os << "uint8"; }

// - type int16 ------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInt16::Duplicate() const { return std::make_unique<TypeInt16>(); }

void TypeInt16::Dump(std::ostream& os) const { os << "int16"; }

// - type uint16 -----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUint16::Duplicate() const { return std::make_unique<TypeUint16>(); }

void TypeUint16::Dump(std::ostream& os) const { os << "uint16"; }

// - type int32 ------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInt32::Duplicate() const { return std::make_unique<TypeInt32>(); }

void TypeInt32::Dump(std::ostream& os) const { os << "int32"; }

// - type uint32 -----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUint32::Duplicate() const { return std::make_unique<TypeUint32>(); }

void TypeUint32::Dump(std::ostream& os) const { os << "uint32"; }

// - type int64 ------------------------------------------------------------------------------------

std::unique_ptr<Type> TypeInt64::Duplicate() const { return std::make_unique<TypeInt64>(); }

void TypeInt64::Dump(std::ostream& os) const { os << "int64"; }

// - type uint64 -----------------------------------------------------------------------------------

std::unique_ptr<Type> TypeUint64::Duplicate() const { return std::make_unique<TypeUint64>(); }

void TypeUint64::Dump(std::ostream& os) const { os << "uint64"; }

void TypeUint64::GenerateDefaultValue(ExecutionContext* context, code::Code* code) const {
  code->Literal64(0);
}

void TypeUint64::GenerateIntegerLiteral(ExecutionContext* context, code::Code* code,
                                        const IntegerLiteral* literal) const {
  if (literal->negative()) {
    std::stringstream ss;
    ss << "Can't create an integer literal of type " << *this << " with a negative value.";
    context->EmitError(literal->id(), ss.str());
    return;
  }
  code->Literal64(literal->absolute_value());
}

void TypeUint64::LoadVariable(const ExecutionScope* scope, size_t index, Value* value) const {
  const uint64_t* data = reinterpret_cast<const uint64_t*>(scope->Data(index, sizeof(uint64_t)));
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

}  // namespace interpreter
}  // namespace shell
