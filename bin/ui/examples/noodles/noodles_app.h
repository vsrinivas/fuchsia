// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_UI_NOODLES_NOODLES_APP_H_
#define EXAMPLES_UI_NOODLES_NOODLES_APP_H_

#include "apps/mozart/lib/view_framework/view_provider_app.h"

namespace examples {

class NoodlesApp : public mozart::ViewProviderApp {
 public:
  NoodlesApp();
  ~NoodlesApp() override;

  void CreateView(
      const std::string& connection_url,
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      mojo::InterfaceRequest<mojo::ServiceProvider> services) override;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(NoodlesApp);
};

}  // namespace examples

#endif  // EXAMPLES_UI_NOODLES_NOODLES_APP_H_
