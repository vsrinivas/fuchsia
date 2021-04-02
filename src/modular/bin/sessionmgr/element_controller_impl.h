// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_ELEMENT_CONTROLLER_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_ELEMENT_CONTROLLER_IMPL_H_

#include <fuchsia/element/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/modular/bin/sessionmgr/storage/session_storage.h"

namespace modular {

class ElementControllerImpl : public fuchsia::element::Controller {
 public:
  explicit ElementControllerImpl(std::string story_id, SessionStorage* session_storage);
  ~ElementControllerImpl() override = default;

  void Connect(fidl::InterfaceRequest<fuchsia::element::Controller> request);

  // |Controller|
  void UpdateAnnotations(std::vector<fuchsia::element::Annotation> annotations_to_set,
                         std::vector<fuchsia::element::AnnotationKey> annotations_to_delete,
                         UpdateAnnotationsCallback callback) override;

  // |Controller|
  void GetAnnotations(GetAnnotationsCallback callback) override;

 private:
  // The ID of the story containing the element associated with this controller.
  std::string story_id_;

  SessionStorage* const session_storage_;  // Not owned.

  fidl::BindingSet<fuchsia::element::Controller> bindings_;

  fxl::WeakPtrFactory<ElementControllerImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ElementControllerImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_ELEMENT_CONTROLLER_IMPL_H_
