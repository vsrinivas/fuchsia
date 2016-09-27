// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_UI_INPUT_HANDLER_H_
#define MOJO_UI_INPUT_HANDLER_H_

#include "apps/mozart/services/input/interfaces/input_connection.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/interfaces/application/service_provider.mojom.h"

namespace mojo {
namespace ui {

// Holds an |InputConnection| and sets its |InputListener|.
//
// This class is intended to be included as a member of a View that wants to
// receive input using the following pattern.
//
// class MyView : public mojo::ui::BaseView, public mojo::ui::InputListener {
//  public:
//   MyView(mojo::ApplicationImpl* app_impl,
//          const mojo::ui::ViewProvider::CreateViewCallback&
//              create_view_callback)
//          : BaseView(app_impl, "MyView", create_view_callback),
//            input_handler_(GetViewServiceProvider(), this) {}
//   ~MyView() override {}
//
//  private:
//   // |InputListener|:
//   void OnEvent(mojo::EventPtr event,
//                const OnEventCallback& callback) override;
//
//   mojo::ui::InputHandler input_handler_;
//
//   FTL_DISALLOW_COPY_AND_ASSIGN(MyView);
// };
class InputHandler {
 public:
  // Creates an input connection for the view with the associated
  // service provider.
  InputHandler(mojo::ServiceProvider* service_provider,
               mojo::ui::InputListener* listener);
  ~InputHandler();

  // Gets the input connection.
  mojo::ui::InputConnection* connection() { return connection_.get(); }

 private:
  mojo::Binding<mojo::ui::InputListener> listener_binding_;
  mojo::ui::InputConnectionPtr connection_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InputHandler);
};

}  // namespace ui
}  // namespace mojo

#endif  // MOJO_UI_INPUT_HANDLER_H_
