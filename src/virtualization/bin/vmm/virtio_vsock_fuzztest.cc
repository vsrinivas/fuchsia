// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuzzer/FuzzedDataProvider.h>

#include "src/virtualization/bin/vmm/virtio_vsock.h"

// Need to make a fake virtio_vsock connection and start cramming nums in it
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider provider(data, size);

  zx::socket mock_socket;
  ConnectionKey mock_key{0, 0, 0, 0};
  auto conn =
      VirtioVsock::Connection::Create(mock_key, std::move(mock_socket), nullptr, nullptr, nullptr);

  // Now fuzz the set credit function
  auto fuzzed_buf_alloc = provider.ConsumeIntegral<uint32_t>();
  auto fuzzed_fwd_cnt = provider.ConsumeIntegral<uint32_t>();
  conn->SetCredit(fuzzed_buf_alloc, fuzzed_fwd_cnt);
  return 0;
}
