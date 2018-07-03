// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/action_log/action_log_impl.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/time/time_delta.h>

#include "peridot/bin/action_log/action_log_data.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/pointer.h"
#include "third_party/rapidjson/rapidjson/stringbuffer.h"
#include "third_party/rapidjson/rapidjson/writer.h"

namespace modular {

UserActionLogImpl::UserActionLogImpl(
    fuchsia::modular::ProposalPublisherPtr proposal_publisher)
    : action_log_([this](const ActionData& action_data) {
        BroadcastToSubscribers(action_data);
        MaybeProposeSharingVideo(action_data);
        MaybeRecordEmailRecipient(action_data);
      }),
      proposal_publisher_(std::move(proposal_publisher)) {}

UserActionLogImpl::~UserActionLogImpl() = default;

void UserActionLogImpl::BroadcastToSubscribers(const ActionData& action_data) {
  fuchsia::modular::UserAction action;
  action.component_url = action_data.component_url;
  action.method = action_data.method;
  action.parameters = action_data.params;
  for (auto& listener : subscribers_.ptrs()) {
    fuchsia::modular::UserAction copy;
    fidl::Clone(action, &copy);
    (*listener)->OnAction(std::move(copy));
  };
}

void UserActionLogImpl::MaybeProposeSharingVideo(
    const ActionData& action_data) {
  /* TODO: Either remove this code altogether or refactor it to use a vmo for
    the suggestion image. if (action_data.story_id.empty() ||
    action_data.module_path.empty()) { return;
    }
    if (action_data.method.compare("ViewVideo") != 0) {
      return;
    }

    rapidjson::Document doc_params;
    if (doc_params.Parse(action_data.params).HasParseError()) {
      return;
    }
    rapidjson::Value* vid_value =
        rapidjson::Pointer("/youtube-doc/youtube-video-id").Get(doc_params);
    rapidjson::Value* title_value =
        rapidjson::Pointer("/youtube-doc/youtube-video-title").Get(doc_params);

    if (vid_value == nullptr || !vid_value->IsString()) {
      return;
    }
    std::string video_id = vid_value->GetString();
    std::string proposal_id = "Share Video " + action_data.story_id;

    bool has_title = title_value != nullptr && title_value->IsString();
    std::string video_title;
    if (has_title) {
      video_title = title_value->GetString();
    }

    fuchsia::modular::ProposalPtr proposal(fuchsia::modular::Proposal::New());
    proposal->id = proposal_id;

    auto add_module = AddModuleToStory::New();
    add_module->story_id = action_data.story_id;
    add_module->module_url = "email_composer";
    add_module->module_name = video_id;

    for (auto segment = action_data.module_path.begin();
         segment != action_data.module_path.end(); segment++) {
      add_module->module_path.push_back(*segment);
    }

    // HACK(alhaad): There is an issue that the root youtube module (A) actually
    // embeds 2 other modules (B and C) and it's an embeded module (B) that
    // produces this action log.
    // Story shell freaks out when we try to add a new module (D) parented at
    (B)
    // because it does not know about it (B). So instead we use some domain
    // specific information to parent new module (D) at (A) instead.
    size_t module_path_size = add_module->module_path.size();
    if (module_path_size > 0) {
      add_module->module_path.resize(module_path_size - 1);
    }

    add_module->link_name = "email-composer-link";
    // TODO(azani): Do something sane.
    std::string initial_data = "{\"email-composer\": {\"message\": {";
    if (!last_email_rcpt_.empty()) {
      initial_data += "\"to\": [{\"email\": \"" + last_email_rcpt_ + "\"}],";
    }
    if (has_title) {
      // TODO(youngseokyoon): The video_title should be correctly escaped here.
      initial_data +=
          "\"subject\": \"Watch \\\"" + video_title + "\\\" on YouTube\",";
    } else {
      initial_data += "\"subject\": \"Watch this video on YouTube\",";
    }
    initial_data += "\"text\": \"http://www.youtube.com/watch?v=";
    initial_data += video_id + "\"}}}";
    add_module->initial_data = initial_data;

    // We start compose module in a copresent, dependent configuration that
    takes
    // 30% emphasis.
    add_module->surface_relation = fuchsia::modular::SurfaceRelation::New();
    add_module->surface_relation->arrangement =
        fuchsia::modular::SurfaceArrangement::COPRESENT;
    add_module->surface_relation->dependency =
        fuchsia::modular::SurfaceDependency::DEPENDENT;
    add_module->surface_relation->emphasis = 0.5;

    fuchsia::modular::ActionPtr action(fuchsia::modular::Action::New());
    action->set_add_module_to_story(std::move(add_module));
    proposal->on_selected.push_back(std::move(action));

    action->set_add_module_to_story(std::move(add_module));
    proposal->on_selected.push_back(std::move(action));

    fuchsia::modular::SuggestionDisplayImagePtr
    displayImage(fuchsia::modular::SuggestionDisplayImage::New());
    displayImage->url = "http://img.youtube.com/vi/" + video_id + "/0.jpg";
    displayImage->image_type = fuchsia::modular::SuggestionImageType::OTHER;

    fuchsia::modular::SuggestionDisplayPtr
    display(fuchsia::modular::SuggestionDisplay::New()); if (has_title) {
      display->headline = "Share \"" + video_title + "\" Video via email";
    } else {
      display->headline = "Share Video via email";
    }
    display->color = 0xff42ebf4;
    display->image = std::move(displayImage);
    // If there is an email recipient already available, set an interrupt
    // suggestion.
    if (!last_email_rcpt_.empty()) {
      display->annoyance = fuchsia::modular::AnnoyanceType::INTERRUPT;
      if (has_title) {
        display->headline =
            "Share \"" + video_title + "\" Video with " + last_email_rcpt_;
      } else {
        display->headline = "Share Video with " + last_email_rcpt_;
      }
    }
    proposal->display = std::move(display);

    // We clear any existing proposal for this story.
    proposal_publisher_->Remove(proposal_id);
    proposal_publisher_->Propose(std::move(proposal));
  */
}

void UserActionLogImpl::MaybeRecordEmailRecipient(
    const ActionData& action_data) {
  if (action_data.method.compare("SendEmail") != 0) {
    return;
  }
  rapidjson::Document doc_params;
  if (doc_params.Parse(action_data.params).HasParseError()) {
    return;
  }
  rapidjson::Value* rcpt_value =
      rapidjson::Pointer("/to/0/email").Get(doc_params);

  if (rcpt_value == nullptr || !rcpt_value->IsString()) {
    return;
  }

  last_email_rcpt_ = rcpt_value->GetString();
}

void UserActionLogImpl::GetComponentActionLog(
    fuchsia::modular::ComponentScope scope,
    fidl::InterfaceRequest<fuchsia::modular::ComponentActionLog>
        action_log_request) {
  std::unique_ptr<ComponentActionLogImpl> module_action_log_impl(
      new ComponentActionLogImpl(
          action_log_.GetActionLogger(std::move(scope))));

  action_log_bindings_.AddBinding(std::move(module_action_log_impl),
                                  std::move(action_log_request));
}

void UserActionLogImpl::Duplicate(
    fidl::InterfaceRequest<fuchsia::modular::UserActionLog> request) {
  bindings_.AddBinding(this, std::move(request));
}

void UserActionLogImpl::Subscribe(
    fidl::InterfaceHandle<fuchsia::modular::ActionLogListener>
        listener_handle) {
  fuchsia::modular::ActionLogListenerPtr listener = listener_handle.Bind();
  subscribers_.AddInterfacePtr(std::move(listener));
}

ComponentActionLogImpl::ComponentActionLogImpl(ActionLogger log_action)
    : log_action_(log_action) {}

ComponentActionLogImpl::~ComponentActionLogImpl() = default;

void ComponentActionLogImpl::LogAction(fidl::StringPtr method,
                                       fidl::StringPtr json_params) {
  rapidjson::Document params;
  if (params.Parse(json_params.get().c_str()).HasParseError()) {
    FXL_LOG(WARNING) << "Parse error.";
    return;
  }

  log_action_(method, json_params);
}

}  // namespace modular
