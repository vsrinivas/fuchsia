// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_ENGINE_IMPL_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_ENGINE_IMPL_H_

#include <map>
#include <string>
#include <vector>

#include "lib/app/cpp/application_context.h"

#include "peridot/bin/suggestion_engine/debug.h"
#include "peridot/bin/suggestion_engine/filter.h"
#include "peridot/bin/suggestion_engine/interruptions_channel.h"
#include "peridot/bin/suggestion_engine/proposal_publisher_impl.h"
#include "peridot/bin/suggestion_engine/query_handler_record.h"
#include "peridot/bin/suggestion_engine/query_processor.h"
#include "peridot/bin/suggestion_engine/ranked_suggestions.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"
#include "peridot/bin/suggestion_engine/timeline_stories_filter.h"
#include "peridot/bin/suggestion_engine/timeline_stories_watcher.h"
#include "peridot/lib/bound_set/bound_set.h"
#include "peridot/lib/util/rate_limited_retry.h"

#include "lib/context/fidl/context_writer.fidl.h"
#include "lib/media/fidl/media_service.fidl.h"
#include "lib/story/fidl/story_provider.fidl.h"
#include "lib/suggestion/fidl/suggestion_engine.fidl.h"
#include "lib/user/fidl/focus.fidl.h"

#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace maxwell {

class ProposalPublisherImpl;
class QueryProcessor;

const std::string kQueryContextKey = "/suggestion_engine/current_query";

// This class is currently responsible for 3 things:
//
// 1) Maintaining repositories of ranked Suggestions (stored inside
//    the RankedSuggestions class) for both Ask and Next proposals.
//  a) Ask suggestions are issued by AskHandlers, in a pull-based model
//     in response to Ask queries. Ask queries are issued via the
//     DispatchAsk method, and Suggestions are asynchronously returned
//     through DispatchAsk's callback.
//
//     The set of Ask proposals for the latest query are currently
//     buffered in the ask_suggestions_ member, though this process can
//     be made entirely stateless.
//
//  b) Next suggestions are issued by ProposalPublishers through the
//     Propose method, and can be issued at any time. These proposals
//     are stored in the next_suggestions_ member.
//
//   Whenever a RankedSuggestions object is updated, that update is pushed
//   to its registered subscribers (SuggestionSubscribers). These subscribers
//   are registered on a SuggestionChannel object -- each RankedSuggestions
//   object has a single SuggestionChannel.
//
// 2) Storing FIDL bindings for AskHandlers and ProposalPublishers.
//
//  a) ProposalPublishers (for Next Suggestions) can be registered via the
//     RegisterPublisher method.
//
//  b) AskHandlers are currently registered through
//     ProposalPublisher.RegisterAskHandler, but this is unnecessary coupling
//     between the ProposalPublisher (a Next interface) and AskHandler (an Ask
//     interface), so this should eventually be changed with the addition of
//     SuggestionEngine.RegisterAskHandler
//
// 3) Acts as a SuggestionProvider for those wishing to subscribe to
//    Suggestions.
class SuggestionEngineImpl : public SuggestionEngine,
                             public SuggestionProvider {
 public:
  SuggestionEngineImpl();

  // TODO(andrewosh): The following two methods should be removed. New
  // ProposalPublishers should be created whenever they're requested, and they
  // should be erased automatically when the client disconnects (they should be
  // stored in a BindingSet with an error handler that performs removal).
  void RemoveSourceClient(const std::string& component_url) {
    proposal_publishers_.erase(component_url);
  };

  // Should only be called from ProposalPublisherImpl.
  void AddNextProposal(ProposalPublisherImpl* source, ProposalPtr proposal);

  // Should only be called from ProposalPublisherImpl.
  void RemoveProposal(const std::string& component_url,
                      const std::string& proposal_id);

  // |SuggestionProvider|
  void SubscribeToInterruptions(
      fidl::InterfaceHandle<SuggestionListener> listener) override;

  // |SuggestionProvider|
  void SubscribeToNext(fidl::InterfaceHandle<SuggestionListener> listener,
                       int count) override;

  // |SuggestionProvider|
  void Query(fidl::InterfaceHandle<SuggestionListener> listener,
             UserInputPtr input,
             int count) override;

  // |SuggestionProvider|
  void RegisterFeedbackListener(
      fidl::InterfaceHandle<FeedbackListener> speech_listener) override;

  // |SuggestionProvider|
  void BeginSpeechCapture(fidl::InterfaceHandle<TranscriptionListener>
                              transcription_listener) override;

  // |SuggestionProvider|
  void ListenForHotword(
      fidl::InterfaceHandle<HotwordListener> hotword_listener) override;

  // When a user interacts with a Suggestion, the suggestion engine will be
  // notified of consumed suggestion's ID. With this, we will do two things:
  //
  // 1) Perform the Action contained in the Suggestion
  //    (suggestion->proposal->on_selected)
  //
  //    Action handling should be extracted into separate classes to simplify
  //    SuggestionEngineImpl (i.e. an ActionManager which delegates action
  //    execution to ActionHandlers based on the Action's tag).
  //
  // 2) Remove consumed Suggestion from the next_suggestions_ repository,
  //    if it came from there.  Clear the ask_suggestions_ repository if
  //    it came from there.
  //
  // |SuggestionProvider|
  void NotifyInteraction(const fidl::String& suggestion_uuid,
                         InteractionPtr interaction) override;

  // |SuggestionEngine|
  void RegisterProposalPublisher(
      const fidl::String& url,
      fidl::InterfaceRequest<ProposalPublisher> publisher) override;

  // |SuggestionEngine|
  void RegisterQueryHandler(
      const fidl::String& url,
      fidl::InterfaceHandle<QueryHandler> query_handler) override;

  // |SuggestionEngine|
  void Initialize(fidl::InterfaceHandle<modular::StoryProvider> story_provider,
                  fidl::InterfaceHandle<modular::FocusProvider> focus_provider,
                  fidl::InterfaceHandle<ContextWriter> context_writer) override;

  // |SuggestionEngine|
  void SetSpeechToText(fidl::InterfaceHandle<SpeechToText> service) override;

  // re-ranks dirty channels and dispatches updates
  void Validate();

 private:
  friend class QueryProcessor;

  // TODO(rosswang): move elsewhere, though this should ideally be unnecessary
  void PrimeSpeechCapture();

  // HACK(rosswang): Maintains a singleton media capturer (and returns it or a
  // dummy open handle). See definition for details.
  fidl::InterfaceHandle<media::MediaCapturer> GetMediaCapturer();

  // Cleans up all resources associated with a query, including clearing
  // the previous ask suggestions, closing any still open SuggestionListeners,
  // etc.
  void CleanUpPreviousQuery();

  // TODO(jwnichols): Remove when we change the way ask suggestions are
  // returned to SysUI
  void AddAskProposal(const std::string& source_url, ProposalPtr proposal);

  // Searches for a SuggestionPrototype in the Next and Ask lists.
  SuggestionPrototype* FindSuggestion(std::string suggestion_id);

  SuggestionPrototype* CreateSuggestionPrototype(const std::string& source_url,
                                                 ProposalPtr proposal);

  std::string RandomUuid() {
    static uint64_t id = 0;
    // TODO(rosswang): real UUIDs
    return std::to_string(id++);
  }

  // TODO(andrewosh): Performing actions should be handled by a separate
  // interface that's passed to the SuggestionEngineImpl.
  void PerformActions(const fidl::Array<maxwell::ActionPtr>& actions,
                      uint32_t story_color);

  void PlayMediaResponse(MediaResponsePtr media_response);
  void HandleMediaUpdates(uint64_t version,
                          media::MediaTimelineControlPointStatusPtr status);

  std::unique_ptr<app::ApplicationContext> app_context_;

  fidl::BindingSet<SuggestionEngine> bindings_;
  fidl::BindingSet<SuggestionProvider> suggestion_provider_bindings_;
  fidl::BindingSet<SuggestionDebug> debug_bindings_;

  // Both story_provider_ and focus_provider_ptr are used exclusively during
  // Action execution (in the PerformActions call inside NotifyInteraction).
  //
  // These are required to create new Stories and interact with the current
  // Story.
  modular::StoryProviderPtr story_provider_;
  fidl::InterfacePtr<modular::FocusProvider> focus_provider_ptr_;

  // Watches for changes in StoryInfo from the StoryProvider, acts as a filter
  // for Proposals on all channels, and notifies when there are changes so that
  // we can re-filter Proposals.
  //
  // Initialized late in Initialize().
  std::unique_ptr<TimelineStoriesWatcher> timeline_stories_watcher_;

  // TODO(thatguy): All Channels also get a ReevaluateFilters method, which
  // would remove Suggestions that are now filtered or add
  // new ones that are no longer filtered.

  // The repository of raw suggestion prototypes.
  std::map<std::pair<std::string, std::string>,
           std::unique_ptr<SuggestionPrototype>>
      suggestion_prototypes_;

  // TODO(rosswang): it may be worthwhile to collapse these trios into classes
  // Channels that dispatch outbound suggestions to SuggestionListeners.
  SuggestionChannel ask_channel_;
  RankedSuggestions* ask_suggestions_;
  bool ask_dirty_;

  SuggestionChannel next_channel_;
  RankedSuggestions* next_suggestions_;
  bool next_dirty_;

  InterruptionsChannel interruption_channel_;

  // The set of all QueryHandlers that have been registered mapped to their
  // URLs (stored as strings).
  std::vector<QueryHandlerRecord> query_handlers_;

  // The ProposalPublishers that have registered with the SuggestionEngine.
  std::map<std::string, std::unique_ptr<ProposalPublisherImpl>>
      proposal_publishers_;

  // TODO(andrewosh): Why is this necessary at this level?
  ProposalFilter filter_;

  // The ContextWriter that publishes the current user query to the
  // ContextEngine.
  ContextWriterPtr context_writer_;

  std::unique_ptr<QueryProcessor> active_query_;

  modular::RateLimitedRetry media_service_retry_;
  media::MediaServicePtr media_service_;
  media::MediaSinkPtr media_sink_;
  media::MediaPacketProducerPtr media_packet_producer_;
  media::MediaTimelineControlPointPtr time_lord_;
  media::TimelineConsumerPtr media_timeline_consumer_;

  SpeechToTextPtr speech_to_text_;
  fidl::InterfacePtrSet<FeedbackListener> speech_listeners_;

  // The debugging interface for all Suggestions.
  SuggestionDebugImpl debug_;

  // Media input pipeline updates don't work quite right and creating new media
  // capturers is nontrivial, so for now pass a proxy to the speech capture
  // service to let us know when we need to give it a new one.
  media::MediaCapturerPtr media_capturer_;
  std::unique_ptr<fidl::Binding<media::MediaCapturer>> media_capturer_binding_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_SUGGESTION_ENGINE_IMPL_H_
