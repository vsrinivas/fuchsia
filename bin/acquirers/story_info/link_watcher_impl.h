// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_
#define APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/modular/services/module/module_data.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/story/story_controller.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"

namespace maxwell {

class StoryWatcherImpl;

class LinkWatcherImpl : modular::LinkWatcher {
 public:
  LinkWatcherImpl(StoryWatcherImpl* const owner,
                  modular::StoryController* const story_controller,
                  ContextPublisher* const publisher,
                  const std::string& story_id,
                  const modular::LinkPathPtr& link_path);

  ~LinkWatcherImpl() override;

 private:
  // |LinkWatcher|
  void Notify(const fidl::String& json) override;

  void ProcessContext(const fidl::String& value);

  StoryWatcherImpl* const owner_;
  modular::StoryController* const story_controller_;
  ContextPublisher* const publisher_;

  const std::string story_id_;
  const modular::LinkPathPtr link_path_;

  // TODO(mesch): We would like to not retain a Link connection here, because
  // that keeps the Link instance in memory because it never becomes unused. The
  // Link instance is still destroyed when the Story is destroyed, so it's not a
  // leak, and we'll revisit this. This is perhaps yet another argument to not
  // watch Links directly through an actual Link connection, but expose them
  // through StoryProvider or StoryController instead.
  modular::LinkPtr link_;
  fidl::Binding<modular::LinkWatcher> link_watcher_binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherImpl);
};

}  // namespace maxwell

#endif  // APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_
