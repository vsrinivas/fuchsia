// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rust_url.h"

zx_status_t RustUrl::Parse(const std::string& input) {
  void* out;
  const zx_status_t res = rust_url_parse(input.c_str(), &out);
  if (res == ZX_OK) {
    inner_ = out;
  }
  return res;
}

std::string RustUrl::Domain() {
  std::string domain;

  if (inner_) {
    char* raw_domain = rust_url_get_domain(inner_);
    if (raw_domain) {
      domain = std::string(raw_domain);
      rust_url_free_domain(raw_domain);
    }
  }

  return domain;
}
