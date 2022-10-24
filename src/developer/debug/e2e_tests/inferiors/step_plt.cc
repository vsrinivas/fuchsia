// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <memory>

std::unique_ptr<int> p;

int main() {
  puts("1");
  p = std::make_unique<int>(0);
  puts("2");
}
