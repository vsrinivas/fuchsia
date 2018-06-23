// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/attributes.h"

namespace fidl {

bool HasSimpleLayout(const flat::Decl* decl) {
    return decl->GetAttribute("Layout") == "Simple";
}

} // namespace fidl
