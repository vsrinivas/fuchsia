// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/setting_schema_definition.h"

#include "garnet/bin/zxdb/client/setting_schema.h"

namespace zxdb {

// System ----------------------------------------------------------------------

const char* ClientSettings::kSymbolPaths = "symbol-paths";
const char* kSymbolPathsDescription = R"(
      List of mapping databases, ELF files or directories for symbol lookup.
      When a directory path is passed, the directory will be enumerated
      non-recursively to index all ELF files within. When a .txt file is passed,
      it will be treated as a mapping database from build ID to file path.
      Otherwise, the path will be loaded as an ELF file.)";

fxl::RefPtr<SettingSchema> CreateSystemSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  schema->AddList(ClientSettings::kSymbolPaths, kSymbolPathsDescription,
                  {});

  return schema;
}


}  // namespace zxdb
