// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/callback/managed_container.h"

#include <lib/fit/function.h>

#include <algorithm>

#include "src/lib/callback/scoped_callback.h"

namespace ledger {

ManagedContainer::ManagedContainer() : weak_ptr_factory_(this) {}

ManagedContainer::~ManagedContainer() {}

fit::closure ManagedContainer::ManageElement(std::unique_ptr<Element> element) {
  Element* ptr = element.get();
  managed_elements_.push_back(std::move(element));

  // We use a scoped callback to ManagedContainer to allow the manager to
  // be deleted.
  return callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(), [this, ptr]() {
    auto it = std::find_if(managed_elements_.begin(), managed_elements_.end(),
                           [ptr](const std::unique_ptr<Element>& c) { return c.get() == ptr; });
    FXL_DCHECK(it != managed_elements_.end());
    managed_elements_.erase(it);
    CheckDiscardable();
  });
}

void ManagedContainer::CheckDiscardable() {
  if (managed_elements_.empty() && on_discardable_) {
    on_discardable_();
  }
}

}  // namespace ledger
