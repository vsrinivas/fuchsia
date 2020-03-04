// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/interpreter/src/schema.h"

#include <ostream>

#include "src/developer/shell/interpreter/src/interpreter.h"
#include "src/developer/shell/interpreter/src/nodes.h"
#include "src/developer/shell/interpreter/src/types.h"

namespace shell::interpreter {

std::unique_ptr<Type> ObjectSchema::GetType() const { return std::make_unique<TypeObject>(this); }

}  // namespace shell::interpreter
