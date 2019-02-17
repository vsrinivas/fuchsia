// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/testing/test_util.h"

#include <lib/fdio/limits.h>
#include <lib/fdio/util.h>

namespace component {
namespace testing {

zx::channel OpenAsDirectory(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> node) {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK)
    return zx::channel();
  if (vfs->ServeDirectory(std::move(node), std::move(h1)) != ZX_OK)
    return zx::channel();
  return h2;
}

}  // namespace testing
}  // namespace component
