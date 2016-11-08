// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A common implementation for single service applications.

#ifndef APPS_MODULAR_APPLICATION_SINGLE_SERVICE_APPLICATION_H_
#define APPS_MODULAR_APPLICATION_SINGLE_SERVICE_APPLICATION_H_

#include <memory>

#include "apps/modular/lib/app/application_context.h"
#include "lib/ftl/macros.h"

namespace modular {

template <class Service, class ServiceImpl>
class SingleServiceApplication {
 public:
  SingleServiceApplication()
      : context_(modular::ApplicationContext::CreateFromStartupInfo()) {
    context_->outgoing_services()->AddService<Service>(
        [this](fidl::InterfaceRequest<Service> request) {
          new ServiceImpl(std::move(request));
        });

  ~SingleServiceApplication() override = default;

 private:
  std::unique_ptr<modular::ApplicationContext> context_;
  FTL_DISALLOW_COPY_AND_ASSIGN(SingleServiceApplication);
};

}  // namespace modular

#endif  // APPS_MODULAR_APPLICATION_SINGLE_SERVICE_APPLICATION_H_
