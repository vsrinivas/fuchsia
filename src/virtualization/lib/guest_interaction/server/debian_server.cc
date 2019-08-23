// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/lib/guest_interaction/platform_interface/platform_interface.h"
#include "src/virtualization/lib/guest_interaction/server/server_impl.h"

int main(int argc, char** argv) {
  ServerImpl<PosixPlatform> server;
  server.Run();
  return 0;
}
