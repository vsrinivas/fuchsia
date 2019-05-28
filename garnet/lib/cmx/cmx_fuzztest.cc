// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cmx/cmx.h"

#include <string>

#include <fcntl.h>
#include <unistd.h>

#include "garnet/public/lib/json/json_parser.h"
#include "src/lib/files/scoped_temp_dir.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  auto char_size = sizeof(uint8_t) / sizeof(char);
  auto str_size = size / char_size;
  const char* cstr = reinterpret_cast<const char*>(data);
  const std::string str(cstr, str_size);
  component::CmxMetadata cmx;
  json::JSONParser json_parser;
  files::ScopedTempDir tmp_dir;
  std::string json_basename;
  std::string json_path;

  if (tmp_dir.NewTempFileWithData(str, &json_path)) {
    const int dirfd = open(tmp_dir.path().c_str(), O_RDONLY);
    cmx.ParseFromFileAt(dirfd, json_basename, &json_parser);
    close(dirfd);
  }

  // TODO(markdittmer): Should this test start with a "realistic" CmxMetadata?
  if (str_size >= 10) {
    auto chunk_size = str_size / 10;
    for (auto i = 0; i < 10; i++) {
      const std::string facet(cstr + (i * chunk_size), chunk_size);
      cmx.GetFacet(facet);
    }
  }

  return 0;
}
