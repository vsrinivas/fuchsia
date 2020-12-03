// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/boot-options/boot-options.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <string>

#include <fuzzer/FuzzedDataProvider.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  BootOptions options;
  FuzzedDataProvider provider(data, size);

  char* buff = nullptr;
  size_t buff_size = 0;
  FILE* f = open_memstream(&buff, &buff_size);
  ZX_ASSERT(f);

  std::string key_to_show = provider.ConsumeRandomLengthString();
  bool show_defaults = provider.ConsumeBool();
  std::string data_to_set = provider.ConsumeRemainingBytesAsString();

  options.SetMany(data_to_set, f);
  options.Show(show_defaults, f);
  options.Show(key_to_show, show_defaults, f);

  fclose(f);
  free(buff);
  return 0;
}
