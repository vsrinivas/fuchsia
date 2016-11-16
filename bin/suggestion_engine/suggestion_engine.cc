// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/suggestion_engine.h"

#include "lib/mtl/tasks/message_loop.h"

namespace maxwell {
namespace suggestion {

// SuggestionProvider

void SuggestionEngineApp::SubscribeToInterruptions(
    fidl::InterfaceHandle<Listener> listener) {
  // TODO(rosswang): no interruptions yet
}

void SuggestionEngineApp::SubscribeToNext(
    fidl::InterfaceHandle<Listener> listener,
    fidl::InterfaceRequest<NextController> controller) {
  std::unique_ptr<NextSubscriber> sub(
      new NextSubscriber(&ranked_suggestions_, std::move(listener)));
  sub->Bind(std::move(controller));
  next_subscribers_.emplace(std::move(sub));
}

void SuggestionEngineApp::InitiateAsk(
    fidl::InterfaceHandle<Listener> listener,
    fidl::InterfaceRequest<AskController> controller) {
  // TODO(rosswang): no ask handlers yet
}

void SuggestionEngineApp::NotifyInteraction(const fidl::String& suggestion_uuid,
                                            InteractionPtr interaction) {
  FTL_LOG(INFO) << (interaction->type == InteractionType::SELECTED
                        ? "Accepted"
                        : "Dismissed")
                << " suggestion " << suggestion_uuid << ")";
}

// end SuggestionProvider

// SuggestionEngine

void SuggestionEngineApp::RegisterSuggestionAgent(
    const fidl::String& url,
    fidl::InterfaceRequest<SuggestionAgentClient> client) {
  std::unique_ptr<SuggestionAgentClientImpl>& source = sources_[url];
  if (!source)  // create if it didn't already exist
    source.reset(new SuggestionAgentClientImpl(this, url));

  source->AddBinding(std::move(client));
}

void SuggestionEngineApp::SetStoryProvider(
    fidl::InterfaceHandle<modular::StoryProvider> story_provider) {
  // TODO(rosswang)
}

// end SuggestionEngine

}  // suggestion
}  // maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::suggestion::SuggestionEngineApp app;
  loop.Run();
  return 0;
}
