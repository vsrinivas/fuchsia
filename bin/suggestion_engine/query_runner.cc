// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include "peridot/bin/suggestion_engine/query_runner.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/zx/time.h>

namespace modular {

namespace {

// Force queries to complete after some delay for better UX until/unless we can
// bring back staggered results in a way that isn't jarring and doesn't overly
// complicate the API.
constexpr zx::duration kQueryTimeout = zx::sec(9);

}  // namespace

class QueryRunner::HandlerRequest {
 public:
  HandlerRequest(fxl::WeakPtr<QueryRunner> runner,
                 const std::string& handler_url)
      : runner_(runner), handler_url_(handler_url) {}

  ~HandlerRequest() {
    if (!runner_)
      return;

    if (completed_) {
      FXL_VLOG(1) << "Handler " << handler_url_ << " complete";
    } else {
      FXL_LOG(WARNING) << "Handler " << handler_url_
                       << " closed without completing";
    }

    // find + erase rather than erase key to properly handle duplicate URLs
    // (only remove one)
    runner_->outstanding_handlers_.erase(
        runner_->outstanding_handlers_.find(handler_url_));
    FXL_VLOG(1) << runner_->outstanding_handlers_.size() << " remaining";
    if (runner_->outstanding_handlers_.empty()) {
      runner_->EndRequest();
    }
  }

  void Complete(fuchsia::modular::QueryResponse response) {
    if (runner_) {
      runner_->on_query_response_callback_(handler_url_, std::move(response));
      completed_ = true;
    }
  }

 private:
  fxl::WeakPtr<QueryRunner> runner_;
  std::string handler_url_;
  bool completed_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(HandlerRequest);
};

QueryRunner::QueryRunner(
    fidl::InterfaceHandle<fuchsia::modular::QueryListener> listener,
    fuchsia::modular::UserInput input, int count)
    : listener_(listener.Bind()),
      input_(input),
      max_results_(count),
      request_ended_(false),
      weak_ptr_factory_(this) {}

// TODO(rosswang): Consider moving some of the cleanup logic into here, but
// beware that this may not happen until after the next QueryProcessor has been
// constructed (active_query_ = std::make_unique...).
QueryRunner::~QueryRunner() {
  if (!request_ended_) {
    EndRequest();
  }
}

void QueryRunner::Run(const std::vector<QueryHandlerRecord>& query_handlers) {
  if (query_handlers.empty()) {
    EndRequest();
  } else {
    for (const auto& handler_record : query_handlers) {
      DispatchQuery(handler_record);
    }

    async::PostDelayedTask(async_get_default_dispatcher(),
                           [w = weak_ptr_factory_.GetWeakPtr()] {
                             if (w) {
                               w->TimeOut();
                             }
                           },
                           kQueryTimeout);
  }
}

void QueryRunner::SetEndRequestCallback(std::function<void()> callback) {
  on_end_request_callback_ = std::move(callback);
}

void QueryRunner::SetResponseCallback(
    std::function<void(std::string, fuchsia::modular::QueryResponse)>
        callback) {
  on_query_response_callback_ = std::move(callback);
}

void QueryRunner::DispatchQuery(const QueryHandlerRecord& handler_record) {
  FXL_DCHECK(!request_ended_);

  outstanding_handlers_.insert(handler_record.url);

  handler_record.handler->OnQuery(
      input_, [request = std::make_shared<HandlerRequest>(
                   weak_ptr_factory_.GetWeakPtr(), handler_record.url)](
                  fuchsia::modular::QueryResponse response) {
        request->Complete(std::move(response));
      });
}

void QueryRunner::EndRequest() {
  FXL_DCHECK(!request_ended_);
  listener_->OnQueryComplete();
  on_end_request_callback_();
  weak_ptr_factory_.InvalidateWeakPtrs();
  request_ended_ = true;
}

void QueryRunner::TimeOut() {
  if (!outstanding_handlers_.empty()) {
    FXL_LOG(INFO) << "Query timeout. Still awaiting results from:";
    for (const std::string& handler_url : outstanding_handlers_) {
      FXL_LOG(INFO) << "    " << handler_url;
    }
    EndRequest();
  }
}

}  // namespace modular
