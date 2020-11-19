// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/element_manager_impl.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/uuid/uuid.h"
#include "src/modular/bin/sessionmgr/annotations.h"
#include "src/modular/bin/sessionmgr/element_controller_impl.h"

namespace modular {
namespace {

// Prefix for all story names created for each proposed element.
constexpr auto kElementStoryNamePrefix = "element-";

// Name of the single module in an element story, which represents the element.
constexpr auto kElementModuleName = "element";

// Returns a unique story name for a newly proposed element.
std::string GenerateStoryName() {
  return kElementStoryNamePrefix + uuid::Uuid::Generate().ToString();
};

}  // namespace

ElementManagerImpl::ElementManagerImpl(SessionStorage* const session_storage)
    : session_storage_(session_storage), weak_factory_(this) {
  FX_DCHECK(session_storage_ != nullptr);

  session_storage_->SubscribeStoryDeleted(
      [weak_ptr = weak_factory_.GetWeakPtr()](std::string story_id) {
        if (!weak_ptr) {
          return modular::WatchInterest::kStop;
        }
        weak_ptr->OnStoryStorageDeleted(std::move(story_id));
        return modular::WatchInterest::kContinue;
      });
}

void ElementManagerImpl::Connect(fidl::InterfaceRequest<fuchsia::element::Manager> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ElementManagerImpl::ProposeElement(
    fuchsia::element::Spec spec,
    fidl::InterfaceRequest<fuchsia::element::Controller> element_controller,
    ProposeElementCallback callback) {
  // Component URL is required.
  if (!spec.has_component_url()) {
    fuchsia::element::Manager_ProposeElement_Result result;
    result.set_err(fuchsia::element::ProposeElementError::NOT_FOUND);
    callback(std::move(result));
    return;
  }

  // When |spec.additional_services| is provided, it must have a valid |host_directory| channel,
  // and must not have a valid |provider|.
  if (spec.has_additional_services() && (!spec.additional_services().host_directory.is_valid() ||
                                         spec.additional_services().provider.is_valid())) {
    fuchsia::element::Manager_ProposeElement_Result result;
    result.set_err(fuchsia::element::ProposeElementError::INVALID_ARGS);
    callback(std::move(result));
    return;
  }

  std::vector<fuchsia::modular::Annotation> annotations;
  if (spec.has_annotations()) {
    annotations = element::annotations::ToModularAnnotations(spec.annotations());
  }

  auto story_id = session_storage_->CreateStory(GenerateStoryName(), std::move(annotations));

  // The story should not exist because it was created with a unique name.
  FX_DCHECK(element_controllers_.find(story_id) == element_controllers_.end());

  // Create an |ElementControllerImpl| for this element, even if the proposer did not request
  // an ElementController. The map entry is used to keep track of element stories.
  auto element_controller_impl =
      std::make_unique<ElementControllerImpl>(story_id, session_storage_);

  if (element_controller.is_valid()) {
    element_controller_impl->Connect(std::move(element_controller));
  }

  element_controllers_[story_id] = std::move(element_controller_impl);

  auto story_storage = session_storage_->GetStoryStorage(story_id);

  // Add the element to the story as a module.
  auto module_data = CreateElementModuleData(std::move(spec));
  story_storage->WriteModuleData(std::move(module_data));

  fuchsia::element::Manager_ProposeElement_Result result;
  result.set_response({});
  callback(std::move(result));
}

void ElementManagerImpl::OnStoryStorageDeleted(std::string story_id) {
  // Do nothing if this is not an element story.
  if (element_controllers_.find(story_id) == element_controllers_.end()) {
    return;
  }

  element_controllers_.erase(story_id);
}

fuchsia::modular::ModuleData ElementManagerImpl::CreateElementModuleData(
    fuchsia::element::Spec spec) {
  fuchsia::modular::ModuleData module_data;

  module_data.set_module_url(spec.component_url());
  module_data.mutable_module_path()->push_back(kElementModuleName);
  module_data.set_module_source(fuchsia::modular::ModuleSource::EXTERNAL);
  module_data.set_module_deleted(false);
  module_data.set_is_embedded(false);
  if (spec.has_additional_services()) {
    module_data.set_additional_services(std::move(*spec.mutable_additional_services()));
  }

  fuchsia::modular::Intent intent;
  intent.handler = spec.component_url();
  module_data.set_intent(std::move(intent));

  return module_data;
}

}  // namespace modular
