// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_TEST_TEST_UTILS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_TEST_TEST_UTILS_H_

// This file contains various test utilities and classes for MSGBUF tests.

#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_structs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/netbuf.h"

namespace wlan {
namespace brcmfmac {

// A Netbuf implementation for testing.
class TestNetbuf : public Netbuf {
 public:
  TestNetbuf();
  explicit TestNetbuf(const void* data, size_t size, zx_status_t expected_status);
  ~TestNetbuf() override;

  void Return(zx_status_t status) override;

 private:
  std::vector<char> allocation_;
  zx_status_t expected_status_ = ZX_OK;
};

// Get a MSGBUF struct pointer for the given data buffer, checking size and type.  If the buffer is
// too small, a test expectation is failed and nullptr is returned.
template <typename T>
constexpr const T* GetMsgStruct(const void* buffer, size_t size) {
  const T* const header = reinterpret_cast<const T*>(buffer);
  EXPECT_LE(sizeof(T), size);
  if (sizeof(T) > size) {
    return nullptr;
  }
  EXPECT_EQ(T::kMsgType, header->msg.msgtype);
  if (T::kMsgType != header->msg.msgtype) {
    return nullptr;
  }
  return header;
}

// Compare a container to multiple other concatenated containers.
bool ConcatenatedEquals(const std::initializer_list<std::string_view>& lhs,
                        const std::initializer_list<std::string_view>& rhs);

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_TEST_TEST_UTILS_H_
