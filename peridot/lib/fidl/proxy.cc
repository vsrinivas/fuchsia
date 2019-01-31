// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/fidl/proxy.h"

#include <lib/fxl/logging.h>

namespace modular {

ProxySet::ProxySet() = default;

ProxySet::~ProxySet() = default;

void ProxySet::Drop(ProxyBase* const proxy) {
  auto i = std::remove_if(
      proxies_.begin(), proxies_.end(),
      [proxy](std::unique_ptr<ProxyBase>& p) { return proxy == p.get(); });
  FXL_DCHECK(i != proxies_.end());
  proxies_.erase(i, proxies_.end());
}

ProxyBase::ProxyBase(ProxySet* const set) : set_(set) {}

ProxyBase::~ProxyBase() = default;

void ProxyBase::Drop() { set_->Drop(this); }

}  // namespace modular
