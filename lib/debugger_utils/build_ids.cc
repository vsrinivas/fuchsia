// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build_ids.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "util.h"

namespace debugserver {

bool BuildIdTable::ReadIdsFile(const std::string& file) {
  FXL_LOG(INFO) << "Loading ids data from " << file;

  FILE* f = fopen(file.c_str(), "r");
  if (!f) {
    FXL_LOG(ERROR) << "error opening ids file, " << ErrnoString(errno);
    return false;
  }

  char* line = nullptr;
  size_t linelen = 0;
  int lineno = 1;

  for (; getline(&line, &linelen, f) > 0; ++lineno) {
    size_t n = strlen(line);
    if (n > 0 && line[n - 1] == '\n')
      line[n - 1] = '\0';
    FXL_VLOG(2) << fxl::StringPrintf("read %d: %s", lineno, line);

#define MAX_LINE_LEN 1024
    if (linelen > MAX_LINE_LEN) {
      FXL_VLOG(2) << fxl::StringPrintf("%d: ignoring: %s", lineno, line);
      continue;
    }

    if (!strcmp(line, "\n"))
      continue;
    if (line[0] == '#')
      continue;

    char build_id[linelen];
    char path[linelen];
    if (sscanf(line, "%s %s", build_id, path) == 2) {
      AddBuildId(files::GetDirectoryName(file), build_id, path);
    } else {
      FXL_VLOG(2) << fxl::StringPrintf("%d: ignoring: %s", lineno, line);
    }
  }

  free(line);
  fclose(f);

  return true;
}

void BuildIdTable::AddBuildId(const std::string& file_dir,
                              const std::string& build_id,
                              const std::string& path) {
  std::string abs_path;

  // Convert relative paths to absolute ones.
  if (path[0] != '/') {
    std::string abs_file_dir = files::AbsolutePath(file_dir);
    abs_path = abs_file_dir + "/" + path;
  } else {
    abs_path = path;
  }
  FXL_VLOG(2) << fxl::StringPrintf("build_id %s, file %s", build_id.c_str(),
                                   abs_path.c_str());
  BuildId build_id_object;
  build_id_object.build_id = build_id;
  build_id_object.file = abs_path;
  build_ids_.push_back(build_id_object);
}

const BuildId* BuildIdTable::LookupBuildId(const std::string& bid) {
  for (const auto& b : build_ids_) {
    if (bid == b.build_id)
      return &b;
  }

  return nullptr;
}

}  // namespace debugserver
