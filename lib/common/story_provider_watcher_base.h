// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_COMMON_STORY_PROVIDER_WATCHER_BASE_
#define APPS_MODULAR_LIB_COMMON_STORY_PROVIDER_WATCHER_BASE_

#include "apps/modular/services/story/story_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace modular {

// A simple story provider watcher implementation that calls the Continue()
// callback whenever OnChange() is received. This default implementation ignores
// OnDelete(), but you can override this class to handle OnDelete().
class StoryProviderWatcherBase : modular::StoryProviderWatcher {
 public:
  StoryProviderWatcherBase();
  ~StoryProviderWatcherBase() override;

  // Sets the callback that will be called whenever OnChange is received.
  // Derived classes can change this behavior by overriding OnChange() and
  // making the callback based on the desired criteria.
  void Continue(std::function<void()> at);

  // Registers itself a watcher on the given story provider. Only one story
  // provider can be watched at a time.
  void Watch(modular::StoryProviderPtr* story_provider);

  // Deregisters itself from the watched story provider.
  void Reset();

 protected:
  std::function<void()> continue_;

 private:
  // |StoryProviderWatcher|
  void OnDelete(const ::fidl::String& story_id) override;

  // |StoryProviderWatcher|
  void OnChange(modular::StoryInfoPtr story_info,
                modular::StoryState story_state) override;

  fidl::Binding<modular::StoryProviderWatcher> binding_;
  FXL_DISALLOW_COPY_AND_ASSIGN(StoryProviderWatcherBase);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_COMMON_STORY_PROVIDER_WATCHER_BASE_
