// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/types.h"

#include <ostream>

namespace shell {
namespace interpreter {

void TypeBool::Dump(std::ostream& os) const { os << "bool"; }

void TypeChar::Dump(std::ostream& os) const { os << "char"; }

void TypeString::Dump(std::ostream& os) const { os << "string"; }

void TypeInt8::Dump(std::ostream& os) const { os << "int8"; }

void TypeUint8::Dump(std::ostream& os) const { os << "uint8"; }

void TypeInt16::Dump(std::ostream& os) const { os << "int16"; }

void TypeUint16::Dump(std::ostream& os) const { os << "uint16"; }

void TypeInt32::Dump(std::ostream& os) const { os << "int32"; }

void TypeUint32::Dump(std::ostream& os) const { os << "uint32"; }

void TypeInt64::Dump(std::ostream& os) const { os << "int64"; }

void TypeUint64::Dump(std::ostream& os) const { os << "uint64"; }

void TypeInteger::Dump(std::ostream& os) const { os << "integer"; }

void TypeFloat32::Dump(std::ostream& os) const { os << "float32"; }

void TypeFloat64::Dump(std::ostream& os) const { os << "float64"; }

}  // namespace interpreter
}  // namespace shell
