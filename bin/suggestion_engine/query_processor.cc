// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/query_processor.h"

#include "lib/fsl/tasks/message_loop.h"

namespace maxwell {

namespace {

// Force queries to complete after some delay for better UX until/unless we can
// bring back staggered results in a way that isn't jarring and doesn't overly
// complicate the API.
constexpr fxl::TimeDelta kQueryTimeout = fxl::TimeDelta::FromSeconds(9);

}  // namespace

QueryProcessor::QueryProcessor(SuggestionEngineImpl* engine,
                               maxwell::UserInputPtr input)
    : engine_(engine),
      input_(std::move(input)),
      has_media_response_(false),
      request_ended_(false),
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

void QueryProcessor::DispatchQuery(const QueryHandlerRecord& handler_record) {
  outstanding_handlers_.insert(handler_record.url);
  handler_record.handler->OnQuery(
      input_.Clone(),
      [w = weak_ptr_factory_.GetWeakPtr(),
       handler_url = handler_record.url](QueryResponsePtr response) {
        if (w)
          w->HandlerCallback(handler_url, std::move(response));
      });
}

void QueryProcessor::HandlerCallback(const std::string& handler_url,
                                     QueryResponsePtr response) {
  // TODO(rosswang): defer selection of "I don't know" responses
  if (!has_media_response_ && response->media_response) {
    has_media_response_ = true;

    // TODO(rosswang): Wait for other potential voice responses so that we
    // choose the best one. We don't have criteria for "best" yet, and we only
    // have one agent (Kronk) with voice responses now, so play immediately.

    // TODO(rosswang): allow falling back on natural language text response
    // without a spoken response
    engine_->speech_listeners_.ForAllPtrs([&](FeedbackListener* listener) {
      listener->OnTextResponse(response->natural_language_response);
    });

    engine_->PlayMediaResponse(std::move(response->media_response));
  }

  // Ranking currently happens as each set of proposals are added.
  for (auto& proposal : response->proposals) {
    engine_->AddAskProposal(handler_url, std::move(proposal));
  }
  engine_->ask_suggestions_->Rank(*input_);
  // Rank includes an invalidate dispatch
  engine_->ask_dirty_ = false;

  FXL_VLOG(1) << "Handler " << handler_url << " complete";

  outstanding_handlers_.erase(outstanding_handlers_.find(handler_url));
  FXL_VLOG(1) << outstanding_handlers_.size() << " remaining";
  if (outstanding_handlers_.empty()) {
    EndRequest();
  }
}

void QueryProcessor::EndRequest() {
  FXL_DCHECK(!request_ended_);

  engine_->debug_.OnAskStart(input_->text, engine_->ask_suggestions_);
  engine_->ask_channel_.DispatchOnProcessingChange(false);

  if (!has_media_response_) {
    // there was no media response for this query, so idle immediately
    engine_->speech_listeners_.ForAllPtrs([](FeedbackListener* listener) {
      listener->OnStatusChanged(SpeechStatus::IDLE);
    });
  }

  weak_ptr_factory_.InvalidateWeakPtrs();
  request_ended_ = true;
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

}  // namespace maxwell
