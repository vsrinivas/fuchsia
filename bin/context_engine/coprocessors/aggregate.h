// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <unordered_map>

#include "apps/maxwell/src/context_engine/context_repository.h"

namespace maxwell {

class AggregateCoprocessor : public ContextCoprocessor {
 public:
  AggregateCoprocessor(const std::string& topic);
  ~AggregateCoprocessor() override;

  // Recieves a |repository| which represents current state, as well as a list
  // of |topics_updated| so far. The Coprocessor can optionally populate |out|
  // with additional topics and values to update.
  void ProcessTopicUpdate(const ContextRepository* repository,
                          const std::set<std::string>& topics_updated,
                          std::map<std::string, std::string>* out) override;

 private:
  const std::string topic_to_aggregate_;
};

}  // namespace maxwell
