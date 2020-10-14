// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TIME_LIB_NETWORK_TIME_ROUGHTIME_SERVER_H_
#define SRC_SYS_TIME_LIB_NETWORK_TIME_ROUGHTIME_SERVER_H_

#include <lib/zx/time.h>
#include <protocol.h>
#include <stdint.h>
#include <string.h>

#include <map>
#include <optional>
#include <string>
#include <utility>

namespace time_server {

enum Status {
  OK,
  NOT_SUPPORTED,  // Server conf is invalid/not supported
  BAD_RESPONSE,   // Bad response from server, most probably can't verify
                  // certificate
  NETWORK_ERROR   // Either timeout while poll or error with other network
                  // operations
};

class RoughTimeServer {
 public:
  bool IsValid() const;
  std::pair<Status, std::optional<zx::time_utc>> GetTimeFromServer() const;
  RoughTimeServer(std::string name, std::string address, uint8_t public_key[], int public_key_len)
      : name_(std::move(name)), address_(std::move(address)) {
    if (public_key_len != roughtime::kPublicKeyLength) {
      valid_ = false;
      return;
    }
    valid_ = true;
    memcpy(public_key_, public_key, roughtime::kPublicKeyLength);
  }
  ~RoughTimeServer() = default;

 private:
  bool valid_ = false;
  std::string name_;
  std::string address_;
  uint8_t public_key_[roughtime::kPublicKeyLength];
};

}  // namespace time_server

#endif  // SRC_SYS_TIME_LIB_NETWORK_TIME_ROUGHTIME_SERVER_H_
