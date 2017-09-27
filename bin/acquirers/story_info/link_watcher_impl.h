// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_
#define PERIDOT_BIN_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include "lib/context/fidl/context_writer.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/module/fidl/module_data.fidl.h"
#include "lib/story/fidl/link.fidl.h"
#include "lib/story/fidl/story_controller.fidl.h"

namespace maxwell {

class StoryWatcherImpl;

class LinkWatcherImpl : modular::LinkWatcher {
 public:
  LinkWatcherImpl(StoryWatcherImpl* const owner,
                  modular::StoryController* const story_controller,
                  const std::string& story_id,
                  ContextValueWriter* story_value,
                  const modular::LinkPathPtr& link_path);

  ~LinkWatcherImpl() override;

 private:
  // |LinkWatcher|
  void Notify(const fidl::String& json) override;

  void ProcessContext(const fidl::String& value);

  StoryWatcherImpl* const owner_;
  modular::StoryController* const story_controller_;

  const std::string story_id_;
  ContextValueWriter* const story_value_;
  const modular::LinkPathPtr link_path_;

  // Per context link topic, the context value.
  std::map<fidl::String, ContextValueWriterPtr> values_;

  fidl::Binding<modular::LinkWatcher> link_watcher_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherImpl);
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_
