// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_rate.h"
#include "lib/suggestion/fidl/suggestion_engine.fidl.h"
#include "lib/suggestion/fidl/user_input.fidl.h"
#include "peridot/bin/suggestion_engine/ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/kronk_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/proposal_hint_ranking_feature.h"
#include "peridot/bin/suggestion_engine/ranking_features/query_match_ranking_feature.h"
#include "peridot/bin/suggestion_engine/windowed_subscriber.h"
#include "peridot/lib/fidl/json_xdr.h"

#include <string>

namespace maxwell {

namespace {

// If media fails more than 5x over one second, stop trying to restart it.
constexpr modular::RateLimitedRetry::Threshold kMediaRetryLimit = {
    5, fxl::TimeDelta::FromSeconds(1)};

}  // namespace

SuggestionEngineImpl::SuggestionEngineImpl()
    : app_context_(app::ApplicationContext::CreateFromStartupInfo()),
      ask_suggestions_(new RankedSuggestions(&ask_channel_)),
      ask_dirty_(false),
      next_suggestions_(new RankedSuggestions(&next_channel_)),
      next_dirty_(false),
      media_service_retry_(kMediaRetryLimit) {
  app_context_->outgoing_services()->AddService<SuggestionEngine>(
      [this](fidl::InterfaceRequest<SuggestionEngine> request) {
        bindings_.AddBinding(this, std::move(request));
      });
  app_context_->outgoing_services()->AddService<SuggestionProvider>(
      [this](fidl::InterfaceRequest<SuggestionProvider> request) {
        suggestion_provider_bindings_.AddBinding(this, std::move(request));
      });
  app_context_->outgoing_services()->AddService<SuggestionDebug>(
      [this](fidl::InterfaceRequest<SuggestionDebug> request) {
        debug_bindings_.AddBinding(&debug_, std::move(request));
      });

  media_service_ =
      app_context_->ConnectToEnvironmentService<media::MediaService>();
  media_service_.set_connection_error_handler([this] {
    FXL_LOG(INFO) << "Media service connection error";
    media_service_ = nullptr;
    media_packet_producer_ = nullptr;
  });

  // Create common ranking features
  std::shared_ptr<RankingFeature> proposal_hint_feature =
      std::make_shared<ProposalHintRankingFeature>();
  std::shared_ptr<RankingFeature> kronk_feature =
      std::make_shared<KronkRankingFeature>();

  // TODO(jwnichols): Replace the code configuration of the ranking features
  // with a configuration file

  // Set up the next ranking features
  next_suggestions_->AddRankingFeature(1.0, proposal_hint_feature);
  next_suggestions_->AddRankingFeature(-0.1, kronk_feature);

  // Set up the query ranking features
  ask_suggestions_->AddRankingFeature(1.0, proposal_hint_feature);
  ask_suggestions_->AddRankingFeature(-0.1, kronk_feature);
  ask_suggestions_->AddRankingFeature(
      0, std::make_shared<QueryMatchRankingFeature>());
}

void SuggestionEngineImpl::AddNextProposal(ProposalPublisherImpl* source,
                                           ProposalPtr proposal) {
  // The component_url and proposal ID form a unique identifier for a proposal.
  // If one already exists, remove it before adding the new one.
  RemoveProposal(source->component_url(), proposal->id);

  auto suggestion =
      CreateSuggestionPrototype(source->component_url(), std::move(proposal));

  if (IsInterruption(*suggestion)) {
    debug_.OnInterrupt(suggestion);
    interruption_channel_.AddSuggestion(*suggestion);
  }

  next_suggestions_->AddSuggestion(suggestion);
  next_dirty_ = true;
}

void SuggestionEngineImpl::AddAskProposal(const std::string& source_url,
                                          ProposalPtr proposal) {
  RemoveProposal(source_url, proposal->id);
  auto suggestion = CreateSuggestionPrototype(source_url, std::move(proposal));
  ask_suggestions_->AddSuggestion(std::move(suggestion));
  ask_dirty_ = true;
}

void SuggestionEngineImpl::RemoveProposal(const std::string& component_url,
                                          const std::string& proposal_id) {
  const auto key = std::make_pair(component_url, proposal_id);
  auto toRemove = suggestion_prototypes_.find(key);
  if (toRemove != suggestion_prototypes_.end()) {
    RankedSuggestion* matchingSuggestion =
        next_suggestions_->GetSuggestion(component_url, proposal_id);
    if (matchingSuggestion && IsInterruption(*matchingSuggestion->prototype)) {
      interruption_channel_.RemoveSuggestion(*matchingSuggestion->prototype);
    }
    if (ask_suggestions_->RemoveProposal(component_url, proposal_id)) {
      ask_dirty_ = true;
    }
    if (next_suggestions_->RemoveProposal(component_url, proposal_id)) {
      next_dirty_ = true;
    }
    suggestion_prototypes_.erase(toRemove);
  }
}

// |SuggestionProvider|
void SuggestionEngineImpl::Query(
    fidl::InterfaceHandle<SuggestionListener> listener,
    UserInputPtr input,
    int count) {
  // TODO(jwnichols): I'm not sure this is correct or should be here
  speech_listeners_.ForAllPtrs([](FeedbackListener* listener) {
    listener->OnStatusChanged(SpeechStatus::PROCESSING);
  });

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
  std::string query = input->text;
  if (!query.empty()) {
    std::string formattedQuery;
    modular::XdrWrite(&formattedQuery, &query, modular::XdrFilter<std::string>);
    context_writer_->WriteEntityTopic(kQueryContextKey, formattedQuery);
  }

  // Step 3
  // TODO(rosswang/jwnichols): move the subscriber and ask channel into the
  // query processor
  std::unique_ptr<WindowedSuggestionSubscriber> subscriber =
      std::make_unique<WindowedSuggestionSubscriber>(
          ask_suggestions_, std::move(listener), count);

  subscriber->set_connection_error_handler([this] {
    CleanUpPreviousQuery();
  });  // called if the listener disconnects

  ask_channel_.AddSubscriber(std::move(subscriber));

  // Steps 4 - 6
  active_query_ = std::make_unique<QueryProcessor>(this, std::move(input));
}

void SuggestionEngineImpl::Validate() {
  if (next_dirty_) {
    next_suggestions_->Rank();
    debug_.OnNextUpdate(next_suggestions_);
    next_dirty_ = false;
  }
  if (ask_dirty_) {
    // The only way ask can be dirty outside of a query is removals, so we don't
    // need to rerank.
    ask_channel_.DispatchInvalidate();
    ask_dirty_ = false;
  }
}

void SuggestionEngineImpl::PrimeSpeechCapture() {
  if (media_service_) {
    media_service_->CreateAudioCapturer(media_capturer_.NewRequest());
    media_capturer_->GetSupportedMediaTypes([](auto) {});
    media_capturer_.set_connection_error_handler([this] {
      media_capturer_.reset();

      if (media_service_retry_.ShouldRetry()) {
        FXL_LOG(INFO) << "Restarting closed media capturer";
        PrimeSpeechCapture();
      } else {
        FXL_LOG(WARNING) << "Media input failed more than "
                         << kMediaRetryLimit.count << " times in "
                         << kMediaRetryLimit.period.ToSecondsF()
                         << " seconds; disabling speech capture.";
      }
    });
  }
}

fidl::InterfaceHandle<media::MediaCapturer>
SuggestionEngineImpl::GetMediaCapturer() {
  // HACK(rosswang): Maintain a singleton media capturer. The media subsystem
  // behaves unpredictably when the pipeline is mutated due to race conditions.
  // Fix once media API redesign is complete; fix TBD pending redesign.
  if (!media_capturer_binding_) {
    media_capturer_binding_ =
        std::make_unique<fidl::Binding<media::MediaCapturer>>(
            media_capturer_.get());
    media_capturer_binding_->set_connection_error_handler([this] {
      media_capturer_->Stop();
      media_capturer_binding_ = nullptr;

      // With the hacks in place right now, this tends to mean that Kronk
      // hasn't received any new packets from the media capturer. That or
      // Kronk crashed.
      media_capturer_.reset();
      FXL_LOG(INFO) << "Restarting possible dead media capturer";
      PrimeSpeechCapture();
    });
    return media_capturer_binding_->NewBinding();
  } else {
    // This song and dance makes the handle look valid (invalid handles fail
    // FIDL validation).
    media::MediaCapturerPtr dummy;
    dummy.NewRequest();
    return dummy.PassInterfaceHandle();
  }
}

// |SuggestionProvider|
void SuggestionEngineImpl::BeginSpeechCapture(
    fidl::InterfaceHandle<TranscriptionListener> transcription_listener) {
  if (speech_to_text_ && media_capturer_) {
    speech_to_text_->BeginCapture(GetMediaCapturer(),
                                  std::move(transcription_listener));
  } else {
    // Requesting speech capture without the requisite services is an immediate
    // error.
    TranscriptionListenerPtr::Create(std::move(transcription_listener))
        ->OnError();
  }
}

// |SuggestionProvider|
void SuggestionEngineImpl::ListenForHotword(
    fidl::InterfaceHandle<HotwordListener> hotword_listener) {
  if (speech_to_text_ && media_capturer_) {
    speech_to_text_->ListenForHotword(GetMediaCapturer(),
                                      std::move(hotword_listener));
  }
}

// |SuggestionProvider|
void SuggestionEngineImpl::SubscribeToInterruptions(
    fidl::InterfaceHandle<SuggestionListener> listener) {
  interruption_channel_.AddSubscriber(std::move(listener), *next_suggestions_);
}

// |SuggestionProvider|
void SuggestionEngineImpl::SubscribeToNext(
    fidl::InterfaceHandle<SuggestionListener> listener,
    int count) {
  std::unique_ptr<WindowedSuggestionSubscriber> subscriber =
      std::make_unique<WindowedSuggestionSubscriber>(
          next_suggestions_, std::move(listener), count);
  next_channel_.AddSubscriber(std::move(subscriber));
}

// |SuggestionProvider|
void SuggestionEngineImpl::RegisterFeedbackListener(
    fidl::InterfaceHandle<FeedbackListener> speech_listener) {
  speech_listeners_.AddInterfacePtr(
      FeedbackListenerPtr::Create(std::move(speech_listener)));
}

// |SuggestionProvider|
void SuggestionEngineImpl::NotifyInteraction(
    const fidl::String& suggestion_uuid,
    InteractionPtr interaction) {
  // Find the suggestion
  bool suggestion_in_ask = false;
  RankedSuggestion* suggestion =
      next_suggestions_->GetSuggestion(suggestion_uuid);
  if (!suggestion) {
    suggestion = ask_suggestions_->GetSuggestion(suggestion_uuid);
    suggestion_in_ask = true;
  }

  // If it exists (and it should), perform the action and clean up
  if (suggestion) {
    std::string log_detail = suggestion->prototype
                                 ? short_proposal_str(*suggestion->prototype)
                                 : "invalid";

    FXL_LOG(INFO) << (interaction->type == InteractionType::SELECTED
                          ? "Accepted"
                          : "Dismissed")
                  << " suggestion " << suggestion_uuid << " (" << log_detail
                  << ")";

    debug_.OnSuggestionSelected(suggestion->prototype);

    auto& proposal = suggestion->prototype->proposal;
    if (interaction->type == InteractionType::SELECTED) {
      PerformActions(proposal->on_selected, proposal->display->color);
    }

    if (suggestion_in_ask) {
      CleanUpPreviousQuery();
    } else {
      RemoveProposal(suggestion->prototype->source_url, proposal->id);
    }

    Validate();
  } else {
    FXL_LOG(WARNING) << "Requested suggestion prototype not found. UUID: "
                     << suggestion_uuid;
  }
}

// |SuggestionEngine|
void SuggestionEngineImpl::RegisterProposalPublisher(
    const fidl::String& url,
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
    const fidl::String& url,
    fidl::InterfaceHandle<QueryHandler> query_handler_handle) {
  auto query_handler = QueryHandlerPtr::Create(std::move(query_handler_handle));
  query_handlers_.emplace_back(std::move(query_handler), url);
}

// |SuggestionEngine|
void SuggestionEngineImpl::Initialize(
    fidl::InterfaceHandle<modular::StoryProvider> story_provider,
    fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
    fidl::InterfaceHandle<ContextWriter> context_writer) {
  story_provider_.Bind(std::move(story_provider));
  focus_provider_ptr_.Bind(std::move(focus_provider));
  context_writer_.Bind(std::move(context_writer));

  timeline_stories_watcher_.reset(new TimelineStoriesWatcher(&story_provider_));

  PrimeSpeechCapture();
}

void SuggestionEngineImpl::SetSpeechToText(
    fidl::InterfaceHandle<SpeechToText> service) {
  speech_to_text_ = SpeechToTextPtr::Create(std::move(service));
}

// end SuggestionEngine

void SuggestionEngineImpl::CleanUpPreviousQuery() {
  active_query_ = nullptr;

  // Clean up the suggestions
  for (auto& suggestion : ask_suggestions_->Get()) {
    suggestion_prototypes_.erase(
        std::make_pair(suggestion->prototype->source_url,
                       suggestion->prototype->proposal->id));
  }
  ask_suggestions_->RemoveAllSuggestions();

  // Clean up the query suggestion subscriber
  ask_channel_.RemoveAllSubscribers();
}

SuggestionPrototype* SuggestionEngineImpl::CreateSuggestionPrototype(
    const std::string& source_url,
    ProposalPtr proposal) {
  auto prototype_pair =
      suggestion_prototypes_.emplace(std::make_pair(source_url, proposal->id),
                                     std::make_unique<SuggestionPrototype>());
  auto suggestion_prototype = prototype_pair.first->second.get();
  suggestion_prototype->suggestion_id = RandomUuid();
  suggestion_prototype->source_url = source_url;
  suggestion_prototype->timestamp = fxl::TimePoint::Now();
  suggestion_prototype->proposal = std::move(proposal);

  return suggestion_prototype;
}

void SuggestionEngineImpl::PerformActions(
    const fidl::Array<maxwell::ActionPtr>& actions,
    uint32_t story_color) {
  // TODO(rosswang): If we're asked to add multiple modules, we probably
  // want to add them to the same story. We can't do that yet, but we need
  // to receive a StoryController anyway (not optional atm.).
  for (const auto& action : actions) {
    switch (action->which()) {
      case Action::Tag::CREATE_STORY: {
        const auto& create_story = action->get_create_story();

        if (story_provider_) {
          // TODO(afergan): Make this more robust later. For now, we
          // always assume that there's extra info and that it's a color.
          fidl::Map<fidl::String, fidl::String> extra_info;
          char hex_color[11];
          sprintf(hex_color, "0x%x", story_color);
          extra_info["color"] = hex_color;
          auto& initial_data = create_story->initial_data;
          auto& module_id = create_story->module_id;
          story_provider_->CreateStoryWithInfo(
              create_story->module_id, std::move(extra_info),
              std::move(initial_data),
              [this, module_id](const fidl::String& story_id) {
                modular::StoryControllerPtr story_controller;
                story_provider_->GetController(story_id,
                                               story_controller.NewRequest());
                FXL_LOG(INFO) << "Creating story with module " << module_id;

                story_controller->GetInfo(fxl::MakeCopyable(
                    // TODO(thatguy): We should not be std::move()ing
                    // story_controller *while we're calling it*.
                    [this, controller = std::move(story_controller)](
                        modular::StoryInfoPtr story_info,
                        modular::StoryState state) {
                      FXL_LOG(INFO)
                          << "Requesting focus for story_id " << story_info->id;
                      focus_provider_ptr_->Request(story_info->id);
                    }));
              });
        } else {
          FXL_LOG(WARNING) << "Unable to add module; no story provider";
        }
        break;
      }
      case Action::Tag::FOCUS_STORY: {
        const auto& focus_story = action->get_focus_story();
        FXL_LOG(INFO) << "Requesting focus for story_id "
                      << focus_story->story_id;
        focus_provider_ptr_->Request(focus_story->story_id);
        break;
      }
      case Action::Tag::ADD_MODULE_TO_STORY: {
        if (story_provider_) {
          const auto& add_module_to_story = action->get_add_module_to_story();
          const auto& story_id = add_module_to_story->story_id;
          const auto& module_name = add_module_to_story->module_name;
          const auto& module_url = add_module_to_story->module_url;
          const auto& link_name = add_module_to_story->link_name;
          const auto& module_path = add_module_to_story->module_path;
          const auto& surface_relation = add_module_to_story->surface_relation;

          FXL_LOG(INFO) << "Adding module " << module_url << " to story "
                        << story_id;

          modular::StoryControllerPtr story_controller;
          story_provider_->GetController(story_id,
                                         story_controller.NewRequest());
          if (!add_module_to_story->initial_data.is_null()) {
            modular::LinkPtr link;
            story_controller->GetLink(module_path.Clone(), link_name,
                                      link.NewRequest());
            link->Set(nullptr /* json_path */,
                      add_module_to_story->initial_data);
          }

          story_controller->AddModule(module_path.Clone(), module_name,
                                      module_url, link_name,
                                      surface_relation.Clone());
        } else {
          FXL_LOG(WARNING) << "Unable to add module; no story provider";
        }

        break;
      }
      case Action::Tag::CUSTOM_ACTION: {
        auto custom_action = maxwell::CustomActionPtr::Create(
            std::move(action->get_custom_action()));
        custom_action->Execute(fxl::MakeCopyable(
            [this, custom_action = std::move(custom_action),
             story_color](fidl::Array<maxwell::ActionPtr> actions) {
              if (actions)
                PerformActions(std::move(actions), story_color);
            }));
        break;
      }
      default:
        FXL_LOG(WARNING) << "Unknown action tag " << (uint32_t)action->which();
    }
  }
}

void SuggestionEngineImpl::PlayMediaResponse(MediaResponsePtr media_response) {
  if (!media_service_)
    return;

  media::AudioRendererPtr audio_renderer;
  media::MediaRendererPtr media_renderer;
  media_service_->CreateAudioRenderer(audio_renderer.NewRequest(),
                                      media_renderer.NewRequest());

  media_sink_.reset();
  media_service_->CreateSink(media_renderer.PassInterfaceHandle(),
                             media_sink_.NewRequest());

  media_packet_producer_ = media::MediaPacketProducerPtr::Create(
      std::move(media_response->media_packet_producer));
  media_sink_->ConsumeMediaType(
      std::move(media_response->media_type),
      [this](fidl::InterfaceHandle<media::MediaPacketConsumer> consumer) {
        media_packet_producer_->Connect(
            media::MediaPacketConsumerPtr::Create(std::move(consumer)), [this] {
              time_lord_.reset();
              media_timeline_consumer_.reset();

              speech_listeners_.ForAllPtrs([](FeedbackListener* listener) {
                listener->OnStatusChanged(SpeechStatus::RESPONDING);
              });

              media_sink_->GetTimelineControlPoint(time_lord_.NewRequest());
              time_lord_->GetTimelineConsumer(
                  media_timeline_consumer_.NewRequest());
              time_lord_->Prime([this] {
                auto tt = media::TimelineTransform::New();
                tt->reference_time = media::Timeline::local_now() +
                                     media::Timeline::ns_from_ms(30);
                tt->subject_time = media::kUnspecifiedTime;
                tt->reference_delta = tt->subject_delta = 1;

                HandleMediaUpdates(
                    media::MediaTimelineControlPoint::kInitialStatus, nullptr);

                media_timeline_consumer_->SetTimelineTransform(
                    std::move(tt), [](bool completed) {});
              });
            });
      });

  media_packet_producer_.set_connection_error_handler([this] {
    speech_listeners_.ForAllPtrs([](FeedbackListener* listener) {
      listener->OnStatusChanged(SpeechStatus::IDLE);
    });
  });
}

void SuggestionEngineImpl::HandleMediaUpdates(
    uint64_t version,
    media::MediaTimelineControlPointStatusPtr status) {
  if (status && status->end_of_stream) {
    speech_listeners_.ForAllPtrs([](FeedbackListener* listener) {
      listener->OnStatusChanged(SpeechStatus::IDLE);
    });
    media_packet_producer_ = nullptr;
    media_sink_ = nullptr;
  } else {
    time_lord_->GetStatus(
        version, [this](uint64_t next_version,
                        media::MediaTimelineControlPointStatusPtr next_status) {
          HandleMediaUpdates(next_version, std::move(next_status));
        });
  }
}

}  // namespace maxwell

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;
  maxwell::SuggestionEngineImpl app;
  loop.Run();
  return 0;
}
