// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// TODO(porce): Design the scope of the authoritative use of enums
// across code stacks. Revisit when FIDL2 is available.

// See FIDL's definitions here
// //garnet/public/lib/wlan/fidl/wlan_mlme.fidl

// Variable name styles in consistence with FIDL.
enum BSSTypes {
    INFRASTRUCTURE,
    PERSONAL,
    INDEPENDENT,
    MESH,
    ANY_BSS,
    LAST = ANY_BSS,
};
