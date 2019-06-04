// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "src/lib/pkg_url/fuchsia_pkg_url.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  auto char_size = sizeof(uint8_t) / sizeof(char);
  const std::string str(reinterpret_cast<const char*>(data), char_size);

  component::FuchsiaPkgUrl::IsFuchsiaPkgScheme(str);
  component::FuchsiaPkgUrl url;
  url.Parse(str);
  url.GetDefaultComponentCmxPath();
  url.pkgfs_dir_path();
  url.package_path();

  return 0;
}
