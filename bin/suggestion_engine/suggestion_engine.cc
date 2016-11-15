// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/suggestion_engine.h"

#include "lib/mtl/tasks/message_loop.h"

namespace maxwell {
namespace suggestion {

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

}  // suggestion
}  // maxwell

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  maxwell::suggestion::SuggestionEngineApp app;
  loop.Run();
  return 0;
}
