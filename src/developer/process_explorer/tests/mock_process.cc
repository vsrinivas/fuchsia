// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper program launched in the test.

#include <unistd.h>

#include <thread>

int main(int argc, char** argv) {
  close(STDOUT_FILENO);
  char buffer[1];
  read(STDIN_FILENO, buffer, 1);
}
