// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/mozart/session.h"

namespace mz {

Session::Session(Mozart* owner,
                 SessionId id,
                 ::fidl::InterfaceHandle<ui_mozart::SessionListener> listener)
    : mozart_(owner), id_(id), listener_(listener.Bind()), weak_factory_(this) {
  FXL_DCHECK(mozart_);
}

Session::~Session() = default;

void Session::Enqueue(::fidl::Array<ui_mozart::CommandPtr> cmds) {
  FXL_DCHECK(false) << "unimplemented.";
}

void Session::Present() {
  FXL_DCHECK(false) << "unimplemented.";
}

void Session::SetCommandDispatchers(
    std::array<std::unique_ptr<CommandDispatcher>, System::TypeId::kMaxSystems>
        dispatchers) {
  for (size_t i = 0; i < System::TypeId::kMaxSystems; ++i) {
    dispatchers_[i] = std::move(dispatchers[i]);
  }
}

bool Session::ApplyCommand(const ui_mozart::CommandPtr& command) {
  System::TypeId system_type = System::TypeId::kMaxSystems;  // invalid
  switch (command->which()) {
    case ui_mozart::Command::Tag::SCENIC:
      system_type = System::TypeId::kScenic;
      break;
    case ui_mozart::Command::Tag::DUMMY:
      system_type = System::TypeId::kDummySystem;
      break;
    case ui_mozart::Command::Tag::__UNKNOWN__:
      // TODO: use ErrorHandler
      return false;
  }
  if (auto& dispatcher = dispatchers_[system_type]) {
    return dispatcher->ApplyCommand(command);
  } else {
    // TODO: use ErrorHandler.
    return false;
  }
}

}  // namespace mz
