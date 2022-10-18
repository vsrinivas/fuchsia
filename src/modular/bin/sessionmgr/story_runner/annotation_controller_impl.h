// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_ANNOTATION_CONTROLLER_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_ANNOTATION_CONTROLLER_IMPL_H_

#include <fuchsia/element/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/modular/bin/sessionmgr/storage/session_storage.h"

namespace modular {

class AnnotationControllerImpl : public fuchsia::element::AnnotationController {
 public:
  explicit AnnotationControllerImpl(std::string story_id, SessionStorage* session_storage);
  ~AnnotationControllerImpl() override = default;

  void Connect(fidl::InterfaceRequest<fuchsia::element::AnnotationController> request);

  // |AnnotationController|
  void UpdateAnnotations(std::vector<fuchsia::element::Annotation> annotations_to_set,
                         std::vector<fuchsia::element::AnnotationKey> annotations_to_delete,
                         UpdateAnnotationsCallback callback) override;

  // |AnnotationController|
  void GetAnnotations(GetAnnotationsCallback callback) override;

  // |AnnotationController|
  void WatchAnnotations(WatchAnnotationsCallback callback) override;

 private:
  // The ID of the story containing the element associated with this annotation controller.
  const std::string story_id_;

  SessionStorage* const session_storage_;  // Not owned.

  fidl::Binding<fuchsia::element::AnnotationController> binding_{this};

  // True if WatchAnnotations() has been called by the client, indicating that
  // the controller is subscribed to annotation updates.
  bool watching_annotations_{false};

  // True if the next WatchAnnotations() call should return immediately, with
  // the current annotations state at the time.
  bool have_pending_update_{true};

  // Holds the WatchAnnotations() completion callback, to use to notify the
  // client when an annotations update is next available.
  WatchAnnotationsCallback watch_callback_;

  fxl::WeakPtrFactory<AnnotationControllerImpl> weak_factory_;
  FXL_DISALLOW_COPY_AND_ASSIGN(AnnotationControllerImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_ANNOTATION_CONTROLLER_IMPL_H_
