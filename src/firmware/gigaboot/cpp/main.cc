// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon_boot/zircon_boot.h>
#include <stdio.h>

int main(int argc, char** argv) {
  printf("Gigaboot main\n");

  // TODO(b/236039205): Implement logic to construct these arguments for the API. This is currently
  // a placeholder for testing compilation/linking.
  ZirconBootResult res = LoadAndBoot(nullptr, nullptr, 0, kForceRecoveryOff);
  if (res != kBootResultOK) {
    printf("Failed to boot zircon\n");
    return 1;
  }
  return 0;
}
