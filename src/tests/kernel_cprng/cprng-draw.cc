// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This program writes random numbers obtained by zx_cprng_draw to the dlog.
// Output format is cprng-draw{random-number-in-hex}
// The program is intended to be used for testing.

#include <assert.h>
#include <lib/fdio/fd.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/resource.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <string_view>

namespace {

constexpr size_t kRandomDraws = 32;
constexpr size_t kRandomDrawSize = 32;

constexpr const char* kProgramStartMsg = "-- cprng-draw-start --";
constexpr const char* kProgramEndMsg = "-- cprng-draw-end --";

void print_random_string() {
  uint8_t randoms[kRandomDrawSize]{};
  zx_cprng_draw(randoms, sizeof(randoms));

  printf("cprng-draw{");
  for (size_t i = 0; i < sizeof(randoms); i++) {
    printf("%02hhx", randoms[i]);
  }
  printf("}\n");
}
}  // namespace

int main(int argc, char** argv) {
  zx::resource root_resource(zx_take_startup_handle(PA_RESOURCE));
  if (root_resource.get() == ZX_HANDLE_INVALID) {
    return -1;
  }

  zx::debuglog debuglog;
  if (auto status = zx::debuglog::create(root_resource, 0, &debuglog); status != ZX_OK) {
    return status;
  }

  int fd;
  if (auto status = fdio_fd_create(debuglog.get(), &fd); status != ZX_OK) {
    return status;
  }
  if (dup2(fd, STDOUT_FILENO) == -1) {
    return -1;
  }

  printf("%s\n", kProgramStartMsg);

  for (size_t i = 0; i < kRandomDraws; i++) {
    print_random_string();
  }

  printf("%s\n", kProgramEndMsg);
}
