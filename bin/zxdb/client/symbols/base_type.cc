// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/base_type.h"

namespace zxdb {

// Storage for constants.
const int BaseType::kBaseTypeNone;
const int BaseType::kBaseTypeAddress;
const int BaseType::kBaseTypeBoolean;
const int BaseType::kBaseTypeFloat;
const int BaseType::kBaseTypeSigned;
const int BaseType::kBaseTypeSignedChar;
const int BaseType::kBaseTypeUnsigned;
const int BaseType::kBaseTypeUnsignedChar;
const int BaseType::kBaseTypeUTF;

BaseType::BaseType() : Type(Symbol::kTagBaseType) {}
BaseType::~BaseType() = default;

const BaseType* BaseType::AsBaseType() const { return this; }

}  // namespace zxdb
