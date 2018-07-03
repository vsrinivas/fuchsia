// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_QUERY_RUNNER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_QUERY_RUNNER_H_

#include <set>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fxl/memory/weak_ptr.h>

namespace modular {

struct QueryHandlerRecord;

// QueryRunner is in charge of executing a query and interacting with the query
// handlers, making sure all of them return or timeout if the query takes too
// long to resolve. Through its callbacks it notifies when a query response
// arrives and when all handlers finish processing the query or it times out.
class QueryRunner {
 public:
  QueryRunner(fidl::InterfaceHandle<fuchsia::modular::QueryListener> listener,
              fuchsia::modular::UserInput input, int count);
  ~QueryRunner();

  // Starts running a query notifying the provided handlers and processes their
  // responses.
  void Run(const std::vector<QueryHandlerRecord>& query_handlers);

  // Sets a callback that will be executed when a query request ends.
  void SetEndRequestCallback(std::function<void()> callback);

  // Sets a callback that will be executed when a response for the query in
  // execution is received.
  void SetResponseCallback(
      std::function<void(std::string, fuchsia::modular::QueryResponse)>
          callback);

  fuchsia::modular::QueryListener* listener() const { return listener_.get(); }
  size_t max_results() const { return max_results_; }

 private:
  void DispatchQuery(const QueryHandlerRecord& handler);

  void HandlerCallback(const std::string& handler_url,
                       fuchsia::modular::QueryResponse response);

  void TimeOut();

  void EndRequest();

  fuchsia::modular::QueryListenerPtr listener_;
  const fuchsia::modular::UserInput input_;
  const size_t max_results_;
  bool request_ended_;
  fxl::WeakPtrFactory<QueryRunner> weak_ptr_factory_;

  std::function<void(std::string, fuchsia::modular::QueryResponse)>
      on_query_response_callback_;
  std::function<void()> on_end_request_callback_;

  std::multiset<std::string> outstanding_handlers_;
};

struct QueryHandlerRecord {
  QueryHandlerRecord(fuchsia::modular::QueryHandlerPtr handler, std::string url)
      : handler(std::move(handler)), url(std::move(url)) {}

  fuchsia::modular::QueryHandlerPtr handler;
  std::string url;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_QUERY_RUNNER_H_
