// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The sole purpose of this is to publish a vmo with known contents to the provided
// '/svc' handle through the __sanitizer_publish_data.

#include <lib/zx/vmo.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/sanitizer.h>

int main(int argc, const char**) {
  zx::vmo data;

  ZX_ASSERT(zx::vmo::create(4096, 0, &data) == ZX_OK);
  ZX_ASSERT(data.write("Hello World!", 0, 12) == ZX_OK);

  __sanitizer_publish_data("data-provider", data.release());
  return 0;
}
