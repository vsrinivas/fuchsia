// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon_boot/zircon_boot.h>
#include <stdio.h>

#include "zircon_boot_ops.h"

int main(int argc, char** argv) {
  printf("Gigaboot main\n");

  // TODO(b/235489025): Implement mechanism for setting force recovery. The C gigaboot
  // uses bootbyte (a non-volatile memory), which can be device specific. Consider using
  // the abr one-shot-recovery mechanism.
  ForceRecovery force_recovery_option = kForceRecoveryOff;

  // TODO(b/236039205): Implement logic to construct these arguments for the API. This is currently
  // a placeholder for testing compilation/linking.
  ZirconBootOps zircon_boot_ops = gigaboot::GetZirconBootOps();
  ZirconBootResult res = LoadAndBoot(&zircon_boot_ops, nullptr, 0, force_recovery_option);
  if (res != kBootResultOK) {
    printf("Failed to boot zircon\n");
    return 1;
  }
  return 0;
}
