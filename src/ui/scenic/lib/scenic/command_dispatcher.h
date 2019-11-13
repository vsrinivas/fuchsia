// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_COMMAND_DISPATCHER_H_
#define SRC_UI_SCENIC_LIB_SCENIC_COMMAND_DISPATCHER_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fit/function.h>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/ui/scenic/lib/scenic/forward_declarations.h"

namespace scenic_impl {

using OnFramePresentedCallback =
    fit::function<void(fuchsia::scenic::scheduling::FramePresentedInfo info)>;

// Provides the capabilities that a CommandDispatcher needs to do its job,
// without directly exposing the Session.
class CommandDispatcherContext {
 public:
  explicit CommandDispatcherContext(Scenic* scenic, Session* session);

  explicit CommandDispatcherContext(Scenic* scenic, Session* session, SessionId id);

  CommandDispatcherContext(CommandDispatcherContext&& context);

  // TODO(SCN-808): can/should we avoid exposing any/all of these?
  Session* session() {
    FXL_DCHECK(session_);
    return session_;
  }
  SessionId session_id() {
    FXL_DCHECK(session_id_);
    return session_id_;
  }

  void KillSession();

 private:
  Scenic* const scenic_;
  Session* const session_;
  const SessionId session_id_;
};

class CommandDispatcher {
 public:
  explicit CommandDispatcher(CommandDispatcherContext context);
  virtual ~CommandDispatcher();

  virtual void DispatchCommand(fuchsia::ui::scenic::Command command) = 0;

  CommandDispatcherContext* command_dispatcher_context() { return &context_; }

 private:
  CommandDispatcherContext context_;
  FXL_DISALLOW_COPY_AND_ASSIGN(CommandDispatcher);
};

using CommandDispatcherUniquePtr =
    std::unique_ptr<CommandDispatcher, std::function<void(CommandDispatcher*)>>;

// TODO(SCN-421): Remove this once view manager is another Scenic system.
class TempSessionDelegate : public CommandDispatcher {
 public:
  explicit TempSessionDelegate(CommandDispatcherContext context);

  virtual void Present(uint64_t presentation_time, std::vector<zx::event> acquire_fences,
                       std::vector<zx::event> release_fences,
                       fuchsia::ui::scenic::Session::PresentCallback callback) = 0;

  virtual void Present2(zx_time_t requested_presentation_time,
                        std::vector<zx::event> acquire_fences,
                        std::vector<zx::event> release_fences) = 0;

  virtual void SetDebugName(const std::string& debug_name) = 0;

  virtual std::vector<fuchsia::scenic::scheduling::PresentationInfo> GetFuturePresentationInfos(
      zx::duration requested_prediction_span) = 0;

  virtual void SetOnFramePresentedCallback(OnFramePresentedCallback callback) = 0;

  virtual void KillSession() = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(TempSessionDelegate);
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SCENIC_COMMAND_DISPATCHER_H_
