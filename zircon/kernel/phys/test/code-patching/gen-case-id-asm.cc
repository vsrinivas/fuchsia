// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <hwreg/asm.h>

#include "test.h"

// Generate the <case-id-asm.h> header used in the code to be patched.

int main(int argc, char** argv) {
  return hwreg::AsmHeader()
      .Macro("ADD_ONE_CASE_ID", kAddOneCaseId)
      .Macro("ADD_ONE_PATCH_SIZE", kAddOnePatchSize)
      .Main(argc, argv);
}
