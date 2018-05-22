// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_EXAMPLES_SIMPLE_AGENT_SIMPLE_IMPL_H_
#define PERIDOT_EXAMPLES_SIMPLE_AGENT_SIMPLE_IMPL_H_

#include <string>

#include <simple_agent/cpp/fidl.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace simple_agent {

// An implementation of the Simple interface exposed by |SimpleAgent|.
class SimpleImpl : Simple {
 public:
  SimpleImpl();
  ~SimpleImpl();

  void Connect(fidl::InterfaceRequest<Simple> request);

  std::string message_queue_token() const { return token_; }

 private:
  // |Simple| interface method.
  void SetMessageQueue(fidl::StringPtr queue_token);

  // The bindings to the Simple service.
  fidl::BindingSet<Simple> bindings_;

  // The current message queue token.
  std::string token_;
};

}  // namespace simple_agent

#endif // PERIDOT_EXAMPLES_SIMPLE_AGENT_SIMPLE_IMPL_H_
