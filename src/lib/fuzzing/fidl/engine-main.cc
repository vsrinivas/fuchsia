// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine.h"
#include "libfuzzer.h"

extern "C" {

int LLVMFuzzerInitialize(int *argc, char ***argv) {
  return Engine::GetInstance()->Initialize(argc, argv);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  return Engine::GetInstance()->TestOneInput(data, size);
}
}
