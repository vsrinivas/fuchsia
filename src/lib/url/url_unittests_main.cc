// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/test/test_settings.h>

#include "src/lib/icu_data/cpp/icu_data.h"

int main(int argc, char **argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  if (!icu_data::Initialize()) {
    FXL_LOG(ERROR) << "Unable to set common ICU data. "
                   << "Timezone data unavailable.";
    return 1;
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
