// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/struct_class.h"

namespace zxdb {

StructClass::StructClass(int tag) : Type(tag) {}
StructClass::~StructClass() = default;

const StructClass* StructClass::AsStructClass() const { return this; }

}  // namespace zxdb
