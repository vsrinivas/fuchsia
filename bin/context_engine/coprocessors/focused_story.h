// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <unordered_map>

#include "apps/maxwell/src/context_engine/context_repository.h"

namespace maxwell {

// Copies all Story-scoped context topics and values from the currently focused
// Story (specified in /story/focused_id) from /story/id/<focused_id>/* to
// /story/focused.
//
// This Coprocessor should be the last in the list of Coprocessors to ensure it
// catches all relevant changes to topics.
class FocusedStoryCoprocessor : public ContextCoprocessor {
 public:
  FocusedStoryCoprocessor();
  ~FocusedStoryCoprocessor() override;

  void ProcessTopicUpdate(const ContextRepository* repository,
                          const std::set<std::string>& topics_updated,
                          std::map<std::string, std::string>* out) override;

 private:
  std::string current_focused_id_;
};

}  // namespace maxwell
