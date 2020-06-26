// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

// Simple program that throws an exception.

int main() {
  volatile char *segfault = 0;
  *segfault = 1;
  return *segfault;
}
