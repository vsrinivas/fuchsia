// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/io/redirection.h"

#include <zircon/processargs.h>

#include "gtest/gtest.h"
#include "lib/fxl/macros.h"

namespace fsl {
namespace {

TEST(Redirection, CreateRedirectedSocket) {
  zx::socket socket;
  StartupHandle startup_handle;
  zx_status_t status = CreateRedirectedSocket(2, &socket, &startup_handle);

  ASSERT_EQ(ZX_OK, status);
  EXPECT_TRUE(socket);
  EXPECT_EQ(static_cast<uint32_t>(PA_HND(PA_FDIO_PIPE, 2)),
            startup_handle.id);
  EXPECT_TRUE(startup_handle.handle);
}

}  // namespace
}  // namespace fsl
