// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/console_main.h"

int main(int argc, char* argv[]) {
  return zxdb::ConsoleMain(argc, const_cast<const char**>(argv));
}
