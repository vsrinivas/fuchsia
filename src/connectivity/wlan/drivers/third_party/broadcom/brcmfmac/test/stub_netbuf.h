// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TEST_STUB_NETBUF_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TEST_STUB_NETBUF_H_

#include <zircon/types.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/netbuf.h"

namespace wlan {
namespace brcmfmac {

// A stub Netbuf implementation for testing, which includes an expected Return() status.
class StubNetbuf : public Netbuf {
 public:
 public:
  StubNetbuf();
  explicit StubNetbuf(const void* data, size_t size, zx_status_t expected_status);
  ~StubNetbuf() override;

  void Return(zx_status_t status) override;

 private:
  zx_status_t expected_status_ = ZX_OK;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TEST_STUB_NETBUF_H_
