// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/tests/test_library.h"

#include <zircon/assert.h>

void SharedAmongstLibraries::AddLibraryZx() {
  TestLibrary zx_lib(this, "zx.fidl", R"FIDL(
library zx;

type obj_type = enum : uint32 {
    NONE = 0;
    PROCESS = 1;
    THREAD = 2;
    VMO = 3;
    CHANNEL = 4;
    EVENT = 5;
    PORT = 6;
};

type rights = bits : uint32 {
    DUPLICATE = 0x00000001;
    TRANSFER = 0x00000002;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
        rights rights;
    };
};
)FIDL");
  ZX_ASSERT_MSG(zx_lib.Compile(), "failed to compile library zx");
}

void SharedAmongstLibraries::AddLibraryFdf() {
  TestLibrary fdf_lib(this, "fdf.fidl", R"FIDL(
library fdf;

type obj_type = enum : uint32 {
  CHANNEL = 1;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
    };
};
)FIDL");
  ZX_ASSERT_MSG(fdf_lib.Compile(), "failed to compile library fdf");
}
