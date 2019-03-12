// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/type.h"

#include "garnet/bin/zxdb/symbols/symbol_utils.h"
#include "lib/fxl/logging.h"

namespace zxdb {

Type::Type(DwarfTag kind) : Symbol(kind) {}
Type::~Type() = default;

const Type* Type::AsType() const { return this; }

const Type* Type::GetConcreteType() const { return this; }

}  // namespace zxdb
