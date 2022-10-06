// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/amlogic-display/vout.h"

#include "zxtest/zxtest.h"

TEST(Vout, UnsupportedHdmi) {
  auto vout = std::make_unique<amlogic_display::Vout>();
  vout->InitHdmi(nullptr);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, vout->PowerOff().status_value());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, vout->PowerOn().status_value());
}
