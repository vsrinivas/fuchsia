// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/maxwell/src/suggestion_engine/ask_channel.h"
#include "apps/maxwell/src/suggestion_engine/filter.h"
#include "apps/maxwell/src/suggestion_engine/next_channel.h"
#include "apps/maxwell/src/suggestion_engine/proposal_record.h"
#include "apps/maxwell/src/suggestion_engine/suggestion_agent_client_impl.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"

namespace maxwell {
namespace suggestion {

class Repo {
 public:
  Repo(ProposalRecordFilter filter) : filter_(filter) {}

  SuggestionAgentClientImpl* GetOrCreateSourceClient(
      const std::string& component_url);

  void RemoveSourceClient(const std::string& component_url) {
    sources_.erase(component_url);
  }

  void AddSuggestion(std::unique_ptr<ProposalRecord> proposal,
                     AgentSuggestionRecord* agent_suggestion_record);

  void RemoveSuggestion(const std::string& id) { suggestions_.erase(id); }

  void SubscribeToNext(fidl::InterfaceHandle<Listener> listener,
                       fidl::InterfaceRequest<NextController> controller) {
    next_channel_.AddSubscriber(std::make_unique<NextSubscriber>(
        next_channel_.ranked_suggestions(), std::move(listener),
        std::move(controller)));
  }

  void InitiateAsk(fidl::InterfaceHandle<Listener> listener,
                   fidl::InterfaceRequest<AskController> controller);

  void AddAskHandler(fidl::InterfaceHandle<AskHandler> ask_handler) {
    ask_handlers_.AddInterfacePtr(
        AskHandlerPtr::Create(std::move(ask_handler)));
  }

  void DispatchAsk(UserInputPtr query) {
    ask_handlers_.ForAllPtrs([&query](AskHandler* handler) {
      handler->Ask(query.Clone(), [](fidl::Array<ProposalPtr> proposals) {
        // TODO(rosswang)
      });
    });
  }

  // Non-mutating indexer; returns NULL if no such suggestion exists.
  const ProposalRecord* operator[](const std::string& suggestion_id) const {
    auto it = suggestions_.find(suggestion_id);
    return it == suggestions_.end() ? NULL : it->second.get();
  }

 private:
  std::string RandomUuid() {
    static uint64_t id = 0;
    // TODO(rosswang): real UUIDs
    return std::to_string(id++);
  }

  std::unordered_map<std::string, std::unique_ptr<SuggestionAgentClientImpl>>
      sources_;
  // indexed by suggestion ID
  std::unordered_map<std::string, ProposalRecordPtr> suggestions_;
  NextChannel next_channel_;
  maxwell::BoundNonMovableSet<AskChannel> ask_channels_;
  fidl::InterfacePtrSet<AskHandler> ask_handlers_;

  ProposalRecordFilter filter_;
};

}  // namespace suggestion
}  // namespace maxwell
