// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_BANJO_INCLUDE_BANJO_ATTRIBUTES_H_
#define ZIRCON_SYSTEM_HOST_BANJO_INCLUDE_BANJO_ATTRIBUTES_H_

#include "flat_ast.h"

namespace banjo {

bool HasSimpleLayout(const flat::Decl* decl);
bool IsDefault(const flat::Decl* decl);

} // namespace banjo

#endif // ZIRCON_SYSTEM_HOST_BANJO_INCLUDE_BANJO_ATTRIBUTES_H_
