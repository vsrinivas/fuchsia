// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/console/console.h"

int main(int argc, char* argv[]) {
  return shell::ConsoleMain(argc, const_cast<const char**>(argv));
}
