// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_
#define APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include "apps/maxwell/lib/async/future_value.h"
#include "apps/maxwell/services/context/context_writer.fidl.h"
#include "apps/modular/services/module/module_data.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/story/story_controller.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"

namespace maxwell {

class StoryWatcherImpl;

class LinkWatcherImpl : modular::LinkWatcher {
 public:
  LinkWatcherImpl(StoryWatcherImpl* const owner,
                  modular::StoryController* const story_controller,
                  ContextWriter* const writer,
                  const std::string& story_id,
                  const fidl::String& parent_value_id,
                  const modular::LinkPathPtr& link_path);

  ~LinkWatcherImpl() override;

 private:
  // |LinkWatcher|
  void Notify(const fidl::String& json) override;

  void ProcessContext(const fidl::String& value);

  StoryWatcherImpl* const owner_;
  modular::StoryController* const story_controller_;
  ContextWriter* const writer_;

  const std::string story_id_;
  const fidl::String parent_value_id_;
  const modular::LinkPathPtr link_path_;

  // Per context link topic, the context value id.
  std::map<fidl::String, FutureValue<fidl::String>> value_ids_;

  fidl::Binding<modular::LinkWatcher> link_watcher_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherImpl);
};

}  // namespace maxwell

#endif  // APPS_MAXWELL_SRC_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_
