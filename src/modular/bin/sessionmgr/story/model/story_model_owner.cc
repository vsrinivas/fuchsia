// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story/model/story_model_owner.h"

#include <lib/fit/bridge.h>
#include <lib/fit/defer.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/bin/sessionmgr/story/model/apply_mutations.h"
#include "src/modular/bin/sessionmgr/story/model/story_model_storage.h"
#include "src/modular/bin/sessionmgr/story/model/story_mutator.h"

using fuchsia::modular::storymodel::ModuleModel;
using fuchsia::modular::storymodel::StoryModel;
using fuchsia::modular::storymodel::StoryModelMutation;

namespace modular {

namespace {
// Sets default values for all fields of a new StoryModel. Defaults are
// documented in
// src/modular/lib/fidl/public/fuchsia.modular.storymodel/story_model.fidl.
void InitializeModelDefaults(StoryModel* model) {
  model->set_runtime_state(fuchsia::modular::StoryState::STOPPED);
  model->set_visibility_state(fuchsia::modular::StoryVisibilityState::DEFAULT);
  model->set_modules({});
}
}  // namespace

// Delegates Execute() to the StoryModelOwner.
class StoryModelOwner::Mutator : public StoryMutator {
 public:
  Mutator(fxl::WeakPtr<StoryModelOwner> weak_owner) : weak_owner_(weak_owner) {}
  ~Mutator() override = default;

 private:
  // |StoryMutator|
  fit::consumer<> ExecuteInternal(std::vector<StoryModelMutation> commands) override {
    if (!weak_owner_) {
      fit::bridge<> bridge;
      bridge.completer.complete_error();
      return std::move(bridge.consumer);
    }
    return weak_owner_->ExecuteCommands(std::move(commands));
  }

  fxl::WeakPtr<StoryModelOwner> weak_owner_;
};

// Manages the lifecycle of multiple listener callbacks. When Observer dies,
// all callbacks registered with RegisterListener() are unregistered from the
// backing StoryModelOwner.
class StoryModelOwner::Observer : public StoryObserver {
 public:
  Observer(fxl::WeakPtr<StoryModelOwner> weak_owner) : weak_owner_(weak_owner) {}
  ~Observer() {
    // If our owner is gone, all of the listener functions have already been
    // cleaned up. We need to cancel all the deferred actions since they
    // capture and make a call on our owner.
    if (!weak_owner_) {
      for (auto& i : deferred_cleanup_) {
        i.cancel();
      }
    }
  }

 private:
  void RegisterListener(fit::function<void(const StoryModel&)> listener) override {
    if (!weak_owner_) {
      return;
      // |listener| is destroyed.
    }

    deferred_cleanup_.push_back(weak_owner_->RegisterListener(std::move(listener)));
  }

  const StoryModel& model() override {
    FX_CHECK(weak_owner_);
    return weak_owner_->model_;
  }

  fxl::WeakPtr<StoryModelOwner> weak_owner_;
  // When we are destroyed, we want to clean up any listeners we've added to
  // |shared_state_->owner|.
  std::vector<fit::deferred_action<fit::function<void()>>> deferred_cleanup_;
};

StoryModelOwner::StoryModelOwner(const std::string& story_name, fit::executor* executor,
                                 std::unique_ptr<StoryModelStorage> model_storage)
    : model_storage_(std::move(model_storage)), weak_ptr_factory_(this), executor_(executor) {
  FX_CHECK(model_storage_ != nullptr);
  model_.mutable_name()->assign(story_name);
  InitializeModelDefaults(&model_);
  model_storage_->SetObserveCallback([this](std::vector<StoryModelMutation> commands) {
    HandleObservedMutations(std::move(commands));
  });
}

StoryModelOwner::~StoryModelOwner() = default;

std::unique_ptr<StoryMutator> StoryModelOwner::NewMutator() {
  return std::make_unique<Mutator>(weak_ptr_factory_.GetWeakPtr());
}

std::unique_ptr<StoryObserver> StoryModelOwner::NewObserver() {
  return std::make_unique<Observer>(weak_ptr_factory_.GetWeakPtr());
}

void StoryModelOwner::LoadStorage() {
  FX_CHECK(!seen_any_requests_to_execute_)
      << "Must call LoadStorage() before any calls to StoryMutator.Execute();";
  executor_->schedule_task(model_storage_->Load());
}

fit::consumer<> StoryModelOwner::FlushStorage() {
  fit::bridge<> bridge;
  executor_->schedule_task(model_storage_->Flush().then(
      [completer = std::move(bridge.completer)](fit::result<>& result) mutable {
        if (result.is_ok()) {
          completer.complete_ok();
        } else {
          completer.complete_error();
        }
      }));
  return std::move(bridge.consumer);
}

fit::deferred_action<fit::function<void()>> StoryModelOwner::RegisterListener(
    fit::function<void(const StoryModel&)> listener) {
  auto it = listeners_.insert(listeners_.end(), std::move(listener));
  return fit::defer(fit::function<void()>([this, it] { listeners_.erase(it); }));
}

fit::consumer<> StoryModelOwner::ExecuteCommands(std::vector<StoryModelMutation> commands) {
  seen_any_requests_to_execute_ = true;
  // fit::bridge allows this function to return (and eventually complete) a
  // promise that is owned by the caller and still schedule a promise as a task
  // to execute the model update locally.
  //
  // If the caller chooses to ignore the result, our local promise will still be
  // scheduled and executed.
  fit::bridge<> bridge;
  auto promise =
      model_storage_->Execute(std::move(commands))
          .then([completer = std::move(bridge.completer)](fit::result<>& result) mutable {
            if (result.is_ok()) {
              completer.complete_ok();
            } else {
              completer.complete_error();
            }
          });

  executor_->schedule_task(std::move(promise));
  return std::move(bridge.consumer);
}

void StoryModelOwner::HandleObservedMutations(std::vector<StoryModelMutation> commands) {
  // This is not thread-safe. We rely on the fact that
  // HandleObservedMutations() will only be called on a single thread.
  StoryModel old_model;
  FX_CHECK(fidl::Clone(model_, &old_model) == ZX_OK);
  model_ = ApplyMutations(old_model, std::move(commands));

  // Don't notify anyone if the model didn't change.
  if (fidl::Equals(model_, old_model)) {
    return;
  }

  executor_->schedule_task(fit::make_promise([this] {
                             for (auto& listener : listeners_) {
                               listener(model_);
                             }
                             return fit::ok();
                           }).wrap_with(scope_));
}

}  // namespace modular
