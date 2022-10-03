// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_RUST_URL_RUST_URL_H_
#define SRC_LIB_RUST_URL_RUST_URL_H_

#include <zircon/types.h>

#include <string>

// APIs exposed by our Rust static library -- should not be called directly.
extern "C" zx_status_t rust_url_parse(const char* input, void** out);
extern "C" void rust_url_free(void* url);

extern "C" char* rust_url_get_domain(const void* url);
extern "C" void rust_url_free_domain(char* domain);

class RustUrl {
 public:
  ~RustUrl() {
    if (inner_) {
      rust_url_free(inner_);
    }
  }

  // Parse a URL. Must be called after construction and before any other methods are called.
  //
  // If this method does not return ZX_OK, no subsequent methods will succeed.
  zx_status_t Parse(const std::string& input);

  // Return the domain of this URL, if any. Should be called only after successful calls to Parse().
  //
  // Returns an empty string if no domain is present.
  std::string Domain();

 private:
  // Managed by the Rust side.
  //
  // Explicitly initialized to nullptr so we can use that as a sentinel for parsing.
  void* inner_ = nullptr;
};

#endif  // SRC_LIB_RUST_URL_RUST_URL_H_
