// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_MODEL_STORY_MODEL_OWNER_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_MODEL_STORY_MODEL_OWNER_H_

#include <fuchsia/modular/storymodel/cpp/fidl.h>
#include <lib/async_promise/executor.h>
#include <lib/fit/defer.h>
#include <lib/fit/scope.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/memory/weak_ptr.h>

#include <list>
#include <memory>

#include "peridot/bin/sessionmgr/story/model/story_mutator.h"
#include "peridot/bin/sessionmgr/story/model/story_observer.h"

namespace modular {

class StoryModelStorage;

// Owns a single instance of StoryModel and manages the flow of control from a
// stream of mutations to observers.
//
// Clients do not depend on or have any direct knowledge of StoryModelOwner.
// Rather, they depend on either or both a StoryObserver and
// StoryMutator, depending on if they need to observe or mutate the model.
//
// See README.md.
//
// This class is not thread-safe.
class StoryModelOwner {
 public:
  // |story_name| is applied to StoryModel.name.
  //
  // Uses |executor| to schedule internal mutation tasks. Delegates mutation
  // commands to and reacts to observation of applied mutations from
  // |model_storage|.
  explicit StoryModelOwner(const std::string& story_name,
                           fit::executor* executor,
                           std::unique_ptr<StoryModelStorage> model_storage);
  ~StoryModelOwner();

  // Instructs |model_storage| to load persisted data. After calling, clients
  // may begin to see notifications through any created StoryObservers.
  //
  // This method must be called no more than once and before any associated
  // calls to StoryMutator.Execute().
  void LoadStorage();

  // Returns a consumer which is completed when |model_storage| has
  // successfully executed any pending tasks to mutate its underlying storage.
  // Note this does not indicate that the storage layer has notifed |this| of
  // the application of those mutations.
  //
  // To avoid losing any pending changes to the model, it is recommended to
  // call Flush() and wait for completion before destroying |this|.
  fit::consumer<> FlushStorage();

  // Returns a mutator object that can be provided to a System that requires
  // the ability to issue commands to mutate the model. This can be provided to
  // Systems as a constructor argument.
  //
  // The returned StoryMutator may outlive |this|, but will return an
  // error for all attempts to mutate the model.
  std::unique_ptr<StoryMutator> NewMutator();

  // Returns an object that can be used to register observer callbacks to the
  // be notified of the model's current state when changes are made. This
  // should be provided to Systems as a constructor argument.
  //
  // The returned StoryObserver may outlive |this|. See the documentation
  // for StoryObserver for caveats.
  std::unique_ptr<StoryObserver> NewObserver();

 private:
  class Mutator;
  class Observer;

  // Registers |listener| to be called whenever mutation commands are applied
  // to |model_|. Returns a deferred action that will deregister |listener|
  // when it goes out of scope.
  //
  // Called by instances of Observer.
  fit::deferred_action<fit::function<void()>> RegisterListener(
      fit::function<void(const fuchsia::modular::storymodel::StoryModel&)>
          listener);

  // Calls |model_storage_| to execute |commands|.
  //
  // Called by instances of Mutator.
  fit::consumer<> ExecuteCommands(
      std::vector<fuchsia::modular::storymodel::StoryModelMutation> commands);

  // Applies |commands| to |model_| and notifies all |listeners_| with the
  // updated StoryModel.
  //
  // Called indirectly by |model_storage_| through a callback.
  void HandleObservedMutations(
      std::vector<fuchsia::modular::storymodel::StoryModelMutation> commands);

  // Set to true on the first call to ExecuteCommands().
  bool seen_any_requests_to_execute_{false};

  std::unique_ptr<StoryModelStorage> model_storage_;

  // The most recent StoryModel value. Accessed by StoryObservers at any
  // time. Updated by HandleObservedMutations().
  fuchsia::modular::storymodel::StoryModel model_;

  // Used to signal to instances of StoryMutator/Observer when |this| is
  // destroyed.
  fxl::WeakPtrFactory<StoryModelOwner> weak_ptr_factory_;

  // A list<> so that we can get stable iterators for cleanup purposes. See
  // RegisterListener().
  std::list<
      fit::function<void(const fuchsia::modular::storymodel::StoryModel&)>>
      listeners_;

  fit::executor* executor_;  // Not owned.

  // Since we schedule our fit::promises for execution on |executor_|, which can
  // outlive |this|, we use this to wrap our fit::promises (using
  // fit::promise.wrap_with(scope_)) such that when |this| is destroyed, all
  // pending promises are abandoned.
  fit::scope scope_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryModelOwner);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_MODEL_STORY_MODEL_OWNER_H_
