// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/story_manager/transaction.h"

#include "lib/ftl/logging.h"

namespace modular {

void TransactionContainer::Hold(Transaction* const t) {
  transactions_.emplace_back(t);
}

void TransactionContainer::Drop(Transaction* const t) {
  auto it = std::remove_if(
      transactions_.begin(), transactions_.end(),
      [t](const std::unique_ptr<Transaction>& p) { return p.get() == t; });
  FTL_DCHECK(it != transactions_.end());
  transactions_.erase(it, transactions_.end());
}

Transaction::Transaction(TransactionContainer* const container)
    : container_(container) {
  container_->Hold(this);
}

void Transaction::Done() {
  container_->Drop(this);
}

}  // namespace modular
