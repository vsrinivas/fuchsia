// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/examples/simple/simple_impl.h"

using ::fuchsia::modular::examples::simple::Simple;

namespace simple {

SimpleImpl::SimpleImpl() = default;

SimpleImpl::~SimpleImpl() = default;

void SimpleImpl::Connect(fidl::InterfaceRequest<Simple> request) {
  bindings_.AddBinding(this, std::move(request));
}

void SimpleImpl::SetMessageQueue(std::string queue_token) {
  token_ = queue_token;
}

}  // namespace simple
