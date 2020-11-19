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
    : story_id_(std::move(story_id)), session_storage_(session_storage), weak_factory_(this) {
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
  fuchsia::element::AnnotationController_UpdateAnnotations_Result result;

  // Ensure all keys, by themselves, are valid.
  bool is_to_set_keys_valid = std::all_of(
      annotations_to_set.begin(), annotations_to_set.end(),
      [](const auto& annotation) { return element::annotations::IsValidKey(annotation.key); });
  bool is_to_delete_keys_valid = std::all_of(
      annotations_to_delete.begin(), annotations_to_delete.end(), element::annotations::IsValidKey);

  if (!is_to_set_keys_valid || !is_to_delete_keys_valid) {
    result.set_err(fuchsia::element::UpdateAnnotationsError::INVALID_ARGS);
    callback(std::move(result));
    return;
  }

  // Ensure that there are no annotations being both set and deleted, i.e. that a key
  // does not exist in both |annotations_to_set| and |annotations_to_delete|.
  std::unordered_set<fuchsia::element::AnnotationKey> to_set_keys;
  for (const auto& annotation : annotations_to_set) {
    to_set_keys.insert(fidl::Clone(annotation.key));
  }
  auto is_same_key_set_delete =
      std::any_of(annotations_to_delete.begin(), annotations_to_delete.end(),
                  [&to_set_keys](const auto& key) { return to_set_keys.count(key) > 0; });

  if (is_same_key_set_delete) {
    result.set_err(fuchsia::element::UpdateAnnotationsError::INVALID_ARGS);
    callback(std::move(result));
    return;
  }

  auto modular_annotations = element::annotations::ToModularAnnotations(annotations_to_set);

  // Add |annotations_to_delete| as Modular annotations with a null value.
  // |MergeStoryAnnotations| removes annotations with a null value from the story.
  for (const auto& key : annotations_to_delete) {
    auto annotation = fuchsia::modular::Annotation{
        .key = element::annotations::ToModularAnnotationKey(key), .value = nullptr};
    modular_annotations.push_back(std::move(annotation));
  }

  auto merge_err =
      session_storage_->MergeStoryAnnotations(story_id_, std::move(modular_annotations));
  if (merge_err.has_value()) {
    if (merge_err.value() == fuchsia::modular::AnnotationError::TOO_MANY_ANNOTATIONS) {
      result.set_err(fuchsia::element::UpdateAnnotationsError::TOO_MANY_ANNOTATIONS);
    } else {
      result.set_err(fuchsia::element::UpdateAnnotationsError::INVALID_ARGS);
    }
  } else {
    result.set_response({});
  }
  callback(std::move(result));
}

void ElementControllerImpl::GetAnnotations(GetAnnotationsCallback callback) {
  auto story_data = session_storage_->GetStoryData(story_id_);

  if (!story_data) {
    fuchsia::element::AnnotationController_GetAnnotations_Result result;
    result.set_response({});
    callback(std::move(result));
    return;
  }

  FX_DCHECK(story_data->has_story_info());

  fuchsia::element::AnnotationController_GetAnnotations_Response response;
  if (story_data->story_info().has_annotations()) {
    response.annotations =
        modular::annotations::ToElementAnnotations(story_data->story_info().annotations());
  }
  fuchsia::element::AnnotationController_GetAnnotations_Result result;
  result.set_response(std::move(response));
  callback(std::move(result));
}

}  // namespace modular
