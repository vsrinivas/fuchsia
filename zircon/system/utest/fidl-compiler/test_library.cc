// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_library.h"

TestLibrary WithLibraryZx(const std::string& source_code) {
  return WithLibraryZx(source_code, fidl::ExperimentalFlags());
}

TestLibrary WithLibraryZx(const std::string& source_code, fidl::ExperimentalFlags flags) {
  return WithLibraryZx("example.fidl", source_code, flags);
}

TestLibrary WithLibraryZx(const std::string& filename, const std::string& source_code) {
  return WithLibraryZx(filename, source_code, fidl::ExperimentalFlags());
}

TestLibrary WithLibraryZx(const std::string& filename, const std::string& source_code,
                          fidl::ExperimentalFlags flags) {
  TestLibrary main_lib(filename, source_code, flags);

  std::string zx = R"FIDL(
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
)FIDL";

  TestLibrary zx_lib("zx.fidl", zx, main_lib.OwnedShared(), flags);
  zx_lib.Compile();
  main_lib.AddDependentLibrary(std::move(zx_lib));

  return main_lib;
}
