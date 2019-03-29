// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_MODEL_STORY_OBSERVER_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_MODEL_STORY_OBSERVER_H_

#include <fuchsia/modular/storymodel/cpp/fidl.h>
#include <src/lib/fxl/macros.h>

namespace modular {

// Implemented bv StoryModelOwner. A client of StoryObserver can query the
// current state of a StoryModel or register a callback to be notified of
// changes.
class StoryObserver {
 public:
  StoryObserver() = default;
  virtual ~StoryObserver() = default;

  // Registers |listener| as a callback for StoryModel updates. If |this| is
  // destroyed, or the StoryModelOwner that created |this| is destroyed, all
  // registered listeners will also be destroyed.
  //
  // Clients may learn that the StoryModelOwner is destroyed by deferring
  // a callback in the capture list of |listener|. Example:
  //
  //   auto on_abandoned = [] { /* do some cleanup */ };
  //   observer.RegisterListener(
  //       [on_destroy = fit::defer(on_abandoned)]
  //       (const StoryModel& model) {
  //         /* do something with the update */
  //       });
  //
  // Note that the received StoryModel is only valid for the lifetime
  // of the call to |listener|.
  virtual void RegisterListener(
      fit::function<void(const fuchsia::modular::storymodel::StoryModel& model)>
          listener) = 0;

  // Returns the current state of the StoryModel.
  virtual const fuchsia::modular::storymodel::StoryModel& model() = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(StoryObserver);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_MODEL_STORY_OBSERVER_H_
