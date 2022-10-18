// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story_runner/annotation_controller_impl.h"

#include <lib/syslog/cpp/macros.h>

#include <unordered_set>

#include "src/modular/bin/sessionmgr/annotations.h"

namespace modular {

AnnotationControllerImpl::AnnotationControllerImpl(std::string story_id,
                                                   SessionStorage* const session_storage)
    : story_id_(std::move(story_id)), session_storage_(session_storage), weak_factory_(this) {
  FX_DCHECK(session_storage_ != nullptr);
}

void AnnotationControllerImpl::Connect(
    fidl::InterfaceRequest<fuchsia::element::AnnotationController> request) {
  FX_DCHECK(request);
  FX_CHECK(!binding_.is_bound());
  binding_.Bind(std::move(request));
}

void AnnotationControllerImpl::UpdateAnnotations(
    std::vector<fuchsia::element::Annotation> annotations_to_set,
    std::vector<fuchsia::element::AnnotationKey> annotations_to_delete,
    UpdateAnnotationsCallback callback) {
  fuchsia::element::AnnotationController_UpdateAnnotations_Result result;

  // Ensure all keys, by themselves, are valid.
  bool keys_to_set_are_valid{true};
  for (const auto& annotation : annotations_to_set) {
    if (!element::annotations::IsValidKey(annotation.key)) {
      FX_LOGS(ERROR) << "Setting invalid key for story id: " << story_id_
                     << " annotation key namespace: " << annotation.key.namespace_
                     << " annotation key value: " << annotation.key.value;
      keys_to_set_are_valid = false;
    }
  }
  if (!keys_to_set_are_valid) {
    result.set_err(fuchsia::element::UpdateAnnotationsError::INVALID_ARGS);
    callback(std::move(result));
    return;
  }

  bool keys_to_delete_are_valid{true};
  for (const auto& annotation_key : annotations_to_delete) {
    if (!element::annotations::IsValidKey(annotation_key)) {
      FX_LOGS(ERROR) << "Deleting invalid key for story id: " << story_id_
                     << " annotation key namespace: " << annotation_key.namespace_
                     << " annotation key value: " << annotation_key.value;
      keys_to_delete_are_valid = false;
    }
  }
  if (!keys_to_delete_are_valid) {
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
  bool is_same_key_set_delete{false};
  for (const auto& annotation_key : annotations_to_delete) {
    if (to_set_keys.count(annotation_key) > 0) {
      FX_LOGS(ERROR) << "Setting and deleting the same annotation key for story id: " << story_id_
                     << " annotation key namespace: " << annotation_key.namespace_
                     << " annotation key value: " << annotation_key.value;
      is_same_key_set_delete = true;
    }
  }

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

void AnnotationControllerImpl::GetAnnotations(GetAnnotationsCallback callback) {
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

void AnnotationControllerImpl::WatchAnnotations(WatchAnnotationsCallback callback) {
  if (watch_callback_) {
    binding_.Close(ZX_ERR_BAD_STATE);
    return;
  }

  if (!watching_annotations_) {
    watching_annotations_ = true;
    session_storage_->SubscribeAnnotationsUpdated(
        [weak_this = weak_factory_.GetWeakPtr()](
            std::string updated_story_id,
            const std::vector<fuchsia::modular::Annotation>& annotations,
            const std::set<std::string>& /*annotation_keys_added*/,
            const std::set<std::string>& /*annotation_keys_deleted*/) {
          if (!weak_this)
            return WatchInterest::kStop;
          if (updated_story_id != weak_this->story_id_)
            return WatchInterest::kContinue;

          if (weak_this->watch_callback_) {
            // Notify the client immediately if a callback is pending.
            fuchsia::element::AnnotationController_WatchAnnotations_Response response;
            response.annotations = modular::annotations::ToElementAnnotations(annotations);
            fuchsia::element::AnnotationController_WatchAnnotations_Result result;
            result.set_response(std::move(response));
            std::exchange(weak_this->watch_callback_,
                          WatchAnnotationsCallback())(std::move(result));
          } else {
            weak_this->have_pending_update_ = true;
          }

          return WatchInterest::kContinue;
        });
  }

  if (!have_pending_update_) {
    watch_callback_ = std::move(callback);
    return;
  }

  have_pending_update_ = false;
  GetAnnotations(std::move(reinterpret_cast<GetAnnotationsCallback&>(callback)));
}

}  // namespace modular
