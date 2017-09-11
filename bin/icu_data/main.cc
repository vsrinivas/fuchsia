// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "lib/app/cpp/application_context.h"
#include "garnet/bin/icu_data/icu_data_provider_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace icu_data {

class App {
 public:
  App() : context_(app::ApplicationContext::CreateFromStartupInfo()) {
    if (!icu_data_.LoadData())
      exit(MX_ERR_UNAVAILABLE);
    context_->outgoing_services()->AddService<ICUDataProvider>(
        [this](fidl::InterfaceRequest<ICUDataProvider> request) {
          icu_data_.AddBinding(std::move(request));
        });
  }

 private:
  std::unique_ptr<app::ApplicationContext> context_;
  ICUDataProviderImpl icu_data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace icu_data

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  icu_data::App app;
  loop.Run();
  return 0;
}
