// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_LIB_VIEW_FRAMEWORK_VIEW_PROVIDER_APP_H_
#define APPS_MOZART_LIB_VIEW_FRAMEWORK_VIEW_PROVIDER_APP_H_

#include <memory>

#include "apps/modular/lib/app/application_context.h"
#include "apps/mozart/lib/view_framework/view_provider_service.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/ftl/tasks/task_runner.h"

namespace mozart {

// Provides a skeleton for an entire application that only offers
// a view provider service.
// This is only intended to be used for simple example programs.
class ViewProviderApp {
 public:
  explicit ViewProviderApp(ViewFactory factory,
                           const ftl::RefPtr<ftl::TaskRunner>& task_runner);
  ~ViewProviderApp();

 private:
  void Start();

  ViewFactory factory_;
  std::unique_ptr<modular::ApplicationContext> application_context_;
  std::unique_ptr<ViewProviderService> service_;

  ftl::WeakPtrFactory<ViewProviderApp> weak_ptr_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewProviderApp);
};

}  // namespace mozart

#endif  // APPS_MOZART_LIB_VIEW_FRAMEWORK_VIEW_PROVIDER_APP_H_
