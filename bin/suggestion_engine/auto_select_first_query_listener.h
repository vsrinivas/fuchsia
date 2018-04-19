// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_AUTO_SELECT_FIRST_QUERY_LISTENER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_AUTO_SELECT_FIRST_QUERY_LISTENER_H_

#include <fuchsia/cpp/modular.h>

namespace modular {

class SuggestionEngineImpl;

// This listener is created when performing a QueryAction.
// It saves the last query results received and on query complete it picks the
// first one and selects it.
class AutoSelectFirstQueryListener : public QueryListener {
 public:
  AutoSelectFirstQueryListener(SuggestionEngineImpl* suggestion_engine);

 private:
  // |QueryListener|
  void OnQueryResults(fidl::VectorPtr<Suggestion> suggestions) override;

  // |QueryListener|
  void OnQueryComplete() override;

  fidl::VectorPtr<Suggestion> suggestions_;
  SuggestionEngineImpl* const engine_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_AUTO_SELECT_FIRST_QUERY_LISTENER_H_
