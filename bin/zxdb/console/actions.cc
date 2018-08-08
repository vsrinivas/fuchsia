// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/actions.h"
#include "garnet/bin/zxdb/console/flags.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

using Option = fxl::CommandLine::Option;

void PrintCommandFeedback(size_t index, const char* name) {
  printf("\nRunning command %lu: \"%s\"\n", index, name);
  printf("---------------------------------------------------------------\n");
}

}  // namespace

// Action ----------------------------------------------------------------------

Action::Action() = default;
Action::Action(std::string name, Action::ActionFunction action)
    : name_(name), action_(action) {}

Action::Action(Action&&) = default;
Action& Action::operator=(Action&& other) {
  this->name_ = std::move(other.name_);
  this->action_ = std::move(other.action_);
  return *this;
}

void Action::operator()(const Session& session, Console* console) const {
  // The next_action_ chaining will take care of calling the following command
  // when the time is due.
  action_(*this, session, console);
}

// ActionFlow ------------------------------------------------------------------

void ActionFlow::ScheduleActions(std::vector<Action>&& actions,
                                 const Session* session, Console* console,
                                 Callback callback) {
  // If there are no actions, we schedule the callback.
  if (actions.empty()) {
    callback_(Err());
    return;
  }

  // We store the paramaters as they will be used in the future.
  flow_ = std::move(actions);
  session_ = session;
  console_ = console;
  callback_ = std::move(callback);

  // We schedule the first action to run
  debug_ipc::MessageLoop::Current()->PostTask([&]() {
    const auto& action = flow_.front();
    PrintCommandFeedback(1lu, action.name().c_str());
    action(*session_, console_);
  });
}

ActionFlow& ActionFlow::Singleton() {
  // We use the global callback function so that the user doesn't have to
  // worry about tracking an instance.
  static ActionFlow flow;
  return flow;
}

ActionFlow::ActionFlow() = default;

void ActionFlow::PostActionCallback(Err err) {
  ActionFlow& flow = ActionFlow::Singleton();
  // We log the callback
  flow.callbacks_.push_back(err);

  // If the command wants us to stop processing, we call the complete callback.
  if ((err.type() == ErrType::kCanceled) || err.has_error()) {
    flow.callback_(err);
    return;
  }

  flow.current_action_index_++;
  // In no more actions available, communicate to caller.
  if (flow.current_action_index_ >= flow.flow_.size()) {
    flow.callback_(Err());
    return;
  }

  // Schedule the next action.
  const auto& next_action = flow.current_action();
  debug_ipc::MessageLoop::Current()->PostTask([&]() {
    PrintCommandFeedback(flow.current_action_index_ + 1,
                         next_action.name().c_str());
    next_action(*flow.session_, flow.console_);
  });
}

void ActionFlow::Clear() {
  flow_.clear();
  current_action_index_ = 0;
  session_ = nullptr;
  console_ = nullptr;
  callback_ = nullptr;
  callbacks_.clear();
}

}  // namespace zxdb
