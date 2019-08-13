// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_TOKEN_MANAGER_H_
#define SRC_LEDGER_BIN_APP_TOKEN_MANAGER_H_

#include <lib/fit/function.h>

#include <trace/event.h>

#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ledger {

// Issues |ExpiringToken|s and calls its on-empty callback (if set) when the last of its outstanding
// issued |ExpiringToken|s is deleted.
class TokenManager {
 public:
  TokenManager();
  ~TokenManager();

  // Sets the on_empty callback, to be called every time this object becomes
  // empty.
  void set_on_empty(fit::closure on_empty_callback);

  ExpiringToken CreateToken();

  // Checks and returns whether there are no active external or internal
  // requests.
  bool IsEmpty();

 private:
  ssize_t outstanding_token_count;
  fit::closure on_empty_callback_;
  // Must be the last member.
  fxl::WeakPtrFactory<TokenManager> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TokenManager);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_TOKEN_MANAGER_H_
