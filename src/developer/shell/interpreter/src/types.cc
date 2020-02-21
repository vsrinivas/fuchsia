// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/types.h"

#include <ostream>
#include <string>

#include "src/developer/shell/interpreter/src/scope.h"

namespace shell {
namespace interpreter {

// - type undefined --------------------------------------------------------------------------------

void TypeUndefined::Dump(std::ostream& os) const { os << "undefined"; }

// - type bool -------------------------------------------------------------------------------------

void TypeBool::Dump(std::ostream& os) const { os << "bool"; }

// - type char -------------------------------------------------------------------------------------

void TypeChar::Dump(std::ostream& os) const { os << "char"; }

// - type string -----------------------------------------------------------------------------------

void TypeString::Dump(std::ostream& os) const { os << "string"; }

// - type int8 -------------------------------------------------------------------------------------

void TypeInt8::Dump(std::ostream& os) const { os << "int8"; }

// - type uint8 ------------------------------------------------------------------------------------

void TypeUint8::Dump(std::ostream& os) const { os << "uint8"; }

// - type int16 ------------------------------------------------------------------------------------

void TypeInt16::Dump(std::ostream& os) const { os << "int16"; }

// - type uint16 -----------------------------------------------------------------------------------

void TypeUint16::Dump(std::ostream& os) const { os << "uint16"; }

// - type int32 ------------------------------------------------------------------------------------

void TypeInt32::Dump(std::ostream& os) const { os << "int32"; }

// - type uint32 -----------------------------------------------------------------------------------

void TypeUint32::Dump(std::ostream& os) const { os << "uint32"; }

// - type int64 ------------------------------------------------------------------------------------

void TypeInt64::Dump(std::ostream& os) const { os << "int64"; }

void TypeInt64::CreateVariable(ExecutionContext* context, Scope* scope, NodeId id,
                               const std::string& name) const {
  scope->CreateInt64Variable(id, name);
}

// - type uint64 -----------------------------------------------------------------------------------

void TypeUint64::Dump(std::ostream& os) const { os << "uint64"; }

void TypeUint64::CreateVariable(ExecutionContext* context, Scope* scope, NodeId id,
                                const std::string& name) const {
  scope->CreateUint64Variable(id, name);
}

// - type integer ----------------------------------------------------------------------------------

void TypeInteger::Dump(std::ostream& os) const { os << "integer"; }

// - type float32 ----------------------------------------------------------------------------------

void TypeFloat32::Dump(std::ostream& os) const { os << "float32"; }

// - type float64 ----------------------------------------------------------------------------------

void TypeFloat64::Dump(std::ostream& os) const { os << "float64"; }

}  // namespace interpreter
}  // namespace shell
