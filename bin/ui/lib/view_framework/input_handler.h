// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_VIEW_FRAMEWORK_INPUT_HANDLER_H_
#define APPS_MOZART_LIB_VIEW_FRAMEWORK_INPUT_HANDLER_H_

#include "apps/modular/services/application/service_provider.fidl.h"
#include "apps/mozart/services/input/input_connection.fidl.h"
#include "lib/ftl/macros.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace mozart {

// Holds an |InputConnection| and sets its |InputListener|.
//
// This class is intended to be included as a member of a View to help
// decode input events.
class InputHandler {
 public:
  // Creates an input connection for the view with the associated
  // service provider.
  InputHandler(modular::ServiceProvider* service_provider,
               InputListener* listener);
  ~InputHandler();

  // Gets the input connection.
  InputConnection* connection() { return connection_.get(); }

 private:
  fidl::Binding<InputListener> listener_binding_;
  InputConnectionPtr connection_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InputHandler);
};

}  // namespace mozart

#endif  // APPS_MOZART_LIB_VIEW_FRAMEWORK_INPUT_HANDLER_H_
