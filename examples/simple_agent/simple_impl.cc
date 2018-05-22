// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/examples/simple_agent/simple_impl.h"

namespace simple_agent {

SimpleImpl::SimpleImpl() {}

SimpleImpl::~SimpleImpl() {}

void SimpleImpl::Connect(fidl::InterfaceRequest<Simple> request) {
  bindings_.AddBinding(this, std::move(request));
}

void SimpleImpl::SetMessageQueue(fidl::StringPtr queue_token) {
  token_ = queue_token;
}

}  // namespace simple_agent
