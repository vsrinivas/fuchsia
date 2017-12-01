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

  void ProcessNewValue(const fidl::String& json);
  void MaybeProcessContextLink(const fidl::String& value);

  StoryWatcherImpl* const owner_;
  modular::StoryController* const story_controller_;

  const std::string story_id_;
  const modular::LinkPathPtr link_path_;

  // Allows us to write the initial Link node in the Context engine, and then
  // create child nodes for each Entity we see in the Link.
  ContextValueWriterPtr link_node_writer_;

  // When applicable: Per top-level JSON member key in the Link value, a value
  // writer that allows us to store the contained Entity.
  //
  // See the documentation in ProcessNewValue() for more details.
  std::map<std::string, ContextValueWriterPtr> entity_node_writers_;
  // TODO(thatguy): When Bundles come online, remove |entity_values_| in favor
  // of this. Rename to |entity_value_|.
  ContextValueWriterPtr single_entity_node_writer_;

  // Per context link topic, the context value.
  // TODO(thatguy): Deprecate this usage in favor of Links.
  std::map<fidl::String, ContextValueWriterPtr> topic_node_writers_;

  fidl::Binding<modular::LinkWatcher> link_watcher_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherImpl);
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_ACQUIRERS_STORY_INFO_LINK_WATCHER_IMPL_H_
