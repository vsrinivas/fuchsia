// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/input_handler.h"

#include "apps/modular/lib/app/connect.h"
#include "lib/ftl/logging.h"

namespace mozart {

InputHandler::InputHandler(modular::ServiceProvider* service_provider,
                           InputListener* listener)
    : listener_binding_(listener) {
  FTL_DCHECK(service_provider);
  FTL_DCHECK(listener);

  modular::ConnectToService(service_provider, GetProxy(&connection_));

  InputListenerPtr listener_ptr;
  listener_binding_.Bind(GetProxy(&listener_ptr));
  connection_->SetListener(std::move(listener_ptr));
}

InputHandler::~InputHandler() {}

}  // namespace mozart
