// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TIME_LIB_NETWORK_TIME_TEST_COMMON_H_
#define SRC_SYS_TIME_LIB_NETWORK_TIME_TEST_COMMON_H_

#include "src/lib/fxl/strings/string_printf.h"
#include "third_party/roughtime/protocol.h"

namespace time_server {

#define NETWORK_TIME_TEST_PUBLIC_KEY                                                              \
  0x3b, 0x6a, 0x27, 0xbc, 0xce, 0xb6, 0xa4, 0x2d, 0x62, 0xa3, 0xa8, 0xd0, 0x2a, 0x6f, 0x0d, 0x73, \
      0x65, 0x32, 0x15, 0x77, 0x1d, 0xe2, 0x43, 0xa6, 0x3a, 0xc0, 0x48, 0xa1, 0x8b, 0x59, 0xda,   \
      0x29

// Ed25519 private key used by a test roughtime server. The
// private part consists of all zeros and so is only for use in this example.
constexpr uint8_t kTestPrivateKey[roughtime::kPrivateKeyLength] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, NETWORK_TIME_TEST_PUBLIC_KEY};

// Same as the second half of the private key, but there's no sane way to avoid
// duplicating this code without macros.
constexpr uint8_t kTestPublicKey[roughtime::kPublicKeyLength] = {NETWORK_TIME_TEST_PUBLIC_KEY};

#undef NETWORK_TIME_TEST_PUBLIC_KEY

constexpr uint8_t kWrongPrivateKey[roughtime::kPrivateKeyLength] = {};

// Copied from zircon/lib/fidl/array_to_string
std::string to_hex_string(const uint8_t* data, size_t size) {
  constexpr char kHexadecimalCharacters[] = "0123456789abcdef";
  std::string ret;
  ret.reserve(size * 2);
  for (size_t i = 0; i < size; i++) {
    unsigned char c = data[i];
    ret.push_back(kHexadecimalCharacters[c >> 4]);
    ret.push_back(kHexadecimalCharacters[c & 0xf]);
  }
  return ret;
}

// Creates a client config for a roughtime server listening on [::1]:port
std::string local_client_config(uint16_t port) {
  // Note that the host must explicitly be "::1". "localhost" is
  // misinterpreted as implying IPv4.
  return fxl::StringPrintf(
      R"(
{
  "servers":
  [
    {
      "name": "Local",
      "publicKey": "%s",
      "addresses":
        [
          {
            "address": "::1:%d"
          }
        ]
    }
  ]
})",
      to_hex_string(kTestPublicKey, roughtime::kPublicKeyLength).c_str(), port);
}

}  //  namespace time_server

#endif  // SRC_SYS_TIME_LIB_NETWORK_TIME_TEST_COMMON_H_
