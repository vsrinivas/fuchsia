// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/element_controller_impl.h"

#include <lib/syslog/cpp/macros.h>

#include <unordered_set>
#include <utility>

#include "src/modular/bin/sessionmgr/annotations.h"

namespace modular {

ElementControllerImpl::ElementControllerImpl(std::string story_id,
                                             SessionStorage* const session_storage)
    : story_id_(std::move(story_id)),
      session_storage_(session_storage),
      annotation_controller_(
          std::make_unique<AnnotationControllerImpl>(story_id_, session_storage_)),
      weak_factory_(this) {
  FX_DCHECK(session_storage_ != nullptr);
}

void ElementControllerImpl::Connect(fidl::InterfaceRequest<fuchsia::element::Controller> request) {
  auto on_error = [weak_ptr = weak_factory_.GetWeakPtr()](zx_status_t status) {
    if (!weak_ptr) {
      return;
    }

    FX_PLOGS(INFO, status) << "Element proposer closed ElementController; deleting story: "
                           << weak_ptr->story_id_;

    weak_ptr->session_storage_->DeleteStory(weak_ptr->story_id_);
  };

  bindings_.AddBinding(this, std::move(request), /*dispatcher=*/nullptr, std::move(on_error));
}

void ElementControllerImpl::UpdateAnnotations(
    std::vector<fuchsia::element::Annotation> annotations_to_set,
    std::vector<fuchsia::element::AnnotationKey> annotations_to_delete,
    UpdateAnnotationsCallback callback) {
  annotation_controller_->UpdateAnnotations(std::move(annotations_to_set),
                                            std::move(annotations_to_delete), std::move(callback));
}

void ElementControllerImpl::GetAnnotations(GetAnnotationsCallback callback) {
  annotation_controller_->GetAnnotations(std::move(callback));
}

}  // namespace modular
