// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_QUERY_PROCESSOR_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_QUERY_PROCESSOR_H_

#include <set>

#include "lib/fxl/memory/weak_ptr.h"
#include "lib/suggestion/fidl/user_input.fidl.h"
#include "peridot/bin/suggestion_engine/query_handler_record.h"
#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

namespace maxwell {

class SuggestionEngineImpl;

class QueryProcessor {
 public:
  QueryProcessor(SuggestionEngineImpl* engine, UserInputPtr input);
  ~QueryProcessor();

 private:
  void DispatchQuery(const QueryHandlerRecord& handler_record);
  void HandlerCallback(const std::string& handler_url,
                       QueryResponsePtr response);
  void EndRequest();
  void TimeOut();

  SuggestionEngineImpl* const engine_;
  const UserInputPtr input_;

  std::multiset<std::string> outstanding_handlers_;
  // When multiple handlers want to play media as part of their responses, we
  // only want to allow one of them to do so. For lack of a better policy, we
  // play the first one we encounter.
  bool has_media_response_;
  bool request_ended_;

  fxl::WeakPtrFactory<QueryProcessor> weak_ptr_factory_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_QUERY_PROCESSOR_H_
