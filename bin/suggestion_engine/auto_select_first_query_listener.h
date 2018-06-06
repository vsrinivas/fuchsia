// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_AUTO_SELECT_FIRST_QUERY_LISTENER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_AUTO_SELECT_FIRST_QUERY_LISTENER_H_

#include <fuchsia/modular/cpp/fidl.h>

namespace modular {

class SuggestionEngineImpl;

// This listener is created when performing a fuchsia::modular::QueryAction.
// It saves the last query results received and on query complete it picks the
// first one and selects it.
class AutoSelectFirstQueryListener : public fuchsia::modular::QueryListener {
 public:
  AutoSelectFirstQueryListener(SuggestionEngineImpl* suggestion_engine);

 private:
  // |fuchsia::modular::QueryListener|
  void OnQueryResults(
      fidl::VectorPtr<fuchsia::modular::Suggestion> suggestions) override;

  // |fuchsia::modular::QueryListener|
  void OnQueryComplete() override;

  fidl::VectorPtr<fuchsia::modular::Suggestion> suggestions_;
  SuggestionEngineImpl* const engine_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_AUTO_SELECT_FIRST_QUERY_LISTENER_H_
