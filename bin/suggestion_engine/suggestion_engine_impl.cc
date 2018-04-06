// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

#include <fuchsia/cpp/modular.h>
#include "lib/app/cpp/application_context.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/context/cpp/context_helper.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_rate.h"

#include "peridot/bin/suggestion_engine/ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/kronk_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/mod_pair_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/proposal_hint_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/query_match_ranking_feature.h"
#include "peridot/lib/fidl/json_xdr.h"

namespace modular {

SuggestionEngineImpl::SuggestionEngineImpl(
    component::ApplicationContext* app_context)
    : debug_(std::make_shared<SuggestionDebugImpl>()),
      next_processor_(debug_),
      context_listener_binding_(this) {
  app_context->outgoing_services()->AddService<SuggestionEngine>(
      [this](fidl::InterfaceRequest<SuggestionEngine> request) {
        bindings_.AddBinding(this, std::move(request));
      });
  app_context->outgoing_services()->AddService<SuggestionProvider>(
      [this](fidl::InterfaceRequest<SuggestionProvider> request) {
        suggestion_provider_bindings_.AddBinding(this, std::move(request));
      });
  app_context->outgoing_services()->AddService<SuggestionDebug>(
      [this](fidl::InterfaceRequest<SuggestionDebug> request) {
        debug_bindings_.AddBinding(debug_.get(), std::move(request));
      });

  audio_server_ =
      app_context->ConnectToEnvironmentService<media::AudioServer>();
  audio_server_.set_error_handler([this] {
    FXL_LOG(INFO) << "Audio server connection error";
    audio_server_ = nullptr;
    media_packet_producer_ = nullptr;
  });
}

SuggestionEngineImpl::~SuggestionEngineImpl() = default;

fxl::WeakPtr<SuggestionDebugImpl> SuggestionEngineImpl::debug() {
  return debug_->GetWeakPtr();
}

void SuggestionEngineImpl::AddNextProposal(ProposalPublisherImpl* source,
                                           Proposal proposal) {
  next_processor_.AddProposal(source->component_url(), std::move(proposal));
}

void SuggestionEngineImpl::RemoveNextProposal(const std::string& component_url,
                                              const std::string& proposal_id) {
  next_processor_.RemoveProposal(component_url, proposal_id);
}

// |SuggestionProvider|
void SuggestionEngineImpl::Query(fidl::InterfaceHandle<QueryListener> listener,
                                 UserInput input,
                                 int count) {
  // TODO(jwnichols): I'm not sure this is correct or should be here
  for (auto& listener : speech_listeners_.ptrs()) {
    (*listener)->OnStatusChanged(SpeechStatus::PROCESSING);
  }

  // Process:
  //   1. Close out and clean up any existing query process
  //   2. Update the context engine with the new query
  //   3. Set up the ask variables in suggestion engine
  //   4. Get suggestions from each of the QueryHandlers
  //   5. Rank the suggestions as received
  //   6. Send "done" to SuggestionListener

  // Step 1
  CleanUpPreviousQuery();

  // Step 2
  std::string query = input.text;
  if (!query.empty()) {
    // Update context engine
    std::string formattedQuery;
    modular::XdrWrite(&formattedQuery, &query, modular::XdrFilter<std::string>);
    context_writer_->WriteEntityTopic(kQueryContextKey, formattedQuery);

    // Update suggestion engine debug interface
    debug_->OnAskStart(query, &query_suggestions_);
  }

  // Steps 3 - 6
  active_query_ = std::make_unique<QueryProcessor>(this, std::move(listener),
                                                   std::move(input), count);
}

void SuggestionEngineImpl::UpdateRanking() {
  next_processor_.UpdateRanking();
}

// |SuggestionProvider|
void SuggestionEngineImpl::SubscribeToInterruptions(
    fidl::InterfaceHandle<InterruptionListener> listener) {
  next_processor_.RegisterInterruptionListener(std::move(listener));
}

// |SuggestionProvider|
void SuggestionEngineImpl::SubscribeToNext(
    fidl::InterfaceHandle<NextListener> listener,
    int count) {
  next_processor_.RegisterListener(std::move(listener), count);
}

// |SuggestionProvider|
void SuggestionEngineImpl::RegisterFeedbackListener(
    fidl::InterfaceHandle<FeedbackListener> speech_listener) {
  speech_listeners_.AddInterfacePtr(speech_listener.Bind());
}

// |SuggestionProvider|
void SuggestionEngineImpl::NotifyInteraction(fidl::StringPtr suggestion_uuid,
                                             Interaction interaction) {
  // Find the suggestion
  bool suggestion_in_ask = false;
  RankedSuggestion* suggestion = next_processor_.GetSuggestion(suggestion_uuid);
  if (!suggestion) {
    suggestion = query_suggestions_.GetSuggestion(suggestion_uuid);
    suggestion_in_ask = true;
  }

  // If it exists (and it should), perform the action and clean up
  if (suggestion) {
    std::string log_detail = suggestion->prototype
                                 ? short_proposal_str(*suggestion->prototype)
                                 : "invalid";

    FXL_LOG(INFO) << (interaction.type == InteractionType::SELECTED
                          ? "Accepted"
                          : "Dismissed")
                  << " suggestion " << suggestion_uuid << " (" << log_detail
                  << ")";

    debug_->OnSuggestionSelected(suggestion->prototype);

    auto& proposal = suggestion->prototype->proposal;
    if (interaction.type == InteractionType::SELECTED) {
      PerformActions(std::move(proposal.on_selected),
                     suggestion->prototype->source_url, proposal.display.color);
    }

    if (suggestion_in_ask) {
      CleanUpPreviousQuery();
      UpdateRanking();
    } else {
      RemoveNextProposal(suggestion->prototype->source_url, proposal.id);
    }
  } else {
    FXL_LOG(WARNING) << "Requested suggestion prototype not found. UUID: "
                     << suggestion_uuid;
  }
}

// |SuggestionEngine|
void SuggestionEngineImpl::RegisterProposalPublisher(
    fidl::StringPtr url,
    fidl::InterfaceRequest<ProposalPublisher> publisher) {
  // Check to see if a ProposalPublisher has already been created for the
  // component with this url. If not, create one.
  std::unique_ptr<ProposalPublisherImpl>& source = proposal_publishers_[url];
  if (!source) {  // create if it didn't already exist
    source = std::make_unique<ProposalPublisherImpl>(this, url);
  }

  source->AddBinding(std::move(publisher));
}

// |SuggestionEngine|
void SuggestionEngineImpl::RegisterQueryHandler(
    fidl::StringPtr url,
    fidl::InterfaceHandle<QueryHandler> query_handler_handle) {
  auto query_handler = query_handler_handle.Bind();
  query_handlers_.emplace_back(std::move(query_handler), url);
}

// |SuggestionEngine|
void SuggestionEngineImpl::Initialize(
    fidl::InterfaceHandle<modular::StoryProvider> story_provider,
    fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
    fidl::InterfaceHandle<ContextWriter> context_writer,
    fidl::InterfaceHandle<ContextReader> context_reader) {
  story_provider_.Bind(std::move(story_provider));
  focus_provider_ptr_.Bind(std::move(focus_provider));
  context_writer_.Bind(std::move(context_writer));
  context_reader_.Bind(std::move(context_reader));
  RegisterRankingFeatures();
  timeline_stories_watcher_.reset(new TimelineStoriesWatcher(&story_provider_));
}

// end SuggestionEngine

void SuggestionEngineImpl::RegisterRankingFeatures() {
  // Create common ranking features
  ranking_features["proposal_hint_rf"] =
      std::make_shared<ProposalHintRankingFeature>();
  ranking_features["kronk_rf"] = std::make_shared<KronkRankingFeature>();
  ranking_features["mod_pairs_rf"] = std::make_shared<ModPairRankingFeature>();
  ranking_features["query_match_rf"] =
      std::make_shared<QueryMatchRankingFeature>();

  // Get context updates every time a story is focused to rerank suggestions
  // based on the story that is focused at the moment.
  ContextQuery query;
  for (auto const& it : ranking_features) {
    ContextSelectorPtr selector = it.second->CreateContextSelector();
    if (selector) {
      AddToContextQuery(&query, it.first, std::move(*selector));
    }
  }
  context_reader_->Subscribe(std::move(query),
                             context_listener_binding_.NewBinding());

  // TODO(jwnichols): Replace the code configuration of the ranking features
  // with a configuration file

  // Set up the next ranking features
  next_processor_.AddRankingFeature(1.0, ranking_features["proposal_hint_rf"]);
  next_processor_.AddRankingFeature(-0.1, ranking_features["kronk_rf"]);
  next_processor_.AddRankingFeature(0, ranking_features["mod_pairs_rf"]);

  // Set up the query ranking features
  query_suggestions_.AddRankingFeature(1.0,
                                       ranking_features["proposal_hint_rf"]);
  query_suggestions_.AddRankingFeature(-0.1, ranking_features["kronk_rf"]);
  query_suggestions_.AddRankingFeature(0, ranking_features["mod_pairs_rf"]);
  query_suggestions_.AddRankingFeature(0, ranking_features["query_match_rf"]);
}

void SuggestionEngineImpl::CleanUpPreviousQuery() {
  active_query_.reset();
  query_prototypes_.clear();
  query_suggestions_.RemoveAllSuggestions();
}

void SuggestionEngineImpl::PerformActions(fidl::VectorPtr<Action> actions,
                                          const std::string& source_url,
                                          uint32_t story_color) {
  // TODO(rosswang): If we're asked to add multiple modules, we probably
  // want to add them to the same story. We can't do that yet, but we need
  // to receive a StoryController anyway (not optional atm.).
  for (auto& action : *actions) {
    switch (action.Which()) {
      case Action::Tag::kCreateStory: {
        PerformCreateStoryAction(action, story_color);
        break;
      }
      case Action::Tag::kFocusStory: {
        PerformFocusStoryAction(action);
        break;
      }
      case Action::Tag::kAddModuleToStory: {
        PerformAddModuleToStoryAction(action);
        break;
      }
      case Action::Tag::kAddModule: {
        PerformAddModuleAction(action);
        break;
      }
      case Action::Tag::kCustomAction: {
        PerformCustomAction(&action, source_url, story_color);
        break;
      }
      default:
        FXL_LOG(WARNING) << "Unknown action tag " << (uint32_t)action.Which();
    }
  }
}

void SuggestionEngineImpl::PerformCreateStoryAction(const Action& action,
                                                    uint32_t story_color) {
  auto activity = debug_->RegisterOngoingActivity();
  const auto& create_story = action.create_story();

  if (!story_provider_) {
    FXL_LOG(WARNING) << "Unable to add module; no story provider";
    return;
  }

  if (create_story.daisy) {
    // If a daisy was provided, create an empty story and add a module to it
    // with the provided daisy.
    story_provider_->CreateStory(
        nullptr,
        fxl::MakeCopyable([this, daisy = std::move(*create_story.daisy),
                           activity](const fidl::StringPtr& story_id) mutable {
          modular::StoryControllerPtr story_controller;
          story_provider_->GetController(story_id,
                                         story_controller.NewRequest());
          story_controller->AddModule(nullptr, "", std::move(daisy), nullptr);
          focus_provider_ptr_->Request(story_id);
        }));
    return;
  }

  // TODO(afergan): Make this more robust later. For now, we
  // always assume that there's extra info and that it's a color.
  auto extra_info = fidl::VectorPtr<modular::StoryInfoExtraEntry>::New(1);
  char hex_color[11];
  snprintf(hex_color, sizeof(hex_color), "0x%x", story_color);
  extra_info->at(0).key = "color";
  extra_info->at(0).value = hex_color;
  auto& initial_data = create_story.initial_data;
  auto& module_id = create_story.module_id;
  FXL_LOG(INFO) << "Creating story with module " << module_id;
  story_provider_->CreateStoryWithInfo(
      create_story.module_id, std::move(extra_info), std::move(initial_data),
      [this, activity](fidl::StringPtr story_id) {
        story_provider_->GetStoryInfo(
            story_id, [this, activity](modular::StoryInfoPtr story_info) {
              FXL_DCHECK(story_info);
              FXL_LOG(INFO)
                  << "Requesting focus for story_id " << story_info->id;
              focus_provider_ptr_->Request(story_info->id);
            });
      });
}

void SuggestionEngineImpl::PerformFocusStoryAction(const Action& action) {
  const auto& focus_story = action.focus_story();
  FXL_LOG(INFO) << "Requesting focus for story_id " << focus_story.story_id;
  focus_provider_ptr_->Request(focus_story.story_id);
}

void SuggestionEngineImpl::PerformAddModuleToStoryAction(const Action& action) {
  if (story_provider_) {
    const auto& add_module_to_story = action.add_module_to_story();
    const auto& story_id = add_module_to_story.story_id;
    const auto& module_name = add_module_to_story.module_name;
    const auto& module_url = add_module_to_story.module_url;
    const auto& link_name = add_module_to_story.link_name;
    const auto& module_path = add_module_to_story.module_path;
    const auto& surface_relation = add_module_to_story.surface_relation;

    FXL_LOG(INFO) << "Adding module " << module_url << " to story " << story_id;

    modular::StoryControllerPtr story_controller;
    story_provider_->GetController(story_id, story_controller.NewRequest());
    if (!add_module_to_story.initial_data.is_null()) {
      modular::LinkPtr link;
      story_controller->GetLink(module_path.Clone(), link_name,
                                link.NewRequest());
      link->Set(nullptr /* json_path */, add_module_to_story.initial_data);
    }

    story_controller->AddModuleDeprecated(module_path.Clone(), module_name,
                                          module_url, link_name,
                                          fidl::MakeOptional(surface_relation));
  } else {
    FXL_LOG(WARNING) << "Unable to add module; no story provider";
  }
}

void SuggestionEngineImpl::PerformAddModuleAction(const Action& action) {
  if (story_provider_) {
    const auto& add_module = action.add_module();
    const auto& module_name = add_module.module_name;
    const auto& story_id = add_module.story_id;
    modular::StoryControllerPtr story_controller;
    story_provider_->GetController(story_id, story_controller.NewRequest());
    modular::Daisy daisy;
    fidl::Clone(add_module.daisy, &daisy);
    story_controller->AddModule(
        nullptr, module_name, std::move(daisy),
        fidl::MakeOptional(add_module.surface_relation));
  } else {
    FXL_LOG(WARNING) << "Unable to add module; no story provider";
  }
}

void SuggestionEngineImpl::PerformCustomAction(Action* action,
                                               const std::string& source_url,
                                               uint32_t story_color) {
  auto activity = debug_->RegisterOngoingActivity();
  auto custom_action = action->custom_action().Bind();
  custom_action->Execute(fxl::MakeCopyable(
      [this, activity, custom_action = std::move(custom_action), source_url,
       story_color](fidl::VectorPtr<ActionPtr> actions) {
        if (actions) {
          fidl::VectorPtr<Action> non_null_actions;
          for (auto& action : *actions) {
            if (action)
              non_null_actions.push_back(std::move(*action));
          }
          PerformActions(std::move(non_null_actions), source_url, story_color);
        }
      }));
}

void SuggestionEngineImpl::PlayMediaResponse(MediaResponsePtr media_response) {
  if (!audio_server_)
    return;

  auto activity = debug_->RegisterOngoingActivity();

  media_renderer_.Unbind();

  media::AudioRendererPtr audio_renderer;
  audio_server_->CreateRenderer(audio_renderer.NewRequest(),
                                media_renderer_.NewRequest());

  media_packet_producer_ = media_response->media_packet_producer.Bind();
  media_renderer_->SetMediaType(std::move(media_response->media_type));
  media::MediaPacketConsumerPtr consumer;
  media_renderer_->GetPacketConsumer(consumer.NewRequest());

  media_packet_producer_->Connect(std::move(consumer), [this, activity] {
    time_lord_.Unbind();
    media_timeline_consumer_.Unbind();

    for (auto& listener : speech_listeners_.ptrs()) {
      (*listener)->OnStatusChanged(SpeechStatus::RESPONDING);
    }

    media_renderer_->GetTimelineControlPoint(time_lord_.NewRequest());
    time_lord_->GetTimelineConsumer(media_timeline_consumer_.NewRequest());
    time_lord_->Prime([this, activity] {
      media::TimelineTransform tt;
      tt.reference_time =
          media::Timeline::local_now() + media::Timeline::ns_from_ms(30);
      tt.subject_time = media::kUnspecifiedTime;
      tt.reference_delta = tt.subject_delta = 1;

      HandleMediaUpdates(media::kInitialStatus, nullptr);

      media_timeline_consumer_->SetTimelineTransform(
          std::move(tt), [activity](bool completed) {});
    });
  });

  media_packet_producer_.set_error_handler([this] {
    for (auto& listener : speech_listeners_.ptrs()) {
      (*listener)->OnStatusChanged(SpeechStatus::IDLE);
    }
  });
}

void SuggestionEngineImpl::HandleMediaUpdates(
    uint64_t version,
    media::MediaTimelineControlPointStatusPtr status) {
  auto activity = debug_->RegisterOngoingActivity();

  if (status && status->end_of_stream) {
    for (auto& listener : speech_listeners_.ptrs()) {
      (*listener)->OnStatusChanged(SpeechStatus::IDLE);
    }
    media_packet_producer_ = nullptr;
    media_renderer_ = nullptr;
  } else {
    time_lord_->GetStatus(
        version,
        [this, activity](uint64_t next_version,
                         media::MediaTimelineControlPointStatus next_status) {
          HandleMediaUpdates(next_version,
                             fidl::MakeOptional(std::move(next_status)));
        });
  }
}

void SuggestionEngineImpl::OnContextUpdate(ContextUpdate update) {
  for (auto const& it : ranking_features) {
    auto result = TakeContextValue(&update, it.first);
    if (result.first) {
      it.second->UpdateContext(result.second);
    }
  }
  UpdateRanking();
}

}  // namespace modular

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  auto suggestion_engine =
      std::make_unique<modular::SuggestionEngineImpl>(app_context.get());
  fxl::WeakPtr<modular::SuggestionDebugImpl> debug = suggestion_engine->debug();
  modular::AppDriver<modular::SuggestionEngineImpl> driver(
      app_context->outgoing_services(), std::move(suggestion_engine),
      [&loop] { loop.QuitNow(); });

  // The |WaitUntilIdle| debug functionality escapes the main message loop to
  // perform its test.
  do {
    loop.Run();
  } while (debug && debug->FinishIdleCheck());

  return 0;
}
