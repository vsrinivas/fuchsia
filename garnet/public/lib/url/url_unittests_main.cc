// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/logging.h"
#include "third_party/icu/source/common/unicode/errorcode.h"
#include "third_party/icu/source/common/unicode/udata.h"

constexpr char kIcuDataPath[] = "/pkg/data/icudtl.dat";

int main(int argc, char **argv) {
  fsl::SizedVmo icu_data;
  if (!fsl::VmoFromFilename(kIcuDataPath, &icu_data)) {
    FXL_LOG(ERROR) << "Unable to load ICU data. Timezone data unavailable.";
    return 1;
  }

  // Maps the ICU VMO into this process.
  uintptr_t icu_data_addr = 0;
  if (zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ, 0, icu_data.vmo().get(),
                  0, icu_data.size(), &icu_data_addr) != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to map ICU data into process.";
    return 1;
  }

  // Initialize ICU with this data.
  UErrorCode icu_set_data_status = U_ZERO_ERROR;
  udata_setCommonData(reinterpret_cast<void *>(icu_data_addr),
                      &icu_set_data_status);
  if (icu_set_data_status != U_ZERO_ERROR) {
    FXL_LOG(ERROR) << "Unable to set common ICU data. "
                   << "Timezone data unavailable.";
    return 1;
  }

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
