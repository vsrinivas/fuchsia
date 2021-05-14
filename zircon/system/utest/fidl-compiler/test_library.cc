// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_library.h"

TestLibrary WithLibraryZx(const std::string& source_code) {
  return WithLibraryZx(source_code, fidl::ExperimentalFlags());
}

TestLibrary WithLibraryZx(const std::string& source_code, fidl::ExperimentalFlags flags) {
  TestLibrary main_lib(source_code, flags);

  std::string zx = R"FIDL(
deprecated_syntax;
library zx;

enum obj_type : uint32 {
    NONE = 0;
    PROCESS = 1;
    THREAD = 2;
    VMO = 3;
    CHANNEL = 4;
    EVENT = 5;
    PORT = 6;
};

bits rights : uint32 {
    DUPLICATE = 0x00000001;
    TRANSFER = 0x00000002;
};

resource_definition handle : uint32 {
    properties {
        obj_type subtype;
        rights rights;
    };
};
)FIDL";

  // Regardless of what the caller wants for their library, always allow handle
  // rights and the new syntax for the ZX library
  auto zx_flags = flags;
  zx_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  zx_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);
  TestLibrary zx_lib("zx.fidl", zx, main_lib.OwnedShared(), zx_flags);
  zx_lib.Compile();
  main_lib.AddDependentLibrary(std::move(zx_lib));

  return main_lib;
}
