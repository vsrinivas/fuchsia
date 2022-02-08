// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATION_PROVIDERS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATION_PROVIDERS_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>

#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/feedback/annotations/data_register.h"
#include "src/developer/forensics/feedback/annotations/time_provider.h"

namespace forensics::feedback {

// Wraps the annotations providers Feedback uses and the component's AnnotationManager.
class AnnotationProviders {
 public:
  AnnotationProviders(async_dispatcher_t* dispatcher, std::set<std::string> allowlist,
                      Annotations static_annotations);

  AnnotationManager* GetAnnotationManager() { return &annotation_manager_; }

  void Handle(::fidl::InterfaceRequest<fuchsia::feedback::ComponentDataRegister> request,
              ::fit::function<void(zx_status_t)> error_handler);

 private:
  async_dispatcher_t* dispatcher_;

  DataRegister data_register_;
  TimeProvider time_provider_;

  AnnotationManager annotation_manager_;

  ::fidl::BindingSet<fuchsia::feedback::ComponentDataRegister> data_register_connections_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATION_PROVIDERS_H_
