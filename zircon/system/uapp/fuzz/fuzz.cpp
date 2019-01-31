// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuzz-utils/fuzzer.h>

int main(int argc, char* argv[]) {
    return fuzzing::Fuzzer::Main(argc, argv);
}
