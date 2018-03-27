// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_
#define PERIDOT_BIN_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include <fuchsia/cpp/modular.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"

namespace maxwell {

class StoryWatcherImpl;

class LinkWatcherImpl : modular::LinkWatcher {
 public:
  LinkWatcherImpl(StoryWatcherImpl* const owner,
                  modular::StoryController* const story_controller,
                  const std::string& story_id,
                  modular::ContextValueWriter* story_value,
                  modular::LinkPath link_path);

  ~LinkWatcherImpl() override;

 private:
  // |LinkWatcher|
  void Notify(fidl::StringPtr json) override;

  void ProcessNewValue(const fidl::StringPtr& json);
  void MaybeProcessContextLink(const fidl::StringPtr& value);

  StoryWatcherImpl* const owner_;
  modular::StoryController* const story_controller_;

  const std::string story_id_;
  const modular::LinkPath link_path_;

  // Allows us to write the initial Link node in the Context engine, and then
  // create child nodes for each Entity we see in the Link.
  modular::ContextValueWriterPtr link_node_writer_;

  // When applicable: Per top-level JSON member key in the Link value, a value
  // writer that allows us to store the contained Entity.
  //
  // See the documentation in ProcessNewValue() for more details.
  std::map<std::string, modular::ContextValueWriterPtr> entity_node_writers_;
  // TODO(thatguy): When Bundles come online, remove |entity_values_| in favor
  // of this. Rename to |entity_value_|.
  modular::ContextValueWriterPtr single_entity_node_writer_;

  // Per context link topic, the context value.
  // TODO(thatguy): Deprecate this usage in favor of Links.
  std::map<fidl::StringPtr, modular::ContextValueWriterPtr> topic_node_writers_;

  fidl::Binding<modular::LinkWatcher> link_watcher_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherImpl);
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_
