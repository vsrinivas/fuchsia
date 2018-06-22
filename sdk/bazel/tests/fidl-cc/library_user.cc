// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bazel/examples/simple/cpp/fidl.h>

int main(int argc, const char** argv) {
  bazel::examples::simple::Hello object;
  object.world = 314;
  bazel::examples::simple::Hello other_object;
  return object == other_object ? 0 : 1;
}
