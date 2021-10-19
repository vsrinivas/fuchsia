// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

int main() {
  volatile int* p = new int;
  delete p;

  // Should be captured by GWP-ASan as use-after-free.
  *p = 42;

  return 0;
}
