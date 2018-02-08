// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_MOZART_SESSION_H_
#define GARNET_LIB_UI_MOZART_SESSION_H_

#include <array>
#include <memory>

#include "garnet/lib/ui/mozart/system.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/ui/mozart/fidl/session.fidl.h"

namespace mz {

class CommandDispatcher;
class Mozart;

using SessionId = uint64_t;

class Session final : public ui_mozart::Session {
 public:
  Session(Mozart* owner,
          SessionId id,
          ::fidl::InterfaceHandle<ui_mozart::SessionListener> listener);
  ~Session() override;

  void SetCommandDispatchers(
      std::array<std::unique_ptr<CommandDispatcher>,
                 System::TypeId::kMaxSystems> dispatchers);

  bool ApplyCommand(const ui_mozart::CommandPtr& command);

  // |ui_mozart::Session|
  void Enqueue(::fidl::Array<ui_mozart::CommandPtr> cmds) override;

  // |ui_mozart::Session|
  void Present() override;

  SessionId id() const { return id_; }

 private:
  Mozart* const mozart_;
  const SessionId id_;
  ::fidl::InterfacePtr<ui_mozart::SessionListener> listener_;

  std::array<std::unique_ptr<CommandDispatcher>, System::TypeId::kMaxSystems>
      dispatchers_;

  fxl::WeakPtrFactory<Session> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Session);
};

}  // namespace mz

#endif  // GARNET_LIB_UI_MOZART_SESSION_H_
