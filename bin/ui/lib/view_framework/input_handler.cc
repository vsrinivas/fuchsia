// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/input_handler.h"

#include "application/lib/app/connect.h"
#include "lib/ftl/logging.h"

namespace mozart {

InputHandler::InputHandler(app::ServiceProvider* service_provider,
                           InputListener* listener,
                           ViewHitTester* tester)
    : event_listener_binding_(listener), view_hit_tester_binding_(tester) {
  FTL_DCHECK(service_provider);
  FTL_DCHECK(listener);

  app::ConnectToService(service_provider, connection_.NewRequest());

  InputListenerPtr listener_ptr;
  event_listener_binding_.Bind(listener_ptr.NewRequest());
  connection_->SetEventListener(std::move(listener_ptr));

  if (tester) {
    ViewHitTesterPtr tester_ptr;
    view_hit_tester_binding_.Bind(tester_ptr.NewRequest());
    connection_->SetViewHitTester(std::move(tester_ptr));
  }
}

InputHandler::InputHandler(app::ServiceProvider* service_provider,
                           InputListener* listener)
    : InputHandler(service_provider, listener, nullptr) {}

InputHandler::~InputHandler() {}

}  // namespace mozart
