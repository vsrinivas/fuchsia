// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/chain_impl.h"

namespace modular {

ChainImpl::ChainImpl() = default;
ChainImpl::~ChainImpl() = default;

void ChainImpl::Connect(fidl::InterfaceRequest<Chain> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ChainImpl::GetKeys(const GetKeysCallback& done) {
  fidl::Array<fidl::String> keys = fidl::Array<fidl::String>::New(0);
  done(std::move(keys));
}

void ChainImpl::GetLink(const fidl::String& key, fidl::InterfaceRequest<Link> request) {
  // |request| closes when out of scope.
}

}  // namespace modular
