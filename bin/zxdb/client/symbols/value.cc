// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/value.h"

namespace zxdb {

Value::Value(int tag) : Symbol(tag) {}
Value::~Value() = default;

const Value* Value::AsValue() const { return this; }

}  // namespace zxdb
