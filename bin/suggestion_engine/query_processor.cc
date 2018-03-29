// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/query_processor.h"

#include "lib/fsl/tasks/message_loop.h"
#include "peridot/bin/suggestion_engine/suggestion_engine_helper.h"
#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

namespace modular {

namespace {

// Force queries to complete after some delay for better UX until/unless we can
// bring back staggered results in a way that isn't jarring and doesn't overly
// complicate the API.
constexpr fxl::TimeDelta kQueryTimeout = fxl::TimeDelta::FromSeconds(9);

}  // namespace

QueryProcessor::QueryProcessor(SuggestionEngineImpl* engine,
                               fidl::InterfaceHandle<QueryListener> listener,
                               UserInput input,
                               size_t max_results)
    : engine_(engine),
      listener_(listener.Bind()),
      input_(std::move(input)),
      max_results_(max_results),
      dirty_(false),
      has_media_response_(false),
      request_ended_(false),
      activity_(engine->debug()->RegisterOngoingActivity()),
      weak_ptr_factory_(this) {
  if (engine_->query_handlers_.empty()) {
    EndRequest();
  } else {
    for (const auto& handler_record : engine_->query_handlers_) {
      DispatchQuery(handler_record);
    }

    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [w = weak_ptr_factory_.GetWeakPtr()] {
          if (w)
            w->TimeOut();
        },
        kQueryTimeout);
  }
}

// TODO(rosswang): Consider moving some of the cleanup logic into here, but
// beware that this may not happen until after the next QueryProcessor has been
// constructed (active_query_ = std::make_unique...).
QueryProcessor::~QueryProcessor() {
  if (!request_ended_)
    EndRequest();
}

void QueryProcessor::AddProposal(const std::string& source_url,
                                 Proposal proposal) {
  if (engine_->query_suggestions_.RemoveProposal(source_url, proposal.id)) {
    dirty_ = true;
  }

  auto suggestion = CreateSuggestionPrototype(
      &engine_->query_prototypes_, source_url, std::move(proposal));
  engine_->query_suggestions_.AddSuggestion(std::move(suggestion));
  dirty_ = true;
}

void QueryProcessor::DispatchQuery(const QueryHandlerRecord& handler_record) {
  outstanding_handlers_.insert(handler_record.url);
  handler_record.handler->OnQuery(
      input_,
      [w = weak_ptr_factory_.GetWeakPtr(),
       handler_url = handler_record.url](QueryResponse response) {
        if (w)
          w->HandlerCallback(handler_url, std::move(response));
      });
}

void QueryProcessor::HandlerCallback(const std::string& handler_url,
                                     QueryResponse response) {
  // TODO(rosswang): defer selection of "I don't know" responses
  if (!has_media_response_ && response.media_response) {
    has_media_response_ = true;

    // TODO(rosswang): Wait for other potential voice responses so that we
    // choose the best one. We don't have criteria for "best" yet, and we only
    // have one agent (Kronk) with voice responses now, so play immediately.

    // TODO(rosswang): allow falling back on natural language text response
    // without a spoken response
    fidl::StringPtr text_response =
        std::move(response.natural_language_response);
    if (!text_response)
      text_response = "";
    for (auto& listener : engine_->speech_listeners_.ptrs()) {
      (*listener)->OnTextResponse(text_response);
    }

    engine_->PlayMediaResponse(std::move(response.media_response));
  }

  // Ranking currently happens as each set of proposals are added.
  for (size_t i = 0; i < response.proposals->size(); ++i) {
    AddProposal(handler_url, std::move(response.proposals->at(i)));
  }
  engine_->query_suggestions_.Rank(input_);
  // Rank includes an invalidate dispatch
  dirty_ = false;

  // Update the QueryListener with new results
  NotifyOfResults();

  // Update the suggestion engine debug interface
  engine_->debug_->OnAskStart(input_.text, &engine_->query_suggestions_);

  FXL_VLOG(1) << "Handler " << handler_url << " complete";

  outstanding_handlers_.erase(outstanding_handlers_.find(handler_url));
  FXL_VLOG(1) << outstanding_handlers_.size() << " remaining";
  if (outstanding_handlers_.empty()) {
    EndRequest();
  }
}

void QueryProcessor::EndRequest() {
  FXL_DCHECK(!request_ended_);

  engine_->debug_->OnAskStart(input_.text, &engine_->query_suggestions_);
  listener_->OnQueryComplete();

  if (!has_media_response_) {
    // there was no media response for this query, so idle immediately
    for (auto& listener : engine_->speech_listeners_.ptrs()) {
      (*listener)->OnStatusChanged(SpeechStatus::IDLE);
    }
  }

  weak_ptr_factory_.InvalidateWeakPtrs();
  request_ended_ = true;
  activity_ = nullptr;
}

void QueryProcessor::TimeOut() {
  if (!outstanding_handlers_.empty()) {
    FXL_LOG(INFO) << "Query timeout. Still awaiting results from:";
    for (const std::string& handler_url : outstanding_handlers_) {
      FXL_LOG(INFO) << "    " << handler_url;
    }

    EndRequest();
  }
}

void QueryProcessor::NotifyOfResults() {
  const auto& suggestion_vector = engine_->query_suggestions_.Get();

  fidl::VectorPtr<Suggestion> window;
  for (size_t i = 0; i < max_results_ && i < suggestion_vector.size(); i++) {
    window.push_back(CreateSuggestion(*suggestion_vector[i]));
  }

  if (window)
    listener_->OnQueryResults(std::move(window));
}

}  // namespace modular
