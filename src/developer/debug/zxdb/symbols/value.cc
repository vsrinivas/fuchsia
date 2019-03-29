// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/value.h"

namespace zxdb {

Value::Value(DwarfTag tag) : Symbol(tag) {}
Value::Value(DwarfTag tag, const std::string& assigned_name, LazySymbol type)
    : Symbol(tag), assigned_name_(assigned_name), type_(std::move(type)) {}
Value::~Value() = default;

const Value* Value::AsValue() const { return this; }

}  // namespace zxdb
