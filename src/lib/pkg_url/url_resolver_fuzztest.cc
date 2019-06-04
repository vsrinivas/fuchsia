// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "src/lib/pkg_url/url_resolver.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  auto char_size = sizeof(uint8_t) / sizeof(char);
  const std::string str(reinterpret_cast<const char*>(data), char_size);

  component::CanonicalizeURL(str);
  component::GetSchemeFromURL(str);
  component::GetPathFromURL(str);
  component::GetURLFromPath(str);

  return 0;
}
