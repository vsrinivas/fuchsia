// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "message-group.h"

#include <lib/ddk/debug.h>
#include <zircon/status.h>

#include <fbl/auto_lock.h>

#include "server.h"

zx_status_t MessageGroup::ExpectResponses(int response_count, int request_count,
                                          std::optional<reqid_t> request_id) {
  fbl::AutoLock guard(&lock_);
  zx_status_t status = ZX_OK;
  if (pending_ && request_count != 0) {
    response_.status = ZX_ERR_IO;
    zxlogf(WARNING,
           "Attempted to add more requests to finalised transaction group: req=%d group=%d",
           response_.reqid, response_.group);
    status = ZX_ERR_IO;
  }

  if (response_.status != ZX_OK) {
    // The operation failed already, don't bother with any more transactions.
    status = ZX_ERR_IO;
  }

  op_count_ += response_count;
  response_.count += request_count;

  if (request_id != std::nullopt) {
    response_.reqid = *request_id;
    pending_ = true;
  }
  return status;
}

void MessageGroup::Complete(const zx_status_t status) {
  fbl::AutoLock guard(&lock_);

  if (status != ZX_OK && response_.status == ZX_OK) {
    zxlogf(WARNING, "Transaction completed with error status: %s", zx_status_get_string(status));
    response_.status = status;
  }

  if (--op_count_ == 0 && pending_) {
    server_.SendResponse(response_);

    pending_ = false;
    response_.count = 0;
    response_.reqid = 0;
    response_.status = ZX_OK;
  }
}
