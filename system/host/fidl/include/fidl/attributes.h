// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ATTRIBUTES_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ATTRIBUTES_H_

#include "flat_ast.h"

namespace fidl {

bool HasSimpleLayout(const flat::Decl* decl);

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ATTRIBUTES_H_
