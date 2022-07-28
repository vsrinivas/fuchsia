// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "phys/efi/main.h"

#include <stdio.h>

#include "fastboot_tcp.h"
#include "gigaboot/src/netifc.h"
#include "xefi.h"
#include "zircon_boot_ops.h"

// TODO(b/236039205): We need a better solution for allocating buffer for
// loading kernels. The required buffer size for loading the target slot
// kernel is only known when processing the zbi header (in LoadKernel() at
// src/firmware/lib/zircon_boot/zircon_boot.c). Consider adding a
// `get_kernel_load_buffer(size_t kernel_size)` callback function in
// `ZirconBootOps`, instead of asking the application to pass a buffer to
// LoadAndBoot() with no hint.
namespace {
uint8_t kernel_load_buffer[128 * 1024 * 1024];
}

// TODO(b/235489025): The function comes from legacy gigaboot. Implement a
// similar function in C++ and remove this.
extern "C" char key_prompt(const char* valid_keys, int timeout_s);

int main(int argc, char** argv) {
  printf("Gigaboot main\n");

  // TODO(b/235489025): We reuse some legacy C gigaboot code for stuff like network stack.
  // This initializes the global variables the legacy code needs. Once these needed features are
  // re-implemented, remove these dependencies.
  xefi_init(gEfiImageHandle, gEfiSystemTable);

  // The following check/initialize network interface and generate ip6 address.
  if (netifc_open()) {
    printf("netifc: Failed to open network interface\n");
    return 1;
  }

  printf("netifc: network interface opened\n");

  printf("Auto boot in 2 seconds. Press f to enter fastboot.\n");
  // If time out, the first char in the `valid_keys` argument will be returned. Thus
  // we put a random different char here, so that we don't always drop to fastboot.
  char key = key_prompt("0f", 2);
  if (key == 'f') {
    zx::status<> ret = gigaboot::FastbootTcpMain();
    if (ret.is_error()) {
      printf("Fastboot failed\n");
      return 1;
    }
  }

  // TODO(b/235489025): Implement mechanism for setting force recovery. The C gigaboot
  // uses bootbyte (a non-volatile memory), which can be device specific. Consider using
  // the abr one-shot-recovery mechanism.
  ForceRecovery force_recovery_option = kForceRecoveryOff;

  // TODO(b/236039205): Implement logic to construct these arguments for the API. This is currently
  // a placeholder for testing compilation/linking.
  ZirconBootOps zircon_boot_ops = gigaboot::GetZirconBootOps();
  ZirconBootResult res = LoadAndBoot(&zircon_boot_ops, kernel_load_buffer,
                                     sizeof(kernel_load_buffer), force_recovery_option);
  if (res != kBootResultOK) {
    printf("Failed to boot zircon\n");
    return 1;
  }

  return 0;
}
