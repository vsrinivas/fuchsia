// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "banjo/attributes.h"

namespace banjo {

bool HasSimpleLayout(const flat::Decl* decl) {
    return decl->GetAttribute("Layout") == "Simple";
}

bool IsDefaultProtocol(const flat::Decl* decl) {
    return decl->GetAttribute("Layout") == "ddk-protocol" &&
           decl->HasAttribute("DefaultProtocol");
}

} // namespace banjo
