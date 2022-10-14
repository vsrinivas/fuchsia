// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_LIB_MEXEC_TESTING_ZBI_TEST_ENTRY_H_
#define SRC_BRINGUP_LIB_MEXEC_TESTING_ZBI_TEST_ENTRY_H_

#include <lib/zx/resource.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

class ZbiTestEntry {
 public:
  zx::status<> Init(int argc, char** argv);

  zx::unowned_resource mexec_resource() { return mexec_resource_.borrow(); }
  zx::vmo& kernel_zbi() { return kernel_zbi_; }
  zx::vmo& data_zbi() { return data_zbi_; }

 private:
  zx::resource mexec_resource_;
  zx::vmo kernel_zbi_, data_zbi_;
};

#endif  // SRC_BRINGUP_LIB_MEXEC_TESTING_ZBI_TEST_ENTRY_H_
