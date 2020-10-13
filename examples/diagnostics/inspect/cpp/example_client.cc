// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>

#include <iostream>
#include <string>

#include <fidl/examples/echo/cpp/fidl.h>
#include <src/lib/files/path.h>

#include "lib/fidl/cpp/string.h"

int main(int argc, const char** argv) {
  if (argc < 2) {
    printf(
        "Usage: %s <path> <strings...>\nOpens <path> as an echo server and "
        "sends <strings>\n",
        argv[0]);
    return -1;
  }

  fidl::examples::echo::EchoSyncPtr echo;

  std::string file_name = files::AbsolutePath(argv[1]);
  zx_status_t status =
      fdio_service_connect(file_name.c_str(), echo.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    printf("Failed to open %s, %d\n", file_name.c_str(), status);
    return -1;
  }

  for (int i = 2; i < argc; i++) {
    fidl::StringPtr result;
    echo->EchoString(argv[i], &result);
    printf("Response: %s\n", result->c_str());
  }

  printf("Done sending strings, close this component to disconnect.\n");
  sleep(1000000000);

  return 0;
}
