// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/test/stub_netbuf.h"

#include <zxtest/zxtest.h>

namespace wlan {
namespace brcmfmac {

StubNetbuf::StubNetbuf() = default;

StubNetbuf::StubNetbuf(const void* data, size_t size, zx_status_t expected_status)
    : expected_status_(expected_status) {
  data_ = data;
  size_ = size;
}

StubNetbuf::~StubNetbuf() {
  EXPECT_EQ(nullptr, data_);
  EXPECT_EQ(0u, size_);
}

void StubNetbuf::Return(zx_status_t status) {
  Netbuf::Return(status);
  EXPECT_EQ(expected_status_, status);
}

}  // namespace brcmfmac
}  // namespace wlan
