// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_PROVIDER_PUBLIC_USER_ID_PROVIDER_H_
#define PERIDOT_BIN_LEDGER_P2P_PROVIDER_PUBLIC_USER_ID_PROVIDER_H_

#include <lib/fit/function.h>

#include "lib/fxl/macros.h"

namespace p2p_provider {
// UserIdProvider abstracts the unique User ID shared accross all devices of a
// user and used to establish a connection.
class UserIdProvider {
 public:
  enum class Status {
    OK,
    ERROR,
  };

  UserIdProvider() {}
  virtual ~UserIdProvider() {}

  // GetUserId calls its callback with the user id.
  virtual void GetUserId(fit::function<void(Status, std::string)> callback) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(UserIdProvider);
};

}  // namespace p2p_provider

#endif  // PERIDOT_BIN_LEDGER_P2P_PROVIDER_PUBLIC_USER_ID_PROVIDER_H_
