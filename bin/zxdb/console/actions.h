// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <functional>
#include <limits>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/public/lib/fxl/command_line.h"

#pragma once

namespace zxdb {

class Session;

// The flag processing will generate actions that will be run after the flag
// processing. a global ActionFlow
// is used and PostActionCallback is used as the overall callback.
// See PostActionCallback below for details.
class Action {
 public:
  // The functor to be called for each action
  using ActionFunction = std::function<void(const Action&, const Session&,
                                            Console*)>;

  Action();
  explicit Action(std::string name, ActionFunction action);
  // Movable
  Action(Action&&);
  Action& operator=(Action&&);

  // Action is a functor that receives the session and console state to run
  // commands. They also receive an option test callback that can be used to
  // modify behaviour when running on tests.
  void operator()(const Session&, Console*) const;

  const std::string& name() const { return name_; }

 private:
  std::string name_;  // For debug and error purposes
  ActionFunction action_;
};


// Owner of generated actions processed by command line. It will keep the
// actions sorted by priority,
class ActionFlow {
 public:

  // The callback that will be called on complete or error of a particular
  // action.
  using Callback = std::function<void(Err)>;

  // This singleton is the one that should be used for running Actions outside
  // of a testing environment. It will hook up the correct callback.
  static ActionFlow& Singleton();
  ActionFlow();

  // Will schedule the processed actions into the MessageLoop, linking them with
  // the correct callback to get the flow connected. The given callbacks are the
  // way the scheduling uses to run post action events. They must be set.
  // The given callback will be called with the result of the actions. If the
  // Err has ErrType::kCanceled, it means that a command wants to stop the
  // action processing and the caller might want to react accordingly.
  void ScheduleActions(std::vector<Action>&& actions, const Session*, Console*,
                       Callback);

  // This function is the one that ties all the Actions together. Each of
  // generated flag actions will run this function as their callback. This
  // function obtains a reference to the ActionFlow singleton and is able
  // to determine which action to run next. If no action is left or the current
  // once failed, the console will be initiated and interactive mode will be run.
  // Interactive mode will also run if any action called a command that does not
  // receive a callback (eg. DoStep).
  // The calling action also provides information about whether the console
  // should continue processing the actions. This is different than a failure:
  // eg. help will stop processing anything else, but it has not failed.
  // This is indicated by ErrType::kCanceled
  static void PostActionCallback(Err);

  // Useful for tests, that require a clean slate everytime
  void Clear();

  const std::vector<Action>& flow() const { return flow_; }
  const Action& current_action() const { return flow_[current_action_index_]; }
  const std::vector<Err>& callbacks() const {
    return callbacks_;
  }

 private:
  std::vector<Action> flow_;
  size_t current_action_index_ = 0;
  const Session* session_;
  Console* console_;

  Callback callback_;

  // Useful for test verification
  std::vector<Err> callbacks_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ActionFlow);
};

}   // namespace zxdb
