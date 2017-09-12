// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/tasks/message_loop.h"
#include "garnet/bin/network/network_service_delegate.h"

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  network::NetworkServiceDelegate delegate;
  loop.Run();
  return 0;
}
