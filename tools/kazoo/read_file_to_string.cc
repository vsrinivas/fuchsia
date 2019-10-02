// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/read_file_to_string.h"

#include <stdio.h>

#include "src/lib/fxl/logging.h"

bool ReadFileToString(const std::string& path, std::string* result) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) {
    FXL_LOG(ERROR) << "couldn't open " << path;
    return false;
  }
  fseek(fp, 0, SEEK_END);
  result->resize(ftell(fp));
  rewind(fp);
  fread(&(*result)[0], 1, result->size(), fp);
  fclose(fp);
  return true;
}
